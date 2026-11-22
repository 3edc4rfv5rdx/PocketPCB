#include "boardeditor.h"

#include "canvasview.h"
#include "format.h"
#include "outline.h"
#include "paint.h"

#include <QApplication>
#include <QLineF>

namespace ppcb {

namespace {

QList<OutlineCorner> sharpOutline(const QList<Point> &points)
{
    QList<OutlineCorner> outline;
    for (const Point &point : points)
        outline.append(OutlineCorner{point, 0});
    return outline;
}

} // namespace

BoardEditor::BoardEditor(Document *document, CanvasView *canvas, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_canvas(canvas)
    , m_valueEdit(canvas)
{
    connect(&m_valueEdit, &InPlaceEdit::accepted, this, &BoardEditor::valueTyped);
    // Undo, Redo or a project opened: what was dragged or lit may be gone.
    connect(m_document, &Document::designChanged, this, [this] {
        m_valueEdit.close(false);
        dropDrag();
        emit changed();
    });
}

QString BoardEditor::actionName(Action action)
{
    switch (action) {
    case Action::DrawOutline:
        return tr("Draw board outline");
    case Action::CutOut:
        return tr("Cut out of the board");
    case Action::RectangleBoard:
        return tr("Rectangular board");
    case Action::MoveCorner:
        return tr("Move board corner");
    case Action::MoveSide:
        return tr("Move board edge");
    case Action::MoveOutline:
        return tr("Move board outline");
    case Action::LockOutline:
        return tr("Lock board outline");
    case Action::UnlockOutline:
        return tr("Unlock board outline");
    case Action::RoundCorner:
        return tr("Round board corner");
    case Action::MoveParts:
        return tr("Move parts");
    case Action::RotateParts:
        return tr("Rotate parts");
    case Action::MovePartText:
        return tr("Move part text");
    case Action::ShowValue:
        return tr("Show value");
    case Action::HideValue:
        return tr("Hide value");
    case Action::LineToPart:
        return tr("Line to part");
    case Action::PlaceHole:
        return tr("Place hole");
    case Action::MoveHoles:
        return tr("Move holes");
    case Action::DeleteHoles:
        return tr("Delete holes");
    case Action::HoleDiameter:
        return tr("Hole diameter");
    case Action::PlatedHoles:
        return tr("Plated");
    case Action::CopperRing:
        return tr("Copper ring");
    case Action::LockHoles:
        return tr("Lock holes");
    case Action::UnlockHoles:
        return tr("Unlock holes");
    case Action::LockParts:
        return lockPartsName(true);
    case Action::UnlockParts:
        return lockPartsName(false);
    }
    return QString();
}

QString BoardEditor::diameterHint()
{
    return tr("Type the diameter in millimetres");
}

void BoardEditor::setActive(bool active)
{
    m_valueEdit.close(true);
    m_active = active;
    m_tool = Tool::Select;
    dropChosen();
    m_outlinePoints.clear();
    m_cutting = false;
    dropDrag();
    emit changed();
}

void BoardEditor::chooseSelectTool()
{
    m_tool = Tool::Select;
    m_outlinePoints.clear();
    m_cutting = false;
    emit changed();
}

void BoardEditor::startDrawingOutline()
{
    if (!m_active || refuseLocked())
        return;
    chooseSelectTool();
    dropChosen();
    m_tool = Tool::Outline;
    // Where the cursor is now, before it moves.
    m_cursor = Point{m_canvas->cursorX(), m_canvas->cursorY()};
    emit changed();
}

void BoardEditor::startCuttingOut()
{
    if (!m_active || refuseLocked())
        return;
    chooseSelectTool();
    dropChosen();
    m_tool = Tool::CutOut;
    // Where the cursor is now, before it moves.
    m_cursor = Point{m_canvas->cursorX(), m_canvas->cursorY()};
    emit changed();
}

void BoardEditor::startPlacingHoles()
{
    if (!m_active)
        return;
    chooseSelectTool();
    dropChosen();
    m_tool = Tool::Hole;
    // Where the cursor is now, before it moves.
    m_cursor = Point{m_canvas->cursorX(), m_canvas->cursorY()};
    emit changed();
}

void BoardEditor::setHoleDrill(Coord drill)
{
    if (drill <= 0)
        return;
    m_holeDrill = drill;
    startPlacingHoles();
}

std::optional<MountingHole> BoardEditor::holeToPlace() const
{
    if (!m_active || m_tool != Tool::Hole)
        return std::nullopt;
    MountingHole hole;
    hole.at = m_cursor;
    hole.drill = m_holeDrill;
    return hole;
}

void BoardEditor::placeHole(const Point &at)
{
    const Board &board = m_document->design().board;
    // A second click of a double click lands on the hole just placed.
    if (std::any_of(board.holes.cbegin(), board.holes.cend(),
                    [&at](const MountingHole &hole) { return hole.at == at; }))
        return;
    Board placed = board;
    const MountingHole hole = newHole(at, m_holeDrill);
    placed.holes.append(hole);
    applyBoard(Action::PlaceHole, placed, {hole.id});
}

void BoardEditor::makeRectangleBoard(Coord width, Coord height)
{
    if (refuseLocked())
        return;
    // Back to choosing: the edges of the new board are dragged next.
    chooseSelectTool();
    if (width > 0 && height > 0)
        apply(Action::RectangleBoard, rectangleOutline(Point(), width, height));
}

QRectF BoardEditor::outlineBounds() const
{
    QPolygonF corners;
    for (const OutlineCorner &corner : outline())
        corners.append(toMm(corner.at));
    return corners.boundingRect();
}

int BoardEditor::boardPage() const
{
    return int(m_document->design().schematic.sheets.size());
}

void BoardEditor::apply(Action action, const QList<OutlineCorner> &edited)
{
    Design design = m_document->design();
    design.board.outline = edited;
    m_document->change(actionName(action), design, boardPage());
}

void BoardEditor::applyBoard(Action action, const Board &board, const QSet<ObjectId> &ids)
{
    Design design = m_document->design();
    design.board = board;
    m_document->change(actionName(action), design, boardPage(), ids);
}

QSet<ObjectId> BoardEditor::chosenPlacements() const
{
    QSet<ObjectId> chosen;
    if (!m_active)
        return chosen;
    for (const Placement &placement : m_document->design().board.placements) {
        if (m_chosen.contains(placement.id))
            chosen.insert(placement.id);
    }
    return chosen;
}

QSet<ObjectId> BoardEditor::chosenHoles() const
{
    QSet<ObjectId> chosen;
    if (!m_active)
        return chosen;
    for (const MountingHole &hole : m_document->design().board.holes) {
        if (m_chosen.contains(hole.id))
            chosen.insert(hole.id);
    }
    return chosen;
}

bool BoardEditor::chosenLocked() const
{
    if (!hasChosen())
        return false;
    if (outlineChosen() && !isOutlineLocked())
        return false;
    const Board &board = m_document->design().board;
    const QSet<ObjectId> placements = chosenPlacements();
    const QSet<ObjectId> holes = chosenHoles();
    return (placements.isEmpty() || arePlacementsLocked(board, placements))
           && (holes.isEmpty() || areHolesLocked(board, holes));
}

void BoardEditor::rotateChosen()
{
    const QSet<ObjectId> ids = chosenPlacements();
    if (ids.isEmpty())
        return;
    if (m_dragging) {
        if (m_dragPlacements && !m_dragWhole) {
            m_dragBase = withPlacementsRotated(m_dragBase, ids, m_dragAnchor);
            m_movedBoard = draggedBoard(m_lastAt);
            emit changed();
        }
        return;
    }
    const Board &board = m_document->design().board;
    if (arePlacementsLocked(board, ids)) {
        emit message(partLockedMessage());
        return;
    }
    const Placement *one = ids.size() == 1 ? findById(board.placements, *ids.cbegin()) : nullptr;
    const Point center =
        one != nullptr
            ? one->at
            : m_canvas->snapped(placementsBounds(board, m_document->design().library, ids).center());
    applyBoard(Action::RotateParts, withPlacementsRotated(board, ids, center), ids);
}

void BoardEditor::deleteChosen()
{
    if (!m_active || m_dragging || refuseHolesLocked())
        return;
    // The locked ones stay, chosen.
    const Board &board = m_document->design().board;
    QSet<ObjectId> ids;
    for (const MountingHole &hole : board.holes) {
        if (m_chosen.contains(hole.id) && !hole.locked)
            ids.insert(hole.id);
    }
    if (ids.isEmpty())
        return;
    m_chosen.subtract(ids);
    // An Undo brings them back chosen.
    applyBoard(Action::DeleteHoles, withoutHoles(board, ids), ids);
    emit changed();
}

QSet<QString> BoardEditor::litNets() const
{
    QSet<QString> nets;
    const QSet<ObjectId> chosen = chosenPlacements();
    for (const Placement &placement : m_document->design().board.placements) {
        if (!chosen.contains(placement.id))
            continue;
        for (const QString &net : placement.padNets)
            nets.insert(net);
    }
    return nets;
}

QList<QLineF> BoardEditor::textGuides() const
{
    QList<QLineF> guides;
    const Board board = shownBoard();
    const QSet<ObjectId> chosen = chosenPlacements();
    const PlacementText alone = chosenText();
    for (const Placement &placement : board.placements) {
        for (const ComponentText text : {ComponentText::Designator, ComponentText::Value}) {
            const bool wanted = chosen.contains(placement.id)
                                || (alone.owner == placement.id && alone.text == text);
            if (wanted && !placementTextShown(placement, text).isEmpty())
                guides.append(QLineF(toMm(placement.at),
                                     toMm(placementTextPoint(placement, text))));
        }
    }
    return guides;
}

Board BoardEditor::shownBoard() const
{
    if (m_movedBoard)
        return *m_movedBoard;
    Board board = m_document->design().board;
    if (m_preview)
        board.outline = *m_preview;
    return board;
}

ObjectId BoardEditor::holeUnder(const QPointF &at) const
{
    return holeAt(m_document->design().board, at,
                  m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
}

bool BoardEditor::pressChoosable(const QPointF &at, bool adding)
{
    const Design &design = m_document->design();
    const PlacementText text = placementTextAt(design.board, at);
    if (!text.isNull() && !adding) {
        m_chosen.clear();
        m_outlineChosen = false;
        m_chosenText = text;
        m_dragText = true;
        m_dragAnchor = placementTextPoint(design.board, text);
        return true;
    }
    // Holes are drawn over the pads.
    const ObjectId hole = holeUnder(at);
    const ObjectId hit = hole.isNull() ? placementAt(design.board, design.library, at) : hole;
    if (hit.isNull())
        return false;
    m_chosenText = PlacementText();
    if (adding) {
        if (m_chosen.contains(hit))
            m_chosen.remove(hit);
        else
            m_chosen.insert(hit);
        return true;
    }
    if (!m_chosen.contains(hit)) {
        m_chosen = {hit};
        m_outlineChosen = false;
    }
    m_dragPlacements = true;
    m_dragAnchor = hole.isNull() ? findById(design.board.placements, hit)->at
                                 : findById(design.board.holes, hit)->at;
    return true;
}

Board BoardEditor::draggedBoard(const QPointF &at) const
{
    const Board &board = m_dragBase;
    // What was taken lands on the grid, wherever it stood before.
    const Point delta = m_canvas->snapped(toMm(m_dragAnchor) + (at - m_pressAt)) - m_dragAnchor;
    if (m_dragText)
        return withPlacementTextMoved(board, m_chosenText, delta);
    Board moved = withPlacementsMoved(board, chosenPlacements(), delta);
    moved = withHolesMoved(moved, chosenHoles(), delta);
    if (outlineChosen() && !isOutlineLocked())
        moved.outline = outlineMoved(board.outline, delta);
    return moved;
}

void BoardEditor::dropChosen()
{
    m_outlineChosen = false;
    m_chosen.clear();
    m_chosenText = PlacementText();
}

void BoardEditor::chooseOutline()
{
    chooseSelectTool();
    dropChosen();
    m_outlineChosen = !outline().isEmpty();
    emit changed();
}

void BoardEditor::press(const QPointF &at, bool adding)
{
    if (!m_active)
        return;
    m_valueEdit.close(true);
    switch (m_tool) {
    case Tool::Outline:
        outlineClick(m_canvas->snapped(at));
        return;
    case Tool::Hole:
        placeHole(m_canvas->snapped(at));
        return;
    case Tool::CutOut:
        m_cutting = true;
        m_cutFrom = m_canvas->snapped(at);
        m_cutTo = m_cutFrom;
        emit changed();
        return;
    case Tool::Select:
        break;
    }
    dropDrag();
    m_lockTold = false;
    m_pressed = true;
    m_pressAt = at;
    m_lastAt = at;
    m_pressGrid = m_canvas->snapped(at);
    m_dragBase = m_document->design().board;
    m_dragCorner = outlineCornerAt(outline(), at, m_canvas->pixelsToMm(CanvasView::END_PIXELS));
    // A placement, its text or a hole lies over the sides of the outline.
    if (m_dragCorner < 0 && pressChoosable(at, adding)) {
        emit changed();
        return;
    }
    if (m_dragCorner < 0)
        m_dragSide = outlineSideAt(outline(), at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
    if (m_dragSide >= 0) {
        const int count = int(outline().size());
        const QPointF start = toMm(outline().at(m_dragSide).at);
        const QPointF end = toMm(outline().at((m_dragSide + 1) % count).at);
        m_dragSideEnd = QLineF(at, end).length() < QLineF(at, start).length();
    }
    const bool onOutline = m_dragCorner >= 0 || m_dragSide >= 0;
    m_dragWhole = onOutline && outlineChosen();
    m_chosenText = PlacementText();
    if (m_dragWhole) {
        // The outline chosen whole goes with the placements chosen.
        m_dragPlacements = true;
        m_dragAnchor = m_pressGrid;
    } else if (!adding) {
        m_outlineChosen = false;
        m_chosen.clear();
    }
    // On the empty board a drag draws a frame.
    m_pressEmpty = !onOutline;
    m_frameAdding = adding;
    setLit(m_dragCorner, m_dragSide);
    emit changed();
}

void BoardEditor::move(const QPointF &at)
{
    if (!m_active)
        return;
    if (m_cutting) {
        const Point to = m_canvas->snapped(at);
        if (to != m_cutTo) {
            m_cutTo = to;
            emit changed();
        }
        return;
    }
    const bool takes = m_dragPlacements || m_dragText;
    if (!m_pressed || (!takes && !m_pressEmpty && m_dragCorner < 0 && m_dragSide < 0))
        return;
    if (!m_dragging) {
        if (QLineF(at, m_pressAt).length()
            < m_canvas->pixelsToMm(QApplication::startDragDistance()))
            return;
        if (m_pressEmpty) {
            m_dragging = true;
            m_framing = true;
        } else if (takes) {
            // Nothing taken may move: say why, once.
            const bool outlineGoes = outlineChosen() && !isOutlineLocked();
            const QSet<ObjectId> placements = chosenPlacements();
            const bool placementsGo =
                !placements.isEmpty()
                && !arePlacementsLocked(m_document->design().board, placements);
            const QSet<ObjectId> holes = chosenHoles();
            const bool holesGo =
                !holes.isEmpty() && !areHolesLocked(m_document->design().board, holes);
            if (!m_dragText && !outlineGoes && !placementsGo && !holesGo) {
                if (!m_lockTold) {
                    if (!placements.isEmpty())
                        emit message(partLockedMessage());
                    else if (!refuseHolesLocked())
                        refuseLocked();
                }
                m_lockTold = true;
                return;
            }
            m_dragging = true;
        } else if (isOutlineLocked()) {
            if (!m_lockTold)
                refuseLocked();
            m_lockTold = true;
            return;
        }
        if (!takes && !m_pressEmpty) {
            m_dragging = true;
            m_preview = outline();
        }
    }
    if (m_framing) {
        m_frame = QRectF(m_pressAt, at).normalized();
        emit changed();
        return;
    }
    if (takes) {
        m_lastAt = at;
        const Board moved = draggedBoard(at);
        if (!m_movedBoard || *m_movedBoard != moved) {
            m_movedBoard = moved;
            emit changed();
        }
        return;
    }
    // Where the outline would go wrong, it stays as it was last right.
    const Point cursor = m_canvas->snapped(at);
    std::optional<QList<OutlineCorner>> moved;
    if (m_dragCorner >= 0)
        moved = withOutlineCornerMoved(outline(), m_dragCorner, cursor);
    else
        moved = withOutlineSideMoved(outline(), m_dragSide, cursor - m_pressGrid);
    if (moved && *moved != *m_preview) {
        m_preview = moved;
        emit changed();
    }
}

void BoardEditor::release(const QPointF &at)
{
    if (!m_active)
        return;
    if (m_cutting) {
        m_cutting = false;
        cutOut(m_cutFrom, m_canvas->snapped(at));
        emit changed();
        return;
    }
    if (m_framing) {
        const Design &design = m_document->design();
        const QSet<ObjectId> inside = placementsInside(design.board, design.library, m_frame)
                                          .unite(holesInside(design.board, m_frame));
        if (m_frameAdding)
            m_chosen.unite(inside);
        else
            m_chosen = inside;
        dropDrag();
        emit changed();
        return;
    }
    if (m_movedBoard) {
        const Board moved = *m_movedBoard;
        const QSet<ObjectId> ids = chosenPlacements();
        const QSet<ObjectId> holes = chosenHoles();
        const Action action = m_dragText         ? Action::MovePartText
                              : !ids.isEmpty()   ? Action::MoveParts
                              : !holes.isEmpty() ? Action::MoveHoles
                                                 : Action::MoveOutline;
        const QSet<ObjectId> touched =
            m_dragText ? QSet<ObjectId>{m_chosenText.owner} : QSet<ObjectId>(ids).unite(holes);
        dropDrag();
        if (moved != m_document->design().board)
            applyBoard(action, moved, touched);
        emit changed();
        return;
    }
    const std::optional<QList<OutlineCorner>> moved = m_dragging ? m_preview : std::nullopt;
    const Action action = m_dragCorner >= 0 ? Action::MoveCorner : Action::MoveSide;
    dropDrag();
    if (moved && *moved != outline())
        apply(action, *moved);
    emit changed();
}

void BoardEditor::doubleClick(const QPointF &at)
{
    if (!m_active)
        return;
    if (m_tool != Tool::Select) {
        press(at);
        return;
    }
    const int corner =
        outlineCornerAt(outline(), at, m_canvas->pixelsToMm(CanvasView::END_PIXELS));
    if (corner >= 0) {
        if (!refuseLocked())
            editRadius(corner);
        return;
    }
    if (outlineSideAt(outline(), at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS)) >= 0)
        chooseOutline();
}

void BoardEditor::chooseAll()
{
    if (!m_active)
        return;
    chooseSelectTool();
    dropChosen();
    m_outlineChosen = !outline().isEmpty();
    for (const Placement &placement : m_document->design().board.placements)
        m_chosen.insert(placement.id);
    for (const MountingHole &hole : m_document->design().board.holes)
        m_chosen.insert(hole.id);
    emit changed();
}

BoardEditor::MenuTarget BoardEditor::chooseForMenu(const QPointF &at)
{
    m_menuCorner = -1;
    m_menuHole = ObjectId();
    if (!m_active || m_tool != Tool::Select)
        return MenuTarget::None;
    const Design &design = m_document->design();
    // A text stands for its placement; holes are drawn over the pads.
    const PlacementText text = placementTextAt(design.board, at);
    const ObjectId hole = text.isNull() ? holeUnder(at) : ObjectId();
    const ObjectId hit = !text.isNull()   ? text.owner
                         : !hole.isNull() ? hole
                                          : placementAt(design.board, design.library, at);
    if (!hit.isNull()) {
        if (!m_chosen.contains(hit)) {
            dropChosen();
            m_chosen = {hit};
        }
        m_menuHole = hole;
        emit changed();
        return hole.isNull() ? MenuTarget::Placement : MenuTarget::Hole;
    }
    m_menuCorner = outlineCornerAt(outline(), at, m_canvas->pixelsToMm(CanvasView::END_PIXELS));
    const bool side =
        m_menuCorner < 0
        && outlineSideAt(outline(), at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS)) >= 0;
    if (m_menuCorner < 0 && !side)
        return MenuTarget::None;
    if (!outlineChosen())
        chooseOutline();
    return m_menuCorner >= 0 ? MenuTarget::Corner : MenuTarget::Side;
}

Coord BoardEditor::chosenHolesDrill() const
{
    const QSet<ObjectId> ids = chosenHoles();
    Coord drill = 0;
    for (const MountingHole &hole : m_document->design().board.holes) {
        if (!ids.contains(hole.id))
            continue;
        if (drill != 0 && hole.drill != drill)
            return 0;
        drill = hole.drill;
    }
    return drill;
}

void BoardEditor::setChosenHolesDrill(Coord drill)
{
    const QSet<ObjectId> ids = chosenHoles();
    const Board &board = m_document->design().board;
    if (refuseHolesLocked())
        return;
    const Board drilled = withHolesDrill(board, ids, drill);
    if (drill > 0 && drilled != board)
        applyBoard(Action::HoleDiameter, drilled, ids);
}

bool BoardEditor::chosenHolesPlated() const
{
    return areHolesPlated(m_document->design().board, chosenHoles());
}

bool BoardEditor::chosenHolesLocked() const
{
    return areHolesLocked(m_document->design().board, chosenHoles());
}

void BoardEditor::setChosenHolesPlated(bool plated)
{
    const QSet<ObjectId> ids = chosenHoles();
    const Board &board = m_document->design().board;
    if (refuseHolesLocked())
        return;
    const Board changedBoard = withHolesPlated(board, ids, plated);
    if (changedBoard != board)
        applyBoard(Action::PlatedHoles, changedBoard, ids);
}

void BoardEditor::editHoleValue(Typed typed)
{
    // A line still open keeps what it holds for its own holes.
    m_valueEdit.close(true);
    const MountingHole *hole = findById(m_document->design().board.holes, m_menuHole);
    if (hole == nullptr || chosenHoles().isEmpty() || refuseHolesLocked())
        return;
    m_typed = typed;
    const Coord value = typed == Typed::Ring ? hole->ring : hole->drill;
    m_valueEdit.open(formatMmShort(value), QRectF(toMm(hole->at), QSizeF(0, 0)));
}

void BoardEditor::holeValueTyped(Typed typed, const QString &text)
{
    Coord value = 0;
    if (!parseMm(text, value) || value <= 0) {
        emit message(typed == Typed::Ring ? tr("Type the ring width in millimetres")
                                          : diameterHint());
        return;
    }
    if (typed == Typed::Drill) {
        setChosenHolesDrill(value);
        return;
    }
    const QSet<ObjectId> ids = chosenHoles();
    const Board &board = m_document->design().board;
    const Board ringed = withHolesRing(board, ids, value);
    if (ringed != board)
        applyBoard(Action::CopperRing, ringed, ids);
}

void BoardEditor::valueTyped(const QString &text)
{
    const Typed typed = m_typed;
    m_typed = Typed::None;
    switch (typed) {
    case Typed::Radius:
        radiusTyped(text);
        break;
    case Typed::Drill:
    case Typed::Ring:
        holeValueTyped(typed, text);
        break;
    case Typed::None:
        break;
    }
}

bool BoardEditor::chosenValuesShown() const
{
    const QSet<ObjectId> ids = chosenPlacements();
    const QList<Placement> &placements = m_document->design().board.placements;
    return !ids.isEmpty()
           && std::all_of(placements.cbegin(), placements.cend(), [&ids](const Placement &p) {
                  return !ids.contains(p.id) || p.showValue;
              });
}

void BoardEditor::setChosenValuesShown(bool shown)
{
    const QSet<ObjectId> ids = chosenPlacements();
    Board board = m_document->design().board;
    for (Placement &placement : board.placements) {
        if (ids.contains(placement.id))
            placement.showValue = shown;
    }
    if (board != m_document->design().board)
        applyBoard(shown ? Action::ShowValue : Action::HideValue, board, ids);
}

bool BoardEditor::chosenLined() const
{
    const QSet<ObjectId> ids = chosenPlacements();
    const QList<Placement> &placements = m_document->design().board.placements;
    return !ids.isEmpty()
           && std::all_of(placements.cbegin(), placements.cend(), [&ids](const Placement &p) {
                  return !ids.contains(p.id) || (p.refLine && p.valueLine);
              });
}

void BoardEditor::setChosenLined(bool lined)
{
    const QSet<ObjectId> ids = chosenPlacements();
    Board board = m_document->design().board;
    for (Placement &placement : board.placements) {
        if (ids.contains(placement.id)) {
            placement.refLine = lined;
            placement.valueLine = lined;
        }
    }
    if (board != m_document->design().board)
        applyBoard(Action::LineToPart, board, ids);
}

void BoardEditor::setChosen(const QSet<ObjectId> &ids)
{
    dropChosen();
    m_chosen = ids;
    emit changed();
}

void BoardEditor::setChosenLocked(bool locked)
{
    if (!hasChosen() || locked == chosenLocked())
        return;
    Design design = m_document->design();
    const QSet<ObjectId> placements = chosenPlacements();
    const QSet<ObjectId> holes = chosenHoles();
    if (outlineChosen())
        design.board.outlineLocked = locked;
    design.board = withPlacementsLocked(design.board, placements, locked);
    design.board = withHolesLocked(design.board, holes, locked);
    const Action action = !placements.isEmpty() ? (locked ? Action::LockParts : Action::UnlockParts)
                          : !holes.isEmpty()    ? (locked ? Action::LockHoles : Action::UnlockHoles)
                                                : (locked ? Action::LockOutline
                                                          : Action::UnlockOutline);
    m_document->change(actionName(action), design, boardPage(),
                       QSet<ObjectId>(placements).unite(holes));
}

bool BoardEditor::refuseHolesLocked()
{
    if (!chosenHolesLocked())
        return false;
    emit message(tr("The hole is locked"));
    return true;
}

bool BoardEditor::refuseLocked()
{
    if (!isOutlineLocked())
        return false;
    emit message(tr("The board outline is locked"));
    return true;
}

void BoardEditor::hover(const Point &at)
{
    if (m_cursor == at)
        return;
    m_cursor = at;
    // The side being drawn, the hole to place and the coordinates follow it.
    if (m_active && m_tool != Tool::Select)
        emit changed();
}

void BoardEditor::hoverMouse(const QPointF &at)
{
    if (!m_active || m_tool != Tool::Select || m_pressed)
        return;
    // A locked outline is not offered for dragging.
    if (isOutlineLocked()) {
        setLit(-1, -1);
        return;
    }
    const double sideReach = m_canvas->pixelsToMm(CanvasView::HIT_PIXELS);
    const int corner =
        outlineCornerAt(outline(), at, m_canvas->pixelsToMm(CanvasView::END_PIXELS));
    setLit(corner, corner >= 0 ? -1 : outlineSideAt(outline(), at, sideReach));
}

void BoardEditor::cancel()
{
    if (!m_active)
        return;
    if (m_dragging) {
        dropDrag();
    } else if (!m_outlinePoints.isEmpty()) {
        m_outlinePoints.clear();
    } else if (m_cutting) {
        m_cutting = false;
    } else if (m_tool == Tool::Select
               && (m_outlineChosen || !m_chosen.isEmpty() || !m_chosenText.isNull())) {
        dropChosen();
    } else {
        chooseSelectTool();
        setLit(-1, -1);
    }
    emit changed();
}

QPolygonF BoardEditor::drawnOutline() const
{
    QPolygonF line;
    if (m_outlinePoints.isEmpty())
        return line;
    for (const Point &point : m_outlinePoints)
        line.append(toMm(point));
    line.append(toMm(m_cursor));
    return line;
}

std::optional<Point> BoardEditor::drawnPoint() const
{
    if (!m_active)
        return std::nullopt;
    if (m_cutting)
        return m_cutTo;
    // A corner dragged, or the corner of a dragged side nearer to where it
    // was taken.
    const QList<OutlineCorner> *dragged =
        m_preview ? &*m_preview : (m_movedBoard && m_dragWhole ? &m_movedBoard->outline : nullptr);
    if (m_dragging && dragged != nullptr) {
        const int count = int(dragged->size());
        int corner = m_dragCorner;
        if (corner < 0 && m_dragSide >= 0 && m_dragSide < count)
            corner = m_dragSideEnd ? (m_dragSide + 1) % count : m_dragSide;
        if (corner >= 0 && corner < count)
            return dragged->at(corner).at;
    }
    if (m_tool != Tool::Select)
        return m_cursor;
    return std::nullopt;
}

QRectF BoardEditor::cutRect() const
{
    if (!m_cutting)
        return QRectF();
    return QRectF(toMm(m_cutFrom), toMm(m_cutTo)).normalized();
}

void BoardEditor::outlineClick(const Point &at)
{
    if (m_outlinePoints.isEmpty()) {
        m_outlinePoints.append(at);
        emit changed();
        return;
    }
    // Back on the first corner too early: three corners make the least outline.
    const bool early = at == m_outlinePoints.first() && m_outlinePoints.size() < 3;
    if (at == m_outlinePoints.last() || early)
        return;
    if (at != m_outlinePoints.first()) {
        m_outlinePoints.append(at);
        emit changed();
        return;
    }
    // Closed: the new outline takes the place of the old one.
    const QList<OutlineCorner> drawn = sharpOutline(m_outlinePoints);
    if (outlineProblem(drawn) != OutlineProblem::None) {
        emit message(tr("The outline crosses itself"));
        return;
    }
    m_outlinePoints.clear();
    m_tool = Tool::Select;
    apply(Action::DrawOutline, drawn);
    emit changed();
}

void BoardEditor::cutOut(const Point &a, const Point &b)
{
    if (a == b)
        return;
    const OutlineCut cut = withOutlineCut(outline(), a, b, m_canvas->gridStep());
    switch (cut.problem) {
    case OutlineCutProblem::None:
        apply(Action::CutOut, cut.outline);
        break;
    case OutlineCutProblem::Misses:
        emit message(tr("The cut-out takes nothing off the board"));
        break;
    case OutlineCutProblem::Inside:
        emit message(tr("The cut-out must reach the edge of the board"));
        break;
    case OutlineCutProblem::Splits:
        emit message(tr("The cut-out would split the board"));
        break;
    }
}

void BoardEditor::editRadius(int corner)
{
    // A line still open keeps what it holds for its own corner.
    m_valueEdit.close(true);
    if (corner >= outline().size())
        return;
    m_typed = Typed::Radius;
    m_radiusCorner = corner;
    const Point at = outline().at(corner).at;
    m_valueEdit.open(formatMmShort(outline().at(corner).radius), QRectF(toMm(at), QSizeF(0, 0)));
}

void BoardEditor::radiusTyped(const QString &text)
{
    const int corner = m_radiusCorner;
    m_radiusCorner = -1;
    if (corner < 0 || corner >= outline().size())
        return;
    Coord radius = 0;
    if (!parseMm(text, radius) || radius < 0) {
        emit message(tr("Type the radius in millimetres"));
        return;
    }
    QList<OutlineCorner> edited = outline();
    edited[corner].radius = radius;
    if (edited != outline())
        apply(Action::RoundCorner, edited);
}

void BoardEditor::dropDrag()
{
    m_pressed = false;
    m_dragging = false;
    m_dragCorner = -1;
    m_dragSide = -1;
    m_dragWhole = false;
    m_preview.reset();
    m_dragPlacements = false;
    m_dragText = false;
    m_movedBoard.reset();
    m_framing = false;
    m_pressEmpty = false;
    m_litCorner = -1;
    m_litSide = -1;
}

void BoardEditor::setLit(int corner, int side)
{
    if (corner == m_litCorner && side == m_litSide)
        return;
    m_litCorner = corner;
    m_litSide = side;
    emit changed();
}

} // namespace ppcb
