#include "sheeteditor.h"

#include "canvasview.h"
#include "grid.h"
#include "sheetedit.h"

#include <QApplication>
#include <QLineF>

#include <algorithm>

namespace ppcb {

namespace {

// How near the mouse must be to an object to take it, in screen pixels.
const double HIT_PIXELS = 4.0;
// How many components a step's name lists before it stops.
const int NAMED_COMPONENTS = 3;

} // namespace

SheetEditor::SheetEditor(Document *document, CanvasView *canvas, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_canvas(canvas)
{
}

void SheetEditor::setSheet(int sheet)
{
    m_sheet = sheet;
    m_placing = false;
    m_selection.clear();
    m_pressed = false;
    m_dragging = false;
    m_rubberBanding = false;
    emit changed();
}

void SheetEditor::setSelection(const QSet<ObjectId> &ids)
{
    m_selection.clear();
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
    if (m_placing) {
        hover(snapped(at));
        placeGhost();
        return;
    }
    m_pressed = true;
    m_pressAt = at;
    m_pressAdding = adding;
    const ObjectId hit = objectAt(shapes(), at, pixelsToMm(HIT_PIXELS));
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
        if (!m_selection.contains(hit))
            m_selection = {hit};
        m_pressOnSelection = true;
    }
    emit changed();
}

void SheetEditor::move(const QPointF &at)
{
    const Sheet *sheet = currentSheet();
    if (m_placing) {
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
            m_dragBase = *sheet;
        } else
            m_rubberBanding = true;
    }
    if (m_dragging) {
        // The selection moves by whole grid steps, the way the cursor does.
        m_dragDelta = snapped(at) - snapped(m_pressAt);
        m_preview = movedObjects(m_dragBase, m_selection, m_dragDelta);
    } else {
        m_rubberBand = QRectF(m_pressAt, at).normalized();
    }
    emit changed();
}

void SheetEditor::release(const QPointF &at)
{
    if (m_placing)
        return;
    move(at);
    // A drag that came back to where it started may still have turned.
    if (m_dragging) {
        const Sheet moved = m_preview;
        m_dragging = false;
        apply(actionName(Action::Move), moved);
    } else if (m_rubberBanding) {
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
    if (m_dragging || m_rubberBanding) {
        m_dragging = false;
        m_rubberBanding = false;
        m_pressed = false;
    } else {
        m_selection.clear();
    }
    emit changed();
}

void SheetEditor::selectAll()
{
    m_selection.clear();
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
    m_preview = movedObjects(m_dragBase, m_selection, m_dragDelta);
    emit changed();
}

void SheetEditor::rotateSelection()
{
    if (m_placing) {
        Sheet ghost;
        ghost.components.append(m_ghost);
        m_ghost = rotatedObjects(ghost, {m_ghost.id}, m_ghost.at).components.first();
        updateGhost();
        return;
    }
    if (m_dragging) {
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
        Sheet ghost;
        ghost.components.append(m_ghost);
        m_ghost = mirroredObjects(ghost, {m_ghost.id}, m_ghost.at).components.first();
        updateGhost();
        return;
    }
    if (m_dragging) {
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
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || m_selection.isEmpty())
        return;
    // Named before the objects are gone.
    const QString name = stepName(actionName(Action::Delete));
    Design design = m_document->design();
    design.schematic.sheets[m_sheet] = withoutObjects(*sheet, m_selection);
    const QSet<ObjectId> deleted = m_selection;
    m_selection.clear();
    m_document->change(name, design, m_sheet, deleted);
    emit changed();
}

void SheetEditor::startPlacing(const Library &source, const QString &part,
                               const QString &footprint)
{
    if (currentSheet() == nullptr || source.findPart(part) == nullptr)
        return;
    m_placing = true;
    m_dragging = false;
    m_rubberBanding = false;
    m_pressed = false;
    m_selection.clear();
    m_placeSource = source;
    m_placePart = part;
    const Point at = m_ghost.at;
    m_ghost = newComponent(source, part, footprint);
    m_ghost.at = at;
    updateGhost();
}

void SheetEditor::stopPlacing()
{
    m_placing = false;
    emit changed();
}

void SheetEditor::hover(const Point &at)
{
    if (!m_placing || m_ghost.at == at)
        return;
    m_ghost.at = at;
    updateGhost();
}

void SheetEditor::updateGhost()
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr) {
        m_placing = false;
        emit changed();
        return;
    }
    const Design &design = m_document->design();
    m_placeLibrary = withPartFrom(design.library, m_placeSource, m_placePart);
    // Numbered as it would be if placed now: the project's own part says
    // the prefix.
    const Part *part = m_placeLibrary.findPart(m_placePart);
    m_ghost.designator =
        nextDesignator(design.schematic, designatorPrefix(*part, design.project.designatorStyle));
    m_preview = *sheet;
    m_preview.components.append(m_ghost);
    emit changed();
}

void SheetEditor::placeGhost()
{
    Design design = m_document->design();
    design.library = m_placeLibrary;
    design.schematic.sheets[m_sheet].components.append(m_ghost);
    const QString name = actionName(Action::Place) + QLatin1Char(' ') + m_ghost.designator
                         + QStringLiteral(" (")
                         + sheetName(design.schematic.sheets.at(m_sheet), m_sheet)
                         + QLatin1Char(')');
    m_document->change(name, design, m_sheet, {m_ghost.id});
    // The next one: a new object, numbered on, turned the same way.
    m_ghost.id = newObjectId();
    updateGhost();
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
    const Sheet *sheet = currentSheet();
    QStringList names;
    for (const Component &component : sheet->components) {
        if (m_selection.contains(component.id))
            names.append(component.designator);
    }
    const int others = int(m_selection.size() - names.size());
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
    }
    return QString();
}

QString SheetEditor::sheetName(const Sheet &sheet, int index)
{
    return sheet.title.isEmpty() ? tr("Sheet %1").arg(index + 1) : sheet.title;
}

} // namespace ppcb
