#include "sheeteditor.h"

#include "canvasview.h"
#include "grid.h"
#include "sheetedit.h"
#include "wiring.h"

#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLineF>
#include <QToolTip>

#include <algorithm>

namespace ppcb {

namespace {

// How near the mouse must be to an object to take it, in screen pixels.
const double HIT_PIXELS = 4.0;
// How near the mouse must be to a wire's end to take the end alone.
const double END_PIXELS = 8.0;
// How long the mouse stays on a net before it is lit, in milliseconds.
const int HOVER_DELAY_MS = 1000;
// How many components a step's name lists before it stops.
const int NAMED_COMPONENTS = 3;
// The line a value is edited in holds at least this many letters.
const int VALUE_EDIT_LETTERS = 12;

// True when the object 'id' is found by its line: a wire, a bus, a drawing.
bool isFoundByLine(const QList<SheetObjectShape> &shapes, const ObjectId &id)
{
    for (const SheetObjectShape &shape : shapes) {
        if (shape.id == id)
            return !shape.polyline.isEmpty();
    }
    return false;
}

} // namespace

SheetEditor::SheetEditor(Document *document, CanvasView *canvas, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_canvas(canvas)
{
    connect(m_document, &Document::designChanged, this, &SheetEditor::refreshPreview);
    // The nets are stale, and so is the index of the one lit.
    connect(m_document, &Document::designChanged, this, [this] {
        m_netsBuilt = false;
        m_problemsFound = false;
        clearHoveredNet();
        emit netsChanged();
    });
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(HOVER_DELAY_MS);
    connect(&m_hoverTimer, &QTimer::timeout, this, [this] {
        // Placing or drawing wires may have begun from the keyboard since.
        if (currentSheet() == nullptr || m_placing || m_wiring || m_pressed) {
            clearHoveredNet();
            return;
        }
        setHoveredNet(m_pendingNet);
        if (m_hoveredNet >= 0)
            showNetTip(m_nets.at(m_hoveredNet));
    });
}

void SheetEditor::setSheet(int sheet)
{
    // Set on the sheet it was begun on.
    finishTextEdit(true);
    m_sheet = sheet;
    m_placing = false;
    m_wiring = false;
    m_wirePoints.clear();
    m_selection.clear();
    dropPieces();
    m_pressed = false;
    m_dragging = false;
    m_rubberBanding = false;
    clearHoveredNet();
    emit changed();
}

void SheetEditor::setSelection(const QSet<ObjectId> &ids)
{
    // 'ids' may be the selection itself, which is cleared next.
    const QSet<ObjectId> wanted = ids;
    m_selection.clear();
    dropSegment();
    for (const SheetObjectShape &shape : shapes()) {
        if (wanted.contains(shape.id))
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

void SheetEditor::press(const QPointF &at, bool adding, bool net)
{
    if (currentSheet() == nullptr)
        return;
    clearHoveredNet();
    if (m_wiring) {
        wireClick(snapped(at));
        return;
    }
    if (m_placing && m_askLabelText) {
        // A click with the line open takes what it holds; else the label
        // stops where it is and the line opens over it.
        if (!m_textEdit.isNull()) {
            finishTextEdit(true);
            return;
        }
        hover(snapped(at));
        if (!m_floating.labels.isEmpty())
            editText(m_floating.labels.first().id);
        return;
    }
    if (m_placing) {
        hover(snapped(at));
        if (m_onPartPin && !m_floating.noConnects.isEmpty()) {
            const Point point = m_floatAt;
            const Sheet *sheet = currentSheet();
            const bool marked = std::any_of(sheet->noConnects.cbegin(), sheet->noConnects.cend(),
                                            [&point](const NoConnect &m) { return m.at == point; });
            if (marked || !partPinPoints(*sheet, m_document->design().library).contains(point))
                return;
        }
        placeFloating();
        return;
    }
    m_pressed = true;
    m_pressAt = at;
    m_pressAdding = adding;
    m_endLine = ObjectId();
    m_chosenText = TextHit();
    m_narrowTo = ObjectId();
    dropSegment();
    const QList<SheetObjectShape> found = shapes();
    ObjectId hit = objectAt(found, at, pixelsToMm(HIT_PIXELS));
    // The name of a bus lies off its line, which alone finds the bus, or
    // near enough for the line to be found: a click on the name takes it.
    const TextHit name = textAt(*currentSheet(), m_document->design().library, at);
    const bool onName =
        !name.owner.isNull() && (hit.isNull() || hit == name.owner)
        && std::any_of(currentSheet()->buses.cbegin(), currentSheet()->buses.cend(),
                       [&name](const Bus &bus) { return bus.id == name.owner; });
    if (onName)
        hit = name.owner;
    m_dragAnchor = dragAnchor(*currentSheet(), hit, at);
    // Shift on a wire: every wire joined to it, added with Ctrl.
    const QSet<ObjectId> wires = net ? joinedWires(*currentSheet(), hit) : QSet<ObjectId>();
    if (!wires.isEmpty()) {
        if (adding)
            m_selection.unite(wires);
        else
            m_selection = wires;
        m_pressOnSelection = !adding;
        emit changed();
        return;
    }
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
        const Sheet *sheet = currentSheet();
        // A text of a component or a label, unless a line is taken over it:
        // it is chosen alone, and a drag moves it while its object stays.
        const TextHit text = isFoundByLine(found, hit) && !onName
                                 ? TextHit()
                                 : textAt(*sheet, m_document->design().library, at);
        // A wire or a bus not chosen whole: the segment under the cursor is
        // chosen, and a drag moves it, or the line's end when taken near it.
        QList<Point> line;
        if (text.owner.isNull() && !m_selection.contains(hit))
            line = linePoints(*sheet, hit);
        const bool isBus = std::any_of(sheet->buses.cbegin(), sheet->buses.cend(),
                                       [&hit](const Bus &bus) { return bus.id == hit; });
        if (!text.owner.isNull()) {
            m_selection.clear();
            m_chosenText = text;
            m_dragAnchor = textPoint(*sheet, text);
        } else if (line.size() >= 2) {
            m_selection.clear();
            m_segmentLine = hit;
            m_segmentIndex = segmentAt(line, at);
            const double first = QLineF(at, toMm(line.first())).length();
            const double last = QLineF(at, toMm(line.last())).length();
            // A wire's end on a pin is not taken alone: that would tear it off.
            const Point end = last < first ? line.last() : line.first();
            const bool onPin =
                !isBus && pinPoints(*sheet, m_document->design().library).contains(end);
            if (qMin(first, last) <= pixelsToMm(END_PIXELS) && !onPin) {
                m_endLine = hit;
                m_endIsLast = last < first;
                m_dragAnchor = end;
            } else {
                m_dragAnchor = line.at(m_segmentIndex);
            }
        } else if (!m_selection.contains(hit)) {
            m_selection = {hit};
        } else {
            // A drag takes all that is chosen, a click this one alone.
            m_narrowTo = hit;
        }
    }
    emit changed();
}

bool SheetEditor::hasSegment() const
{
    const Sheet *sheet = currentSheet();
    if (m_segmentLine.isNull() || !m_selection.isEmpty() || sheet == nullptr)
        return false;
    return m_segmentIndex >= 0 && m_segmentIndex + 1 < linePoints(*sheet, m_segmentLine).size();
}

QLineF SheetEditor::chosenSegment() const
{
    if (!hasSegment() || m_dragging)
        return QLineF();
    const QList<Point> line = linePoints(*currentSheet(), m_segmentLine);
    return QLineF(toMm(line.at(m_segmentIndex)), toMm(line.at(m_segmentIndex + 1)));
}

bool SheetEditor::hasText() const
{
    const Sheet *sheet = currentSheet();
    return m_selection.isEmpty() && sheet != nullptr && ppcb::hasText(*sheet, m_chosenText);
}

QRectF SheetEditor::chosenText() const
{
    if (!hasText() || m_dragging)
        return QRectF();
    return textRect(*currentSheet(), m_document->design().library, m_chosenText);
}

QLineF SheetEditor::textGuide() const
{
    if (!m_dragging || !hasText())
        return QLineF();
    return QLineF(toMm(textOwnerPoint(m_preview, m_chosenText)),
                  toMm(textPoint(m_preview, m_chosenText)));
}

bool SheetEditor::dragsWhole() const
{
    return m_endLine.isNull() && !hasText() && !hasSegment();
}

QSet<ObjectId> SheetEditor::chosenIds() const
{
    if (hasSegment())
        return {m_segmentLine};
    if (hasText())
        return {m_chosenText.owner};
    return m_selection;
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
    // The value of a component, the text of a label or the name of a bus
    // chosen alone.
    if (hasText()) {
        const ObjectId owner = m_chosenText.owner;
        const bool label = std::any_of(sheet->labels.cbegin(), sheet->labels.cend(),
                                       [&owner](const Label &l) { return l.id == owner; });
        const bool bus = std::any_of(sheet->buses.cbegin(), sheet->buses.cend(),
                                     [&owner](const Bus &b) { return b.id == owner; });
        if (label || bus || m_chosenText.text == ComponentText::Value) {
            editText(m_chosenText.owner);
            return;
        }
    }
    const ObjectId hit = objectAt(shapes(), at, pixelsToMm(HIT_PIXELS));
    for (const PowerSymbol &power : sheet->powerSymbols) {
        if (power.id == hit) {
            editText(hit);
            return;
        }
    }
    for (const Label &label : sheet->labels) {
        if (label.id == hit) {
            editText(hit);
            return;
        }
    }
    for (const Component &component : sheet->components) {
        if (component.id == hit) {
            emit componentOpened(component.part, component.footprint);
            return;
        }
    }
    // A wire or a bus is chosen whole.
    if (!adding) {
        const bool line = std::any_of(sheet->wires.cbegin(), sheet->wires.cend(),
                                      [&hit](const Wire &w) { return w.id == hit; })
                          || std::any_of(sheet->buses.cbegin(), sheet->buses.cend(),
                                         [&hit](const Bus &b) { return b.id == hit; });
        if (line) {
            dropSegment();
            m_endLine = ObjectId();
            m_selection = {hit};
            emit changed();
        }
    }
}

ObjectId SheetEditor::chooseForMenu(const QPointF &at)
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || m_placing || m_wiring)
        return ObjectId();
    finishTextEdit(true);
    const Library &library = m_document->design().library;
    const ObjectId hit = objectAt(shapes(), at, pixelsToMm(HIT_PIXELS));
    const TextHit text = textAt(*sheet, library, at);
    const bool chosen =
        (!hit.isNull() && m_selection.contains(hit))
        || (hasText() && text.owner == m_chosenText.owner && text.text == m_chosenText.text)
        || (hasSegment() && hit == m_segmentLine
            && segmentAt(linePoints(*sheet, hit), at) == m_segmentIndex);
    if (!chosen && (!hit.isNull() || !text.owner.isNull())) {
        press(at, false);
        release(at, false);
    }
    const auto isIn = [](const auto &objects, const ObjectId &id) {
        return std::any_of(objects.cbegin(), objects.cend(),
                           [&id](const auto &object) { return object.id == id; });
    };
    // A label's text, a bus's name or a component's value under the cursor.
    if (!text.owner.isNull()
        && (isIn(sheet->labels, text.owner) || isIn(sheet->buses, text.owner)
            || text.text == ComponentText::Value)) {
        return text.owner;
    }
    if (isIn(sheet->powerSymbols, hit) || isIn(sheet->labels, hit) || isIn(sheet->buses, hit))
        return hit;
    for (const Component &component : sheet->components) {
        if (component.id == hit
            && !componentTextShown(component, ComponentText::Value, library).isEmpty()) {
            return hit;
        }
    }
    return ObjectId();
}

QString SheetEditor::editInPlaceName(const ObjectId &id) const
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr)
        return QString();
    const auto isIn = [&id](const auto &objects) {
        return std::any_of(objects.cbegin(), objects.cend(),
                           [&id](const auto &object) { return object.id == id; });
    };
    if (isIn(sheet->components))
        return actionName(Action::Value);
    if (isIn(sheet->powerSymbols) || isIn(sheet->labels) || isIn(sheet->buses))
        return actionName(Action::Rename);
    return QString();
}

void SheetEditor::editText(const ObjectId &id)
{
    finishTextEdit(true);
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr)
        return;
    const Library &library = m_document->design().library;
    QString text;
    QRectF rect;
    for (const Component &component : sheet->components) {
        if (component.id == id) {
            text = component.value;
            rect = componentTextRect(component, ComponentText::Value, library);
        }
    }
    for (const PowerSymbol &power : sheet->powerSymbols) {
        if (power.id == id) {
            text = power.name;
            rect = powerNameRect(power, library);
        }
    }
    for (const Bus &bus : sheet->buses) {
        if (bus.id == id) {
            text = bus.name;
            rect = busNameRect(bus);
        }
    }
    // A label of the sheet, or the one being placed, where it stands.
    const QList<Label> labels = sheet->labels + (m_placing ? m_placed.labels : QList<Label>());
    for (const Label &label : labels) {
        if (label.id == id) {
            text = label.text;
            rect = labelTextRect(label);
        }
    }
    if (rect.isNull())
        return;
    m_textOwner = id;
    m_textEditRect = rect;
    auto *edit = new QLineEdit(text, m_canvas->viewport());
    edit->selectAll();
    edit->installEventFilter(this);
    connect(edit, &QLineEdit::returnPressed, this, [this]() { finishTextEdit(true); });
    // The line goes with its text when the view is zoomed or scrolled.
    connect(m_canvas, &CanvasView::viewMoved, edit, [this]() { placeTextEdit(); });
    m_textEdit = edit;
    placeTextEdit();
    edit->show();
    edit->setFocus();
}

void SheetEditor::placeTextEdit()
{
    if (m_textEdit.isNull())
        return;
    QLineEdit *edit = m_textEdit;
    // Over the text, at least wide enough to type a value into.
    const QRect place = m_canvas->mapFromScene(m_textEditRect).boundingRect();
    const int width =
        qMax(place.width(), edit->fontMetrics().averageCharWidth() * VALUE_EDIT_LETTERS);
    const int height = edit->sizeHint().height();
    edit->setGeometry(place.center().x() - width / 2, place.center().y() - height / 2, width,
                      height);
}

void SheetEditor::finishTextEdit(bool keep)
{
    if (m_textEdit.isNull())
        return;
    QLineEdit *edit = m_textEdit;
    m_textEdit.clear();
    // Hiding it takes its focus away: that must not end it again.
    edit->removeEventFilter(this);
    edit->disconnect(this);
    disconnect(m_canvas, nullptr, edit, nullptr);
    const QString value = edit->text();
    edit->hide();
    edit->deleteLater();
    m_canvas->setFocus();
    if (!keep)
        return;
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr)
        return;
    if (m_placing && m_askLabelText && !m_floating.labels.isEmpty()
        && m_floating.labels.first().id == m_textOwner) {
        placeLabel(value);
        return;
    }
    for (const PowerSymbol &power : sheet->powerSymbols) {
        if (power.id == m_textOwner) {
            setPowerName({m_textOwner}, value);
            return;
        }
    }
    for (const Label &label : sheet->labels) {
        if (label.id == m_textOwner) {
            setLabelText({m_textOwner}, value);
            return;
        }
    }
    for (const Bus &bus : sheet->buses) {
        if (bus.id == m_textOwner) {
            setBusName({m_textOwner}, value);
            return;
        }
    }
    setComponentValue({m_textOwner}, value);
}

QList<Component> SheetEditor::chosenComponents() const
{
    QList<Component> chosen;
    const QSet<ObjectId> ids = chosenIds();
    if (const Sheet *sheet = currentSheet()) {
        for (const Component &component : sheet->components) {
            if (ids.contains(component.id))
                chosen.append(component);
        }
    }
    return chosen;
}

void SheetEditor::setComponentValue(const QSet<ObjectId> &ids, const QString &value)
{
    if (const Sheet *sheet = currentSheet())
        applyToComponents(Action::Value, ids, withComponentValue(*sheet, ids, value));
}

void SheetEditor::setLabelText(const QSet<ObjectId> &ids, const QString &text)
{
    if (const Sheet *sheet = currentSheet())
        applyToComponents(Action::Rename, ids, withLabelText(*sheet, ids, text));
}

void SheetEditor::setBusName(const QSet<ObjectId> &ids, const QString &name)
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr)
        return;
    const QString given =
        name.trimmed().isEmpty() ? nextBusName(m_document->design().schematic) : name;
    applyToComponents(Action::Rename, ids, withBusName(*sheet, ids, given));
}

void SheetEditor::setPowerName(const QSet<ObjectId> &ids, const QString &name)
{
    if (const Sheet *sheet = currentSheet())
        applyToComponents(Action::Rename, ids, withPowerName(*sheet, ids, name));
}

void SheetEditor::setComponentDesignator(const ObjectId &id, const QString &designator)
{
    if (const Sheet *sheet = currentSheet())
        applyToComponents(Action::Designator, {id},
                          withComponentDesignator(*sheet, {id}, designator));
}

void SheetEditor::setComponentFootprint(const QSet<ObjectId> &ids, const QString &footprint)
{
    if (const Sheet *sheet = currentSheet())
        applyToComponents(Action::Footprint, ids, withComponentFootprint(*sheet, ids, footprint));
}

void SheetEditor::setComponentOffBoard(const QSet<ObjectId> &ids, bool offBoard)
{
    if (const Sheet *sheet = currentSheet())
        applyToComponents(offBoard ? Action::OffBoard : Action::OnBoard, ids,
                          withComponentOffBoard(*sheet, ids, offBoard));
}

void SheetEditor::applyToComponents(Action action, const QSet<ObjectId> &ids,
                                    const Sheet &edited)
{
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || edited == *sheet)
        return;
    // Named by the designators they had before.
    const QString name = stepName(actionName(action), *sheet, ids);
    Design design = m_document->design();
    design.schematic.sheets[m_sheet] = edited;
    m_document->change(name, design, m_sheet, ids);
    emit changed();
}

bool SheetEditor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_textEdit.data())
        return QObject::eventFilter(watched, event);
    switch (event->type()) {
    case QEvent::ShortcutOverride: {
        // Esc and Enter belong to the line, not to the window's actions.
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::KeyPress:
        if (static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            finishTextEdit(false);
            return true;
        }
        break;
    case QEvent::FocusOut:
        // A press of the panning buttons on the canvas takes the focus, but
        // the view only moves: the line keeps it. A short right click opens
        // a menu, which takes the focus again and ends the line.
        if (static_cast<QFocusEvent *>(event)->reason() == Qt::MouseFocusReason
            && (QApplication::mouseButtons() & (Qt::RightButton | Qt::MiddleButton))
            && (QApplication::focusWidget() == m_canvas
                || QApplication::focusWidget() == m_canvas->viewport())) {
            QLineEdit *edit = m_textEdit;
            QTimer::singleShot(0, edit, [edit]() { edit->setFocus(); });
            break;
        }
        finishTextEdit(true);
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
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
    } else if (m_pressed && !m_narrowTo.isNull()) {
        m_selection = {m_narrowTo};
    }
    m_narrowTo = ObjectId();
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
        dropPieces();
        m_chosenNet.clear();
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
        for (const PowerSymbol &power : sheet.powerSymbols) {
            if (m_selection.contains(power.id))
                return power.at;
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
    if (hasText()) {
        m_preview = withTextMoved(m_dragStart, m_chosenText, m_dragDelta);
        return;
    }
    const Library &library = m_document->design().library;
    const auto isBus = [this](const ObjectId &id) {
        return std::any_of(m_dragStart.buses.cbegin(), m_dragStart.buses.cend(),
                           [&id](const Bus &bus) { return bus.id == id; });
    };
    if (!m_endLine.isNull()) {
        const QList<Point> line = linePoints(m_dragStart, m_endLine);
        if (!line.isEmpty()) {
            const Point to = (m_endIsLast ? line.last() : line.first()) + m_dragDelta;
            m_preview = isBus(m_endLine)
                            ? withBusEndAt(m_dragStart, library, m_endLine, m_endIsLast, to,
                                           m_wireRouting)
                            : withWireEndAt(m_dragStart, library, m_endLine, m_endIsLast, to,
                                            m_wireRouting);
        }
        return;
    }
    if (hasSegment()) {
        m_preview = isBus(m_segmentLine)
                        ? withBusSegmentMoved(m_dragStart, library, m_segmentLine,
                                              m_segmentIndex, m_dragDelta, m_wireRouting)
                        : withSegmentMoved(m_dragStart, library, m_segmentLine, m_segmentIndex,
                                           m_dragDelta, m_wireRouting);
        return;
    }
    const Sheet moved = movedObjects(m_dragBase, m_selection, m_dragDelta);
    m_preview = m_dragDetached ? moved
                               : withWiresFollowing(m_dragStart, moved, m_selection,
                                                    m_document->design().library, m_wireRouting);
}

void SheetEditor::rotateSelection()
{
    if (m_placing) {
        m_floating = rotatedObjects(m_floating, objectIds(m_floating), Point());
        updateFloating();
        return;
    }
    if (m_dragging) {
        if (dragsWhole())
            transformDragged(rotatedObjects);
        return;
    }
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || m_selection.isEmpty())
        return;
    apply(actionName(Action::Rotate),
          withWiresFollowing(*sheet, rotatedObjects(*sheet, m_selection, selectionCenter(*sheet)),
                             m_selection, m_document->design().library, m_wireRouting));
}

void SheetEditor::mirrorSelection()
{
    if (m_placing) {
        m_floating = mirroredObjects(m_floating, objectIds(m_floating), Point());
        updateFloating();
        return;
    }
    if (m_dragging) {
        if (dragsWhole())
            transformDragged(mirroredObjects);
        return;
    }
    const Sheet *sheet = currentSheet();
    if (sheet == nullptr || m_selection.isEmpty())
        return;
    apply(actionName(Action::Mirror),
          withWiresFollowing(*sheet, mirroredObjects(*sheet, m_selection, selectionCenter(*sheet)),
                             m_selection, m_document->design().library, m_wireRouting));
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
        design.schematic.sheets[m_sheet] = withoutSegment(*sheet, m_segmentLine, m_segmentIndex);
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

void SheetEditor::startPlacingPower(const Library &source, const QString &symbol)
{
    const Symbol *drawing = source.findSymbol(symbol);
    if (drawing == nullptr || !drawing->isPower())
        return;
    PowerSymbol power;
    power.id = newObjectId();
    power.name = drawing->power;
    power.symbol = symbol;
    Sheet piece;
    piece.powerSymbols.append(power);
    startFloating(piece, source, Action::Place, true);
}

void SheetEditor::startPlacingLabel()
{
    Label label;
    label.id = newObjectId();
    label.text = m_nextLabelText;
    Sheet piece;
    piece.labels.append(label);
    startFloating(piece, m_document->design().library, Action::Place, true);
    m_askLabelText = m_placing;
}

void SheetEditor::startPlacingNoConnect()
{
    NoConnect mark;
    mark.id = newObjectId();
    Sheet piece;
    piece.noConnects.append(mark);
    startFloating(piece, m_document->design().library, Action::Place, true);
    m_onPartPin = m_placing;
}

void SheetEditor::placeLabel(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return;
    m_floating.labels.first().text = trimmed;
    placeFloating();
    // The next one, numbered on, follows the cursor again.
    m_nextLabelText = nextLabelText(trimmed);
    if (m_placing && !m_floating.labels.isEmpty()) {
        m_floating.labels.first().text = m_nextLabelText;
        updateFloating();
    }
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
        const Sheet piece = segmentOf(sheet, m_segmentLine, m_segmentIndex);
        const QList<Point> ends = linePoints(piece, m_segmentLine);
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
    m_askLabelText = false;
    m_onPartPin = false;
    m_wiring = false;
    m_wirePoints.clear();
    m_dragging = false;
    m_rubberBanding = false;
    m_pressed = false;
    m_selection.clear();
    dropPieces();
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
    // A label waiting for its text stays where it is.
    if (!m_placing || m_floatAt == at || !m_textEdit.isNull())
        return;
    m_floatAt = at;
    updateFloating();
}

void SheetEditor::hoverNet(const QPointF &at)
{
    if (currentSheet() == nullptr || m_placing || m_wiring || m_pressed) {
        clearHoveredNet();
        return;
    }
    const Design &design = m_document->design();
    const int net =
        netAt(nets(), design.schematic, design.library, m_sheet, at, pixelsToMm(HIT_PIXELS));
    if (net >= 0 && net == m_hoveredNet) {
        // Shown again after it went out by itself or with a click.
        showNetTip(m_nets.at(net));
        return;
    }
    // Off the net lit: it goes out at once, and the next waits its time.
    setHoveredNet(-1);
    if (net < 0) {
        m_hoverTimer.stop();
        m_pendingNet = -1;
    } else if (net != m_pendingNet) {
        m_pendingNet = net;
        m_hoverTimer.start();
    }
}

void SheetEditor::clearHoveredNet()
{
    m_hoverTimer.stop();
    m_pendingNet = -1;
    setHoveredNet(-1);
}

const QList<Net> &SheetEditor::nets() const
{
    if (!m_netsBuilt) {
        const Design &design = m_document->design();
        m_nets = ppcb::nets(design.schematic, design.library, design.project);
        m_netsBuilt = true;
    }
    return m_nets;
}

const QList<ErcProblem> &SheetEditor::problems() const
{
    if (!m_problemsFound) {
        const Design &design = m_document->design();
        m_problems = checkSchematic(design.schematic, design.library, design.project, nets());
        m_problemsFound = true;
    }
    return m_problems;
}

void SheetEditor::setChosenNet(const QString &name)
{
    if (name == m_chosenNet)
        return;
    m_chosenNet = name;
    emit changed();
}

const Net *SheetEditor::litNet() const
{
    if (m_hoveredNet >= 0)
        return &m_nets.at(m_hoveredNet);
    if (m_chosenNet.isEmpty())
        return nullptr;
    for (const Net &net : nets()) {
        if (net.name == m_chosenNet)
            return &net;
    }
    return nullptr;
}

void SheetEditor::showNetTip(const Net &net)
{
    QToolTip::showText(QCursor::pos(), net.name, m_canvas->viewport());
}

void SheetEditor::setHoveredNet(int net)
{
    if (net == m_hoveredNet)
        return;
    m_hoveredNet = net;
    if (net < 0)
        QToolTip::hideText();
    emit changed();
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
    for (const PowerSymbol &power : m_floating.powerSymbols)
        m_placeLibrary = withSymbolFrom(m_placeLibrary, m_floatSource, power.symbol);
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
    beginLines(false);
}

void SheetEditor::startDrawingBus()
{
    beginLines(true);
}

void SheetEditor::beginLines(bool bus)
{
    if (currentSheet() == nullptr)
        return;
    m_drawingBus = bus;
    m_placing = false;
    m_dragging = false;
    m_rubberBanding = false;
    m_pressed = false;
    m_selection.clear();
    dropPieces();
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
    // A bus goes on until the second click on its end or until it comes to
    // another bus, which it branches off; a wire stops where it joins
    // something, or on a bus, which it enters.
    const Sheet &sheet = *currentSheet();
    const bool joins = !m_drawingBus && isConnectionPoint(sheet, m_document->design().library, at);
    const bool entersBus = !joins && isOnBus(sheet, at);
    if (again || joins || entersBus)
        finishWire();
    else
        updateWire();
}

void SheetEditor::finishWire()
{
    const QList<Point> points = simplifiedWire(m_wirePoints);
    m_wirePoints.clear();
    if (points.size() < 2) {
        emit changed();
        return;
    }
    Design design = m_document->design();
    Sheet &sheet = design.schematic.sheets[m_sheet];
    ObjectId id = newObjectId();
    QString name;
    if (m_drawingBus) {
        Bus bus;
        bus.id = id;
        bus.points = points;
        bus.name = nextBusName(design.schematic);
        // Begun or ended on another bus, it is a branch of it, with its name.
        for (const Bus &other : sheet.buses) {
            Wire line;
            line.points = other.points;
            if (isOnWire(line, points.first()) || isOnWire(line, points.last())) {
                bus.name = other.name;
                break;
            }
        }
        sheet.buses.append(bus);
        name = actionName(Action::Bus) + QLatin1Char(' ') + bus.name;
    } else {
        Wire wire;
        wire.id = id;
        // Its ends on buses enter them at 45 degrees, the first as well.
        wire.points = withBusEntries(points, sheet, design.library, m_canvas->gridStep());
        sheet.wires.append(wire);
        name = actionName(Action::Wire);
    }
    name += QStringLiteral(" (") + sheetName(sheet, m_sheet) + QLatin1Char(')');
    m_document->change(name, design, m_sheet, {id});
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
        if (wire.points.size() >= 2 && m_drawingBus) {
            Bus bus;
            bus.points = wire.points;
            bus.name = nextBusName(m_document->design().schematic);
            m_preview.buses.append(bus);
        } else if (wire.points.size() >= 2) {
            // Shown as it will enter the buses it ends on.
            wire.points = withBusEntries(wire.points, *sheet, m_document->design().library,
                                         m_canvas->gridStep());
            m_preview.wires.append(wire);
        }
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
    for (const PowerSymbol &power : objects.powerSymbols) {
        if (ids.contains(power.id))
            names.append(power.name);
    }
    for (const Label &label : objects.labels) {
        if (ids.contains(label.id))
            names.append(label.text);
    }
    for (const Bus &bus : objects.buses) {
        if (ids.contains(bus.id))
            names.append(bus.name);
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
    case Action::Value:
        return tr("Change value");
    case Action::Designator:
        return tr("Change designator");
    case Action::Footprint:
        return tr("Change footprint");
    case Action::OffBoard:
        return tr("Take off board");
    case Action::OnBoard:
        return tr("Put on board");
    case Action::Rename:
        return tr("Rename");
    case Action::Bus:
        return tr("Bus");
    }
    return QString();
}

QString SheetEditor::sheetName(const Sheet &sheet, int index)
{
    return sheet.title.isEmpty() ? tr("Sheet %1").arg(index + 1) : sheet.title;
}

} // namespace ppcb
