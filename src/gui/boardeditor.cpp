#include "boardeditor.h"

#include "canvasview.h"
#include "format.h"
#include "outline.h"
#include "paint.h"
#include "ratsnest.h"

#include <QApplication>
#include <QCursor>
#include <QGuiApplication>
#include <QLineF>
#include <QToolTip>

#include <algorithm>
#include <utility>

namespace ppcb {

namespace {

// How many via steps the side is followed back through (sideFollowed()).
const int VIA_STEPS_KEPT = 100;

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
    connect(m_document, &Document::designChanged, this, &BoardEditor::sideFollowed);
    connect(m_document, &Document::designChanged, this, &BoardEditor::laidChanged);
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(CanvasView::HOVER_DELAY_MS);
    connect(&m_hoverTimer, &QTimer::timeout, this, [this] {
        // Something else may have begun since.
        if (m_active && m_tool == Tool::Select && !m_pressed)
            setHoveredNet(m_pendingNet);
    });
    // Undo, Redo or a project opened: what was dragged or lit may be gone.
    connect(m_document, &Document::designChanged, this, [this] {
        m_valueEdit.close(false);
        setHoveredNet(QString());
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
    case Action::MovePartsOff:
        return tr("Move parts off the board");
    case Action::HoleDiameter:
        return tr("Hole diameter");
    case Action::PlatedHoles:
        return tr("Plated");
    case Action::CopperRing:
        return tr("Copper pad");
    case Action::LockHoles:
        return tr("Lock holes");
    case Action::UnlockHoles:
        return tr("Unlock holes");
    case Action::PadSides:
        return tr("Pad side");
    case Action::PadNet:
        return tr("Pad net");
    case Action::LayTrack:
        return tr("Lay track");
    case Action::ViaAndOtherSide:
        return tr("Via to the other side");
    case Action::DeleteTracks:
        return tr("Delete tracks");
    case Action::DeleteAllTracks:
        return tr("Delete all tracks");
    case Action::TrackWidth:
        return tr("Track width");
    case Action::TrackSide:
        return tr("Track layer");
    case Action::LockParts:
        return lockPartsName(true);
    case Action::UnlockParts:
        return lockPartsName(false);
    }
    return QString();
}

QString BoardEditor::sideName(BoardSide side)
{
    return side == BoardSide::Top ? tr("Top layer") : tr("Bottom layer");
}

void BoardEditor::setTrackSide(BoardSide side)
{
    if (side != trackSide())
        viaAndOtherSide();
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
    m_trackFrom.reset();
    m_hoverTimer.stop();
    m_pendingNet.clear();
    setHoveredNet(QString());
    dropChosen();
    m_outlinePoints.clear();
    m_cutting = false;
    dropDrag();
    emit changed();
}

void BoardEditor::chooseSelectTool()
{
    m_tool = Tool::Select;
    m_trackFrom.reset();
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

void BoardEditor::startLayingTracks()
{
    if (!m_active)
        return;
    chooseSelectTool();
    dropChosen();
    m_tool = Tool::Track;
    m_mouseAt = toMm(Point{m_canvas->cursorX(), m_canvas->cursorY()});
    m_cursor = Point{m_canvas->cursorX(), m_canvas->cursorY()};
    emit message(sideName(trackSide()));
    emit changed();
}

BoardSide BoardEditor::trackSide() const
{
    switch (m_document->design().board.copper) {
    case CopperSides::Top:
        return BoardSide::Top;
    case CopperSides::Bottom:
        return BoardSide::Bottom;
    case CopperSides::Both:
        break;
    }
    return m_trackSide;
}

void BoardEditor::swapTrackCorner()
{
    if (!m_active || m_tool != Tool::Track)
        return;
    m_diagonalFirst = !m_diagonalFirst;
    emit changed();
}

void BoardEditor::viaAndOtherSide()
{
    if (!m_active)
        return;
    if (m_document->design().board.copper != CopperSides::Both) {
        emit message(tr("The board has copper on one side"));
        return;
    }
    const BoardSide other = trackSide() == BoardSide::Top ? BoardSide::Bottom : BoardSide::Top;
    if (m_tool == Tool::Track && m_trackFrom) {
        const NetClass *netClass = trackClass(m_document->design(), m_trackNet);
        const Rules &rules = m_document->design().project.rules;
        Via via;
        via.id = newObjectId();
        via.at = trackTarget(m_mouseAt);
        via.net = m_trackNet;
        via.drill = netClass != nullptr ? netClass->viaDrill : rules.viaDrill;
        via.size = netClass != nullptr ? netClass->viaSize : rules.viaSize;
        const Design before = m_document->design();
        layTrack(via.at, Action::ViaAndOtherSide, via);
        m_trackFrom = via.at;
        m_viaSteps.append(ViaStep{before, m_document->design(), trackSide(), other});
        if (m_viaSteps.size() > VIA_STEPS_KEPT)
            m_viaSteps.removeFirst();
    }
    m_trackSide = other;
    if (m_tool == Tool::Track && m_trackFrom)
        noteLaid();
    emit message(sideName(other));
    emit changed();
}

Point BoardEditor::trackTarget(const QPointF &at) const
{
    const Design &design = m_document->design();
    const std::optional<CopperPoint> copper = copperAt(
        design.board, design.library, at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
    if (copper && copper->on(trackSide()))
        return copper->at;
    return m_canvas->snapped(at);
}

QList<Point> BoardEditor::trackPieces(const Point &target) const
{
    if (!m_trackFrom)
        return {};
    // Shift: one piece at any angle.
    if (QGuiApplication::queryKeyboardModifiers() & Qt::ShiftModifier) {
        if (target == *m_trackFrom)
            return {target};
        return {*m_trackFrom, target};
    }
    return trackCorners(*m_trackFrom, target, m_diagonalFirst);
}

QList<Track> BoardEditor::trackPreview() const
{
    QList<Track> preview;
    if (!m_active || m_tool != Tool::Track || !m_trackFrom)
        return preview;
    const QList<Point> points = trackPieces(trackTarget(m_mouseAt));
    for (int i = 1; i < points.size(); ++i) {
        Track track;
        track.side = trackSide();
        track.net = m_trackNet;
        track.width = m_trackWidth;
        track.from = points.at(i - 1);
        track.to = points.at(i);
        preview.append(track);
    }
    return preview;
}

void BoardEditor::layTrack(const Point &target, Action action, const std::optional<Via> &via)
{
    Board board = m_document->design().board;
    QSet<ObjectId> ids;
    const QList<Point> points = trackPieces(target);
    for (int i = 1; i < points.size(); ++i) {
        Track track;
        track.id = newObjectId();
        track.side = trackSide();
        track.net = m_trackNet;
        track.width = m_trackWidth;
        track.from = points.at(i - 1);
        track.to = points.at(i);
        board.tracks.append(track);
        ids.insert(track.id);
    }
    if (via) {
        board.vias.append(*via);
        ids.insert(via->id);
    }
    if (!ids.isEmpty()) {
        m_layingStep = true;
        applyBoard(action, board, ids);
        m_layingStep = false;
    }
}

void BoardEditor::noteLaid()
{
    const Design &design = m_document->design();
    if (m_trackFrom && !m_laid.isEmpty() && design != m_laid.last().design)
        m_laid.append(LaidPoint{*m_trackFrom, trackSide(), design});
}

void BoardEditor::sideFollowed()
{
    if (m_layingStep)
        return;
    const Design &design = m_document->design();
    for (auto step = m_viaSteps.crbegin(); step != m_viaSteps.crend(); ++step) {
        if (design == step->before || design == step->after) {
            m_trackSide = design == step->before ? step->sideBefore : step->sideAfter;
            emit changed();
            return;
        }
    }
}

void BoardEditor::laidChanged()
{
    if (m_layingStep || !m_trackFrom || m_laid.isEmpty())
        return;
    const Design &design = m_document->design();
    while (!m_laid.isEmpty() && m_laid.last().design != design)
        m_laid.removeLast();
    if (m_laid.isEmpty()) {
        m_trackFrom.reset();
    } else {
        m_trackFrom = m_laid.last().at;
        m_trackSide = m_laid.last().side;
    }
    emit changed();
}

void BoardEditor::trackClick(const QPointF &at)
{
    m_mouseAt = at;
    const Design &design = m_document->design();
    const std::optional<CopperPoint> copper = copperAt(
        design.board, design.library, at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
    if (!m_trackFrom) {
        if (!copper) {
            emit message(tr("Start a track on a pad or a track"));
            return;
        }
        // Copper on the other side only: the track starts there.
        if (!copper->on(trackSide()) && design.board.copper == CopperSides::Both)
            m_trackSide = trackSide() == BoardSide::Top ? BoardSide::Bottom : BoardSide::Top;
        m_trackFrom = copper->at;
        m_laid = {LaidPoint{copper->at, trackSide(), design}};
        m_trackNet = copper->net;
        const NetClass *netClass = trackClass(design, m_trackNet);
        m_trackWidth =
            netClass != nullptr ? netClass->trackWidth : design.project.rules.defaultTrackWidth;
        emit changed();
        return;
    }
    const bool ending = copper && copper->on(trackSide());
    if (ending && !copper->net.isEmpty() && !m_trackNet.isEmpty() && copper->net != m_trackNet) {
        emit message(tr("The copper there is on another net"));
        return;
    }
    const Point target = trackTarget(at);
    if (target == *m_trackFrom) {
        // Back on the copper it started from: nothing laid, the track ends.
        if (ending)
            m_trackFrom.reset();
        emit changed();
        return;
    }
    layTrack(target, Action::LayTrack, std::nullopt);
    // On copper of its own side the track ends; else it goes on from there.
    if (ending) {
        m_trackFrom.reset();
    } else {
        m_trackFrom = target;
        noteLaid();
    }
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

QSet<ObjectId> BoardEditor::chosenTracks() const
{
    QSet<ObjectId> chosen;
    if (!m_active)
        return chosen;
    const Board &board = m_document->design().board;
    for (const Track &track : board.tracks) {
        if (m_chosen.contains(track.id))
            chosen.insert(track.id);
    }
    for (const Via &via : board.vias) {
        if (m_chosen.contains(via.id))
            chosen.insert(via.id);
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
            m_dragBase = withTracksFollowingPads(
                m_dragBase, withPlacementsRotated(m_dragBase, ids, m_dragAnchor),
                m_document->design().library);
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
    applyBoard(Action::RotateParts,
               withTracksFollowingPads(board, withPlacementsRotated(board, ids, center),
                                       m_document->design().library),
               ids);
}

void BoardEditor::deleteChosen()
{
    if (!m_active || m_dragging)
        return;
    // The locked ones stay, chosen.
    const Design &design = m_document->design();
    const Board &board = design.board;
    QSet<ObjectId> holes;
    for (const MountingHole &hole : board.holes) {
        if (m_chosen.contains(hole.id) && !hole.locked)
            holes.insert(hole.id);
    }
    QSet<ObjectId> parts;
    for (const Placement &placement : board.placements) {
        if (m_chosen.contains(placement.id) && !placement.locked)
            parts.insert(placement.id);
    }
    // The tracks of the parts and holes taken off go with them.
    const QSet<ObjectId> tracks = chosenTracks().unite(
        tracksOfPads(board, design.library, QSet<ObjectId>(parts).unite(holes)));
    if (holes.isEmpty() && parts.isEmpty() && tracks.isEmpty()) {
        if (!chosenPlacements().isEmpty())
            emit message(partLockedMessage());
        else
            refuseHolesLocked();
        return;
    }
    // The parts stay chosen where they go; an Undo brings all back chosen.
    m_chosen.subtract(holes);
    m_chosen.subtract(tracks);
    const Board result =
        withPlacementsPacked(withoutTracks(withoutHoles(board, holes), tracks), design.library,
                             parts, design.project.boardGridStep);
    const Action action = !parts.isEmpty()   ? Action::MovePartsOff
                          : !holes.isEmpty() ? Action::DeleteHoles
                                             : Action::DeleteTracks;
    applyBoard(action, result, QSet<ObjectId>(holes).unite(parts).unite(tracks));
    emit changed();
}

void BoardEditor::deleteAllTracks()
{
    if (!m_active || m_dragging)
        return;
    const Board &board = m_document->design().board;
    if (board.tracks.isEmpty() && board.vias.isEmpty())
        return;
    m_chosen.subtract(chosenTracks());
    applyBoard(Action::DeleteAllTracks, withoutAnyTracks(board), {});
    emit changed();
}

QSet<QString> BoardEditor::litNets() const
{
    QSet<QString> nets;
    // The pads of the track's net while it is laid, and the net the mouse
    // stays on, unless tracks or vias are chosen: they stand out alone then,
    // the net named in the tip only.
    if (m_active && m_tool == Tool::Track && m_trackFrom && !m_trackNet.isEmpty())
        nets.insert(m_trackNet);
    if (!m_hoveredNet.isEmpty() && chosenTracks().isEmpty())
        nets.insert(m_hoveredNet);
    const QSet<ObjectId> chosen = chosenPlacements();
    for (const Placement &placement : m_document->design().board.placements) {
        if (!chosen.contains(placement.id))
            continue;
        for (const QString &net : placement.padNets)
            nets.insert(net);
    }
    return nets;
}

QString BoardEditor::joinedChosenNet() const
{
    const QSet<ObjectId> chosen = chosenTracks();
    if (chosen.isEmpty())
        return QString();
    const Design &design = m_document->design();
    const ObjectId any = *chosen.cbegin();
    if (joinedCopper(design.board, design.library, any) != chosen)
        return QString();
    if (const Track *track = findById(design.board.tracks, any))
        return track->net;
    if (const Via *via = findById(design.board.vias, any))
        return via->net;
    return QString();
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

void BoardEditor::setSilkscreenShown(bool shown)
{
    m_silkscreenShown = shown;
    // What is chosen of the parts is not seen any more.
    if (!shown) {
        m_chosenText = PlacementText();
        m_chosen.subtract(chosenPlacements());
        emit changed();
    }
}

void BoardEditor::setTracksShown(bool shown)
{
    m_tracksShown = shown;
    if (!shown) {
        if (m_tool == Tool::Track)
            chooseSelectTool();
        m_chosen.subtract(chosenTracks());
        m_hoverTimer.stop();
        m_pendingNet.clear();
        setHoveredNet(QString());
    }
    emit changed();
}

PlacementText BoardEditor::textUnder(const QPointF &at) const
{
    if (!m_silkscreenShown)
        return PlacementText();
    return placementTextAt(m_document->design().board, at);
}

ObjectId BoardEditor::placementUnder(const QPointF &at) const
{
    const Design &design = m_document->design();
    if (!m_silkscreenShown)
        return ObjectId();
    return placementAt(design.board, design.library, at);
}

ObjectId BoardEditor::holeUnder(const QPointF &at) const
{
    return holeAt(m_document->design().board, at,
                  m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
}

bool BoardEditor::pressChoosable(const QPointF &at, bool adding, bool joined)
{
    const Design &design = m_document->design();
    const PlacementText text = textUnder(at);
    if (!text.isNull() && !adding) {
        m_chosen.clear();
        m_outlineChosen = false;
        m_chosenText = text;
        m_dragText = true;
        m_dragAnchor = placementTextPoint(design.board, text);
        return true;
    }
    // Holes are drawn over the pads; vias and tracks are taken off the pads
    // of parts, which are the parts'.
    const double reach = m_canvas->pixelsToMm(CanvasView::HIT_PIXELS);
    const ObjectId hole = holeUnder(at);
    ObjectId copper;
    if (m_tracksShown && hole.isNull() && !padAt(design.board, design.library, at, 0)) {
        copper = viaAt(design.board, at, reach);
        if (copper.isNull())
            copper = trackAt(design.board, at, reach, trackSide());
    }
    const ObjectId hit = !hole.isNull()     ? hole
                         : !copper.isNull() ? copper
                                            : placementUnder(at);
    if (hit.isNull())
        return false;
    // Tracks and vias are chosen, not dragged.
    if (!copper.isNull()) {
        m_chosenText = PlacementText();
        // With Shift, all the copper joined to it.
        const QSet<ObjectId> taken =
            joined ? joinedCopper(design.board, design.library, copper) : QSet<ObjectId>{copper};
        if (adding) {
            if (m_chosen.contains(copper))
                m_chosen.subtract(taken);
            else
                m_chosen.unite(taken);
        } else if (joined || !m_chosen.contains(copper)) {
            m_chosen = taken;
            m_outlineChosen = false;
        }
        return true;
    }
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
    // The tracks and vias chosen go along whole, unless a part or a hole
    // chosen is locked and stays (a locked outline staying holds nothing);
    // the tracks on the pads moved stretch after them.
    const bool lockedChosen =
        std::any_of(board.placements.cbegin(), board.placements.cend(),
                       [this](const Placement &placement) {
                           return placement.locked && m_chosen.contains(placement.id);
                       })
        || std::any_of(board.holes.cbegin(), board.holes.cend(), [this](const MountingHole &hole) {
               return hole.locked && m_chosen.contains(hole.id);
           });
    const QSet<ObjectId> carried = lockedChosen ? QSet<ObjectId>() : chosenTracks();
    moved = withTracksMoved(moved, carried, delta);
    return withTracksFollowingPads(board, moved, m_document->design().library, carried);
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

void BoardEditor::press(const QPointF &at, bool adding, bool joined)
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
    case Tool::Track:
        trackClick(at);
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
    if (m_dragCorner < 0 && pressChoosable(at, adding, joined)) {
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
        const QSet<ObjectId> inside = (m_silkscreenShown
                                           ? placementsInside(design.board, design.library, m_frame)
                                           : QSet<ObjectId>())
                                          .unite(holesInside(design.board, m_frame))
                                          .unite(m_tracksShown ? tracksInside(design.board, m_frame)
                                                               : QSet<ObjectId>());
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
    // The first click of the two laid the track there: it ends.
    if (m_tool == Tool::Track) {
        m_trackFrom.reset();
        emit changed();
        return;
    }
    if (m_tool != Tool::Select) {
        press(at);
        return;
    }
    // A track off the pads chooses the whole of it.
    const Design &design = m_document->design();
    if (m_tracksShown && !padAt(design.board, design.library, at, 0)) {
        const ObjectId track = trackAt(design.board, at,
                                       m_canvas->pixelsToMm(CanvasView::HIT_PIXELS), trackSide());
        if (!track.isNull()) {
            dropChosen();
            m_chosen = trackRun(design.board, design.library, track);
            emit changed();
            return;
        }
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
    // Hidden or not: hiding only takes them out of sight for a while.
    for (const Track &track : m_document->design().board.tracks)
        m_chosen.insert(track.id);
    for (const Via &via : m_document->design().board.vias)
        m_chosen.insert(via.id);
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
    const PlacementText text = textUnder(at);
    m_menuAt = at;
    const ObjectId hole = text.isNull() ? holeUnder(at) : ObjectId();
    ObjectId copper;
    if (m_tracksShown && text.isNull() && hole.isNull()
        && !padAt(design.board, design.library, at, 0)) {
        const double reach = m_canvas->pixelsToMm(CanvasView::HIT_PIXELS);
        copper = viaAt(design.board, at, reach);
        if (copper.isNull())
            copper = trackAt(design.board, at, reach, trackSide());
    }
    const ObjectId hit = !text.isNull()     ? text.owner
                         : !hole.isNull()   ? hole
                         : !copper.isNull() ? copper
                                            : placementUnder(at);
    if (!hit.isNull()) {
        if (!m_chosen.contains(hit)) {
            dropChosen();
            m_chosen = {hit};
        }
        m_menuHole = hole;
        emit changed();
        if (!hole.isNull())
            return MenuTarget::Hole;
        return copper.isNull() ? MenuTarget::Placement : MenuTarget::Track;
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

namespace {

// What 'property' gives for every item of 'ids' in 'items', when it gives
// the same for all of them; nothing otherwise, or when none is there.
template <typename Item, typename Property>
auto commonOf(const QList<Item> &items, const QSet<ObjectId> &ids, Property property)
    -> std::optional<decltype(property(std::declval<const Item &>()))>
{
    std::optional<decltype(property(std::declval<const Item &>()))> common;
    for (const Item &item : items) {
        if (!ids.contains(item.id))
            continue;
        const auto value = property(item);
        if (common && *common != value)
            return std::nullopt;
        common = value;
    }
    return common;
}

} // namespace

Coord BoardEditor::chosenTracksWidth() const
{
    return commonOf(m_document->design().board.tracks, chosenTracks(),
                    [](const Track &track) { return track.width; })
        .value_or(0);
}

void BoardEditor::setChosenTracksWidth(Coord width)
{
    const QSet<ObjectId> ids = chosenTracks();
    const Board &board = m_document->design().board;
    const Board changedBoard = withTracksWidth(board, ids, width);
    if (width > 0 && changedBoard != board)
        applyBoard(Action::TrackWidth, changedBoard, ids);
}

void BoardEditor::setChosenTracksSide(BoardSide side)
{
    const QSet<ObjectId> ids = chosenTracks();
    const Board &board = m_document->design().board;
    const Board changedBoard = withTracksSide(board, ids, side);
    if (changedBoard != board)
        applyBoard(Action::TrackSide, changedBoard, ids);
}

void BoardEditor::editMenuTrackWidth()
{
    // A line still open keeps what it holds for its own tracks.
    m_valueEdit.close(true);
    const QSet<ObjectId> ids = chosenTracks();
    const Board &board = m_document->design().board;
    const auto first = std::find_if(board.tracks.cbegin(), board.tracks.cend(),
                                    [&ids](const Track &track) { return ids.contains(track.id); });
    if (first == board.tracks.cend())
        return;
    m_typed = Typed::TrackWidth;
    const Coord width = chosenTracksWidth();
    m_valueEdit.open(formatMmShort(width != 0 ? width : first->width), QRectF(m_menuAt, QSizeF(0, 0)));
}

void BoardEditor::hoverNet(const QPointF &at)
{
    const Design &design = m_document->design();
    // Only the copper shown: hidden tracks and vias light nothing.
    const double reach = m_canvas->pixelsToMm(CanvasView::HIT_PIXELS);
    const std::optional<CopperPoint> copper =
        m_tracksShown ? copperAt(design.board, design.library, at, reach)
                      : copperAt(withoutAnyTracks(design.board), design.library, at, reach);
    const QString net = copper ? copper->net : QString();
    if (!net.isEmpty() && net == m_hoveredNet) {
        // Shown again after it went out by itself or with a click.
        QToolTip::showText(QCursor::pos(), net, m_canvas->viewport());
        return;
    }
    // Off the net lit: it goes out at once, and the next waits its time.
    setHoveredNet(QString());
    if (net.isEmpty()) {
        m_hoverTimer.stop();
        m_pendingNet.clear();
    } else if (net != m_pendingNet) {
        m_pendingNet = net;
        m_hoverTimer.start();
    }
}

void BoardEditor::setHoveredNet(const QString &net)
{
    if (net == m_hoveredNet)
        return;
    m_hoveredNet = net;
    if (net.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(QCursor::pos(), net, m_canvas->viewport());
    emit changed();
}

Coord BoardEditor::chosenHolesDrill() const
{
    return commonOf(m_document->design().board.holes, chosenHoles(),
                         [](const MountingHole &hole) { return hole.drill; })
        .value_or(0);
}

bool BoardEditor::chosenHolesHavePads() const
{
    return commonOf(m_document->design().board.holes, chosenHoles(),
                         [](const MountingHole &hole) { return hole.ring > 0; })
               == std::optional<bool>(true);
}

bool BoardEditor::chosenHolesPlatedNone() const
{
    return commonOf(m_document->design().board.holes, chosenHoles(),
                         [](const MountingHole &hole) { return hole.plated; })
               == std::optional<bool>(false);
}

std::optional<CopperSides> BoardEditor::chosenHolesPadSides() const
{
    return commonOf(m_document->design().board.holes, chosenHoles(),
                         [](const MountingHole &hole) { return hole.padSides; });
}

std::optional<QString> BoardEditor::chosenHolesNet() const
{
    return commonOf(m_document->design().board.holes, chosenHoles(),
                         [](const MountingHole &hole) { return hole.net; });
}

void BoardEditor::setChosenHolesPadSides(CopperSides sides)
{
    const QSet<ObjectId> ids = chosenHoles();
    const Board &board = m_document->design().board;
    if (refuseHolesLocked())
        return;
    const Board changedBoard = withHolesPadSides(board, ids, sides);
    if (changedBoard != board)
        applyBoard(Action::PadSides, changedBoard, ids);
}

void BoardEditor::setChosenHolesNet(const QString &net)
{
    const QSet<ObjectId> ids = chosenHoles();
    const Board &board = m_document->design().board;
    if (refuseHolesLocked())
        return;
    const Board changedBoard = withHolesNet(board, ids, net);
    if (changedBoard != board)
        applyBoard(Action::PadNet, changedBoard, ids);
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
    const Coord value = typed == Typed::Pad ? (hole->ring > 0 ? holeOuterDiameter(*hole) : 0)
                                            : hole->drill;
    m_valueEdit.open(formatMmShort(value), QRectF(toMm(hole->at), QSizeF(0, 0)));
}

void BoardEditor::holeValueTyped(Typed typed, const QString &text)
{
    // A pad of 0 is taken off; a hole has a diameter.
    Coord value = 0;
    const bool pad = typed == Typed::Pad;
    if (!parseMm(text, value) || value < 0 || (!pad && value == 0)) {
        emit message(pad ? tr("Type the pad diameter in millimetres") : diameterHint());
        return;
    }
    if (!pad) {
        setChosenHolesDrill(value);
        return;
    }
    const QSet<ObjectId> ids = chosenHoles();
    const Board &board = m_document->design().board;
    // Those the pad does not fit keep theirs: say why.
    for (const MountingHole &hole : board.holes) {
        if (!ids.contains(hole.id) || hole.locked || takesPad(hole, value))
            continue;
        emit message(value == 0 ? tr("A plated hole needs a copper ring")
                                : tr("The pad must be larger than the hole"));
        break;
    }
    const Board padded = withHolesPad(board, ids, value);
    if (padded != board)
        applyBoard(Action::CopperRing, padded, ids);
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
    case Typed::TrackWidth: {
        Coord width = 0;
        if (parseMm(text, width) && width > 0)
            setChosenTracksWidth(width);
        else
            emit message(tr("Type the width in millimetres"));
        break;
    }
    case Typed::Pad:
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
    // Hidden tracks and vias, or parts with the silkscreen hidden, are not
    // chosen, after Undo either.
    if (!m_tracksShown)
        m_chosen.subtract(chosenTracks());
    if (!m_silkscreenShown)
        m_chosen.subtract(chosenPlacements());
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
    // The track being laid follows the mouse.
    if (m_active && m_tool == Tool::Track) {
        m_mouseAt = at;
        if (m_trackFrom)
            emit changed();
        return;
    }
    if (!m_active || m_tool != Tool::Select || m_pressed)
        return;
    hoverNet(at);
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
    } else if (m_trackFrom) {
        // The track begun stops where it was last laid to.
        m_trackFrom.reset();
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
