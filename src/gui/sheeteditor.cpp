#include "sheeteditor.h"

#include "canvasview.h"
#include "grid.h"
#include "sheetedit.h"
#include "wiring.h"

#include <QApplication>
#include <QLineF>

#include <algorithm>

namespace ppcb {

namespace {

// How near the mouse must be to an object to take it, in screen pixels.
const double HIT_PIXELS = 4.0;
// How near the mouse must be to a wire's end to take the end alone.
const double END_PIXELS = 8.0;
// How many components a step's name lists before it stops.
const int NAMED_COMPONENTS = 3;

} // namespace

SheetEditor::SheetEditor(Document *document, CanvasView *canvas, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_canvas(canvas)
{
    connect(m_document, &Document::designChanged, this, &SheetEditor::refreshPreview);
}

void SheetEditor::setSheet(int sheet)
{
    m_sheet = sheet;
    m_placing = false;
    m_wiring = false;
    m_wirePoints.clear();
    m_selection.clear();
    dropSegment();
    m_pressed = false;
    m_dragging = false;
    m_rubberBanding = false;
    emit changed();
}

void SheetEditor::setSelection(const QSet<ObjectId> &ids)
{
    m_selection.clear();
    dropSegment();
    for (const SheetObjectShape &shape : shapes()) {
        if (ids.contains(shape.id))
            m_selection.insert(shape.id);
    }
    emit changed();
}

const Sheet *SheetEditor::currentSheet() const
{
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    if (m_sheet < 0 || m_sheet >= sheets.size())
        return nullptr;
    return &sheets.at(m_sheet);
}

QList<SheetObjectShape> SheetEditor::shapes() const
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr)
        return {};
    return sheetObjectShapes(*sheet, m_document->design().library);
}

double SheetEditor::pixelsToMm(double pixels) const
{
    return pixels / m_canvas->pixelsPerMm();
}

Point SheetEditor::snapped(const QPointF &at) const
{
    const Coord step = m_canvas->gridStep();
    return Point{snapToGrid(mmToNm(at.x()), step), snapToGrid(mmToNm(at.y()), step)};
}

void SheetEditor::press(const QPointF &at, bool adding)
{
    if (currentSheet() == nullptr)
        return;
    if (m_wiring) {
        wireClick(snapped(at));
        return;
    }
    if (m_placing) {
        hover(snapped(at));
        placeFloating();
        return;
    }
    m_pressed = true;
    m_pressAt = at;
    m_pressAdding = adding;
    m_endWire = ObjectId();
    dropSegment();
    const ObjectId hit = objectAt(shapes(), at, pixelsToMm(HIT_PIXELS));
    m_dragAnchor = dragAnchor(*currentSheet(), hit, at);
    if (hit.isNull()) {
        m_pressOnSelection = false;
        if (!adding)
            m_selection.clear();
    } else if (adding) {
        // Ctrl and a click switches the object in or out, and never drags.
        if (m_selection.contains(hit))
            m_selection.remove(hit);
        else
            m_selection.insert(hit);
        m_pressOnSelection = false;
    } else {
        m_pressOnSelection = true;
        // A wire not chosen whole: the segment under the cursor is chosen,
        // and a drag moves it, or the wire's end when taken near it.
        const Sheet *sheet = currentSheet();
        const Wire *wire = nullptr;
        if (sheet != nullptr && !m_selection.contains(hit)) {
            for (const Wire &candidate : sheet->wires) {
                if (candidate.id == hit && candidate.points.size() >= 2)
                    wire = &candidate;
            }
        }
        if (wire != nullptr) {
            m_selection.clear();
            m_segmentWire = hit;
            m_segmentIndex = segmentAt(*wire, at);
            const double first = QLineF(at, toMm(wire->points.first())).length();
            const double last = QLineF(at, toMm(wire->points.last())).length();
            if (qMin(first, last) <= pixelsToMm(END_PIXELS)) {
                m_endWire = hit;
                m_endIsLast = last < first;
                m_dragAnchor = m_endIsLast ? wire->points.last() : wire->points.first();
            } else {
                m_dragAnchor = wire->points.at(m_segmentIndex);
            }
        } else if (!m_selection.contains(hit)) {
            m_selection = {hit};
        }
    }
    emit changed();
}

bool SheetEditor::hasSegment() const
{
    const Sheet *sheet = currentSheet();
    if (m_segmentWire.isNull() || !m_selection.isEmpty() || sheet == nullptr)
        return false;
    for (const Wire &wire : sheet->wires) {
        if (wire.id == m_segmentWire)
            return m_segmentIndex >= 0 && m_segmentIndex + 1 < wire.points.size();
    }
    return false;
}

QLineF SheetEditor::chosenSegment() const
{
    if (!hasSegment() || m_dragging)
        return QLineF();
    for (const Wire &wire : currentSheet()->wires) {
        if (wire.id == m_segmentWire) {
            return QLineF(toMm(wire.points.at(m_segmentIndex)),
                          toMm(wire.points.at(m_segmentIndex + 1)));
        }
    }
    return QLineF();
}

QSet<ObjectId> SheetEditor::chosenIds() const
{
    return hasSegment() ? QSet<ObjectId>{m_segmentWire} : m_selection;
}

void SheetEditor::doubleClick(const QPointF &at, bool adding)
{
    // The first click ended a wire: the second must not begin another.
    if (m_wiring && m_wirePoints.isEmpty())
        return;
    const bool placing = m_placing || m_wiring;
    press(at, adding);
    const Sheet *sheet = currentSheet();
    if (placing || sheet == nullptr)
        return;
    const ObjectId hit = objectAt(shapes(), at, pixelsToMm(HIT_PIXELS));
    for (const Component &component : sheet->components) {
        if (component.id == hit) {
            emit componentOpened(component.part, component.footprint);
            return;
        }
    }
    // A wire is chosen whole.
    if (!adding) {
        for (const Wire &wire : sheet->wires) {
            if (wire.id == hit) {
                dropSegment();
                m_endWire = ObjectId();
                m_selection = {hit};
                emit changed();
                return;
            }
        }
    }
}

void SheetEditor::move(const QPointF &at, bool detached)
{
    const Sheet *sheet = currentSheet();
    if (m_placing || m_wiring) {
        hover(snapped(at));
        return;
    }
    if (!m_pressed || sheet == nullptr)
        return;
    if (!m_dragging && !m_rubberBanding) {
        if (QLineF(at, m_pressAt).length() < pixelsToMm(QApplication::startDragDistance()))
            return;
        if (m_pressOnSelection) {
            m_dragging = true;
            m_dragStart = *sheet;
            m_dragBase = *sheet;
        } else
            m_rubberBanding = true;
    }
    if (m_dragging) {
        // What was taken lands on the grid, wherever it stood before.
        m_dragDelta = snapped(toMm(m_dragAnchor) + (at - m_pressAt)) - m_dragAnchor;
        m_dragDetached = detached;
        updateDrag();
    } else {
        m_rubberBand = QRectF(m_pressAt, at).normalized();
    }
    emit changed();
}

void SheetEditor::release(const QPointF &at, bool detached)
{
    if (m_placing || m_wiring)
        return;
    move(at, detached);
    // A drag that came back to where it started may still have turned.
    if (m_dragging) {
        const Sheet moved = m_preview;
        m_dragging = false;
        apply(actionName(Action::Move), moved);
        // The wire's segments are counted anew: none stays chosen.
        dropSegment();
    } else if (m_rubberBanding) {
        dropSegment();
        const QSet<ObjectId> inside = objectsInside(shapes(), m_rubberBand);
        if (m_pressAdding)
            m_selection.unite(inside);
        else
            m_selection = inside;
    }
    m_pressed = false;
    m_dragging = false;
    m_rubberBanding = false;
    emit changed();
}

void SheetEditor::cancel()
{
    if (m_placing) {
        stopPlacing();
        return;
    }
    if (m_wiring) {
        // The wire begun goes first, the drawing of wires next.
        if (m_wirePoints.isEmpty())
            m_wiring = false;
        m_wirePoints.clear();
        emit changed();
        return;
    }
    if (m_dragging || m_rubberBanding) {
        m_dragging = false;
        m_rubberBanding = false;
        m_pressed = false;
    } else {
        m_selection.clear();
        dropSegment();
    }
    emit changed();
}

void SheetEditor::selectAll()
{
    m_selection.clear();
    dropSegment();
    for (const SheetObjectShape &shape : shapes())
        m_selection.insert(shape.id);
    emit changed();
}

Point SheetEditor::selectionCenter(const Sheet &sheet) const
{
    if (m_selection.size() == 1) {
        for (const Component &component : sheet.components) {
            if (m_selection.contains(component.id))
                return component.at;
        }
    }
    const QList<SheetObjectShape> found = sheetObjectShapes(sheet, m_document->design().library);
    return snapped(selectionBounds(found, m_selection).center());
}

void SheetEditor::transformDragged(Sheet (*transform)(const Sheet &, const QSet<ObjectId> &,
                                                      const Point &))
{
    m_dragBase = transform(m_dragBase, m_selection, selectionCenter(m_dragBase));
    updateDrag();
    emit changed();
}

void SheetEditor::updateDrag()
{
    if (!m_endWire.isNull()) {
        for (const Wire &wire : m_dragStart.wires) {
            if (wire.id == m_endWire) {
                const Point end = m_endIsLast ? wire.points.last() : wire.points.first();
                m_preview = withWireEndAt(m_dragStart, m_document->design().library, m_endWire,
                                          m_endIsLast, end + m_dragDelta);
            }
        }
        return;
    }
    if (hasSegment()) {
        m_preview = withSegmentMoved(m_dragStart, m_document->design().library, m_segmentWire,
                                     m_segmentIndex, m_dragDelta);
        return;
    }
    const Sheet moved = movedObjects(m_dragBase, m_selection, m_dragDelta);
    m_preview = m_dragDetached ? moved
                               : withWiresFollowing(m_dragStart, moved, m_selection,
                                                    m_document->design().library);
}

void SheetEditor::rotateSelection()
{
    if (m_placing) {
        m_floating = rotatedObjects(m_floating, objectIds(m_floating), Point());
        updateFloating();
        return;
    }
    if (m_dragging) {
        // A wire's end or segment alone is not turned.
        if (m_endWire.isNull() && !hasSegment())
            transformDragged(rotatedObjects);
        return;
    }
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || m_selection.isEmpty())
        return;
    apply(actionName(Action::Rotate),
          rotatedObjects(*sheet, m_selection, selectionCenter(*sheet)));
}

void SheetEditor::mirrorSelection()
{
    if (m_placing) {
        m_floating = mirroredObjects(m_floating, objectIds(m_floating), Point());
        updateFloating();
        return;
    }
    if (m_dragging) {
        // A wire's end or segment alone is not turned.
        if (m_endWire.isNull() && !hasSegment())
            transformDragged(mirroredObjects);
        return;
    }
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || m_selection.isEmpty())
        return;
    apply(actionName(Action::Mirror),
          mirroredObjects(*sheet, m_selection, selectionCenter(*sheet)));
}

void SheetEditor::deleteSelection()
{
    removeSelection(Action::Delete);
}

void SheetEditor::removeSelection(Action action)
{
    const Sheet *sheet = currentSheet();
    if (hasSegment()) {
        const QString name = stepName(actionName(action));
        Design design = m_document->design();
        design.schematic.sheets[m_sheet] = withoutSegment(*sheet, m_segmentWire, m_segmentIndex);
        dropSegment();
        m_document->change(name, design, m_sheet, QSet<ObjectId>());
        emit changed();
        return;
    }
    if (sheet == nullptr || m_selection.isEmpty())
        return;
    // Named before the objects are gone.
    const QString name = stepName(actionName(action));
    Design design = m_document->design();
    design.schematic.sheets[m_sheet] = withoutObjects(*sheet, m_selection);
    const QSet<ObjectId> deleted = m_selection;
    m_selection.clear();
    dropSegment();
    m_document->change(name, design, m_sheet, deleted);
    emit changed();
}

void SheetEditor::startPlacing(const Library &source, const QString &part,
                               const QString &footprint)
{
    if (source.findPart(part) == nullptr)
        return;
    Sheet piece;
    piece.components.append(newComponent(source, part, footprint));
    startFloating(piece, source, Action::Place, true);
}

void SheetEditor::copySelection()
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || m_placing || (m_selection.isEmpty() && !hasSegment()))
        return;
    m_clipboard = selectionPiece(*sheet);
    m_clipboardLibrary = m_document->design().library;
}

void SheetEditor::cutSelection()
{
    if (m_placing)
        return;
    copySelection();
    removeSelection(Action::Cut);
}

void SheetEditor::paste()
{
    if (m_placing || objectIds(m_clipboard).isEmpty())
        return;
    startFloating(withNewIds(m_clipboard), m_clipboardLibrary, Action::Paste, false);
}

void SheetEditor::duplicateSelection()
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || m_placing || (m_selection.isEmpty() && !hasSegment()))
        return;
    startFloating(withNewIds(selectionPiece(*sheet)), m_document->design().library,
                  Action::Duplicate, false);
}

Sheet SheetEditor::selectionPiece(const Sheet &sheet) const
{
    if (hasSegment()) {
        const Sheet piece = segmentOf(sheet, m_segmentWire, m_segmentIndex);
        const QList<Point> &ends = piece.wires.first().points;
        const Point center = snapped(QLineF(toMm(ends.first()), toMm(ends.last())).center());
        return movedObjects(piece, objectIds(piece), Point() - center);
    }
    const Point center = selectionCenter(sheet);
    const Sheet piece = objectsOf(sheet, m_selection);
    return movedObjects(piece, objectIds(piece), Point() - center);
}

void SheetEditor::startFloating(const Sheet &piece, const Library &source, Action action,
                                bool again)
{
    if (currentSheet() == nullptr || objectIds(piece).isEmpty())
        return;
    m_placing = true;
    m_wiring = false;
    m_wirePoints.clear();
    m_dragging = false;
    m_rubberBanding = false;
    m_pressed = false;
    m_selection.clear();
    dropSegment();
    m_floating = piece;
    m_floatSource = source;
    m_floatAction = action;
    m_floatAgain = again;
    m_floatAt = Point{m_canvas->cursorX(), m_canvas->cursorY()};
    updateFloating();
}

void SheetEditor::stopPlacing()
{
    m_placing = false;
    emit changed();
}

void SheetEditor::hover(const Point &at)
{
    if (m_wiring) {
        if (m_wireCursor != at) {
            m_wireCursor = at;
            updateWire();
        }
        return;
    }
    if (!m_placing || m_floatAt == at)
        return;
    m_floatAt = at;
    updateFloating();
}

void SheetEditor::updateFloating()
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr) {
        m_placing = false;
        emit changed();
        return;
    }
    const Design &design = m_document->design();
    m_placeLibrary = design.library;
    for (const Component &component : m_floating.components)
        m_placeLibrary = withPartFrom(m_placeLibrary, m_floatSource, component.part);
    // Numbered as it would be if placed now: the project's own parts say
    // the prefixes.
    const Sheet numbered = numberedOn(m_floating, design.schematic, m_placeLibrary,
                                      design.project.designatorStyle);
    m_placed = movedObjects(numbered, objectIds(numbered), m_floatAt);
    m_preview = withObjectsAdded(*sheet, m_placed);
    emit changed();
}

void SheetEditor::placeFloating()
{
    // Built again from the sheet as it is now: an Undo may have come since
    // the cursor last moved.
    updateFloating();
    if (!m_placing)
        return;
    Design design = m_document->design();
    design.library = m_placeLibrary;
    design.schematic.sheets[m_sheet] = m_preview;
    const QSet<ObjectId> placed = objectIds(m_placed);
    const QString name = stepName(actionName(m_floatAction), m_placed, placed);
    if (!m_floatAgain) {
        m_placing = false;
        m_selection = placed;
    }
    m_document->change(name, design, m_sheet, placed);
    if (m_placing) {
        // The next one: new objects, numbered on, turned the same way.
        m_floating = withNewIds(m_floating);
        updateFloating();
    } else {
        emit changed();
    }
}

void SheetEditor::startWiring()
{
    if (currentSheet() == nullptr)
        return;
    m_placing = false;
    m_dragging = false;
    m_rubberBanding = false;
    m_pressed = false;
    m_selection.clear();
    dropSegment();
    m_wiring = true;
    m_wirePoints.clear();
    m_wireCursor = Point{m_canvas->cursorX(), m_canvas->cursorY()};
    emit changed();
}

void SheetEditor::swapWireCorner()
{
    if (!m_wiring)
        return;
    m_wireSwapped = !m_wireSwapped;
    updateWire();
}

void SheetEditor::toggleWireSlant()
{
    if (!m_wiring)
        return;
    m_wireSlanted = !m_wireSlanted;
    updateWire();
}

void SheetEditor::wireClick(const Point &at)
{
    m_wireCursor = at;
    if (m_wirePoints.isEmpty()) {
        m_wirePoints.append(at);
        updateWire();
        return;
    }
    const bool again = at == m_wirePoints.last();
    m_wirePoints.append(wireRoute(m_wirePoints.last(), at, m_wireSwapped, m_wireSlanted));
    if (again || isConnectionPoint(*currentSheet(), m_document->design().library, at))
        finishWire();
    else
        updateWire();
}

void SheetEditor::finishWire()
{
    Wire wire;
    wire.id = newObjectId();
    wire.points = simplifiedWire(m_wirePoints);
    m_wirePoints.clear();
    if (wire.points.size() < 2) {
        emit changed();
        return;
    }
    Design design = m_document->design();
    design.schematic.sheets[m_sheet].wires.append(wire);
    const QString name = actionName(Action::Wire) + QStringLiteral(" (")
                         + sheetName(design.schematic.sheets.at(m_sheet), m_sheet)
                         + QLatin1Char(')');
    m_document->change(name, design, m_sheet, {wire.id});
    emit changed();
}

void SheetEditor::updateWire()
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr) {
        m_wiring = false;
        m_wirePoints.clear();
        emit changed();
        return;
    }
    m_preview = *sheet;
    if (!m_wirePoints.isEmpty()) {
        Wire wire;
        wire.points = m_wirePoints
                      + wireRoute(m_wirePoints.last(), m_wireCursor, m_wireSwapped, m_wireSlanted);
        if (wire.points.size() >= 2)
            m_preview.wires.append(wire);
    }
    emit changed();
}

void SheetEditor::refreshPreview()
{
    if (m_wiring)
        updateWire();
    else if (m_placing)
        updateFloating();
}

void SheetEditor::apply(const QString &action, const Sheet &edited)
{
    Design design = m_document->design();
    if (design.schematic.sheets.at(m_sheet) == edited)
        return;
    const QString name = stepName(action);
    design.schematic.sheets[m_sheet] = edited;
    m_document->change(name, design, m_sheet, m_selection);
}

QString SheetEditor::stepName(const QString &action) const
{
    return stepName(action, *currentSheet(), chosenIds());
}

QString SheetEditor::stepName(const QString &action, const Sheet &objects,
                              const QSet<ObjectId> &ids) const
{
    const Sheet *sheet = currentSheet();
    QStringList names;
    for (const Component &component : objects.components) {
        if (ids.contains(component.id))
            names.append(component.designator);
    }
    const int others = int(ids.size() - names.size());
    std::sort(names.begin(), names.end());
    if (names.size() > NAMED_COMPONENTS) {
        names = names.mid(0, NAMED_COMPONENTS);
        names.append(QStringLiteral("…"));
    }
    QString what = names.join(QStringLiteral(", "));
    if (others > 0) {
        if (!what.isEmpty())
            what += QStringLiteral(", ");
        what += tr("objects") + QStringLiteral(": ") + QString::number(others);
    }
    return action + QLatin1Char(' ') + what + QStringLiteral(" (") + sheetName(*sheet, m_sheet)
           + QLatin1Char(')');
}

QString SheetEditor::actionName(Action action)
{
    switch (action) {
    case Action::Move:
        return tr("Move");
    case Action::Rotate:
        return tr("Rotate");
    case Action::Mirror:
        return tr("Mirror");
    case Action::Delete:
        return tr("Delete");
    case Action::Place:
        return tr("Place");
    case Action::Copy:
        return tr("Copy");
    case Action::Cut:
        return tr("Cut");
    case Action::Paste:
        return tr("Paste");
    case Action::Duplicate:
        return tr("Duplicate");
    case Action::Wire:
        return tr("Wire");
    }
    return QString();
}

QString SheetEditor::sheetName(const Sheet &sheet, int index)
{
    return sheet.title.isEmpty() ? tr("Sheet %1").arg(index + 1) : sheet.title;
}

} // namespace ppcb
