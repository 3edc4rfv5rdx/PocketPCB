#include "designviewitem.h"

#include "boardeditor.h"
#include "format.h"
#include "paint.h"
#include "ratsnest.h"
#include "sheeteditor.h"
#include "wiring.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace ppcb {

namespace {

// How opaque the track being laid is.
const int TRACK_PREVIEW_ALPHA = 200;
// The band round a track or a via chosen: how opaque, and how far beyond
// its copper, in screen pixels.
const int CHOSEN_TRACK_ALPHA = 140;
const double CHOSEN_TRACK_PIXELS = 3;

// The band drawn along a selected wire or bus, in millimetres.
const double SELECTED_LINE_WIDTH = 0.8;
// How opaque the net lit is drawn over the sheet, of 255.
const int LIT_NET_ALPHA = 110;
// The radius of the dot on a pin of the net lit, in millimetres.
const double LIT_PIN_RADIUS = 0.6;
// A corner of the board outline is marked by a square this many pixels
// across, at any zoom.
const double CORNER_MARK_PIXELS = 7.0;
// The edge of the board lit under the mouse, in pixels.
const double LIT_EDGE_PIXELS = 4.0;
// The width of a ratsnest line of a net lit, in pixels.
const double LIT_LINE_PIXELS = 3.0;
// How far a line of the ratsnest drawn as an arc is bent, as a part of its
// length: the control point of the curve, which bends it half as far.
const double RATSNEST_BEND = 1.0 / 3;
// The dots at the ends of the ratsnest drawn bright, in screen pixels.
const double RATSNEST_END_PIXELS = 4.0;
// The arms of the origin mark on the board, in pixels.
const double ORIGIN_ARM_PIXELS = 30.0;
// Where the coordinates of the corner being drawn stand from it, in pixels.
const QPointF COORDINATES_OFFSET(14, -10);
// The least width and height of what a view fitted to the drawing holds,
// in millimetres: a lone straight wire has none across it.
const double FIT_DRAWING_MIN_SIZE = 10.0;

} // namespace

DesignViewItem::DesignViewItem(const Document *document, const QRectF &area)
    : m_document(document)
    , m_area(area)
{
}

int DesignViewItem::pageCount() const
{
    return int(m_document->design().schematic.sheets.size()) + 1;
}

void DesignViewItem::setPage(int page)
{
    m_page = qBound(0, page, pageCount() - 1);
    update();
}

void DesignViewItem::contentChanged()
{
    setPage(m_page);
}

QRectF DesignViewItem::pageBounds() const
{
    const Design &design = m_document->design();
    if (m_page == boardPage())
        return boardBounds(design.board, design.library);
    return paperRect(design.schematic.sheets.at(m_page));
}

QRectF DesignViewItem::drawingBounds() const
{
    const Design &design = m_document->design();
    QRectF bounds;
    if (m_page == boardPage()) {
        bounds = layoutBounds(design.board, design.library);
    } else {
        const Sheet &sheet = design.schematic.sheets.at(m_page);
        bounds = sheetDrawingBounds(sheet, design.library,
                                    powerShown(design.project, design.schematic));
        if (bounds.isNull())
            return paperRect(sheet);
    }
    const double growX = qMax(0.0, FIT_DRAWING_MIN_SIZE - bounds.width()) / 2;
    const double growY = qMax(0.0, FIT_DRAWING_MIN_SIZE - bounds.height()) / 2;
    return bounds.adjusted(-growX, -growY, growX, growY);
}

void DesignViewItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                           QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    const Design &design = m_document->design();
    if (m_page == boardPage()) {
        paintBoardPage(*painter);
        return;
    }
    const SheetEditor *editor = activeEditor();
    const Sheet *preview = editor == nullptr ? nullptr : editor->preview();
    if (preview == nullptr) {
        paintSheet(*painter, design.schematic, design.library, m_page, m_document->titleBlockShown(),
                   powerShown(design.project, design.schematic));
        paintLitNet(*painter, design.schematic.sheets.at(m_page));
        paintSelection(*painter, design.schematic.sheets.at(m_page));
        if (editor != nullptr && m_ercMarksShown)
            paintErcMarks(*painter, editor->problems(), m_page);
        return;
    }
    // A drag or a placing in progress: the sheet as it would be.
    Schematic schematic = design.schematic;
    schematic.sheets[m_page] = *preview;
    const Library *library = editor->previewLibrary();
    paintSheet(*painter, schematic, library != nullptr ? *library : design.library, m_page,
               m_document->titleBlockShown(), powerShown(design.project, schematic));
    paintSelection(*painter, *preview);
}

void DesignViewItem::setEditor(SheetEditor *editor)
{
    m_editor = editor;
    setAcceptedMouseButtons(editor == nullptr ? Qt::NoButton : Qt::LeftButton);
    setAcceptHoverEvents(editor != nullptr);
}

void DesignViewItem::setBoardEditor(BoardEditor *editor)
{
    m_boardEditor = editor;
}

void DesignViewItem::setDrawingCoordinatesShown(bool shown)
{
    if (shown == m_drawingCoordinatesShown)
        return;
    m_drawingCoordinatesShown = shown;
    update();
}

void DesignViewItem::setRatsnestArcs(bool arcs)
{
    if (arcs == m_ratsnestArcs)
        return;
    m_ratsnestArcs = arcs;
    update();
}

void DesignViewItem::setRatsnestBright(bool bright)
{
    if (bright == m_ratsnestBright)
        return;
    m_ratsnestBright = bright;
    update();
}

void DesignViewItem::setBoardLayers(const BoardLayerSet &layers)
{
    if (layers == m_boardLayers)
        return;
    m_boardLayers = layers;
    update();
}

void DesignViewItem::setErcMarksShown(bool shown)
{
    if (shown == m_ercMarksShown)
        return;
    m_ercMarksShown = shown;
    update();
}

void DesignViewItem::setDrcMarksShown(bool shown)
{
    if (shown == m_drcMarksShown)
        return;
    m_drcMarksShown = shown;
    update();
}

SheetEditor *DesignViewItem::activeEditor() const
{
    if (m_editor == nullptr || m_editor->sheet() != m_page || m_page == boardPage())
        return nullptr;
    return m_editor;
}

BoardEditor *DesignViewItem::activeBoardEditor() const
{
    if (m_boardEditor == nullptr || m_page != boardPage() || !m_boardEditor->isActive())
        return nullptr;
    return m_boardEditor;
}

void DesignViewItem::paintBoardPage(QPainter &painter) const
{
    const Design &design = m_document->design();
    const BoardEditor *editor = activeBoardEditor();
    const Board board = editor == nullptr ? design.board : editor->shownBoard();
    // The side tracks are laid on bright, the other darker, on a board of two.
    const std::optional<BoardSide> bright =
        editor != nullptr && board.copper == CopperSides::Both
            ? std::optional<BoardSide>(editor->trackSide())
            : std::nullopt;
    // Hidden tracks and vias are left out of the drawing, not out of the
    // ratsnest: what they join stays joined.
    const bool tracksShown = editor == nullptr || editor->tracksShown();
    const Board shown = tracksShown ? board : withoutAnyTracks(board);
    paintBoard(painter, shown, design.library, m_boardLayers, true, bright);
    // The pads still to be joined, as the board stands now; the nets of the
    // parts chosen, and the net the mouse stays on, are lit: their pads,
    // vias, tracks and lines.
    const QSet<QString> lit = editor == nullptr ? QSet<QString>() : editor->litNets();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Lit in the colour of the ratsnest, bolder.
    QColor litColor = paintcolor::ratsnest();
    litColor.setAlpha(LIT_NET_ALPHA);
    painter.setPen(Qt::NoPen);
    painter.setBrush(litColor);
    for (const NetPad &pad : netPads(board, design.library)) {
        if (lit.contains(pad.net)) {
            const double radius = nmToMm(std::max(pad.sizeX, pad.sizeY)) / 2;
            painter.drawEllipse(toMm(pad.at), radius, radius);
        }
    }
    // Its vias and tracks too.
    for (const Via &via : shown.vias) {
        if (lit.contains(via.net))
            painter.drawEllipse(toMm(via.at), nmToMm(via.size) / 2, nmToMm(via.size) / 2);
    }
    for (const Track &track : shown.tracks) {
        if (!lit.contains(track.net))
            continue;
        painter.setPen(QPen(litColor, nmToMm(track.width), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(toMm(track.from), toMm(track.to));
    }
    painter.setBrush(Qt::NoBrush);
    const double litWidth = LIT_LINE_PIXELS / painter.worldTransform().m11();
    const double endRadius = RATSNEST_END_PIXELS / painter.worldTransform().m11();
    // And the unrouted connections of the copper chosen whole.
    const QString joined = editor == nullptr ? QString() : editor->joinedChosenNet();
    for (const RatsnestLine &line : ratsnest(board, design.library)) {
        const bool bold =
            m_ratsnestBright || lit.contains(line.net) || (!joined.isEmpty() && line.net == joined);
        if (bold)
            painter.setPen(QPen(paintcolor::ratsnest(), litWidth));
        else
            painter.setPen(QPen(paintcolor::ratsnest(), 0));
        const QPointF from = toMm(line.from);
        const QPointF to = toMm(line.to);
        if (m_ratsnestArcs) {
            // Bent to the left of its way by a part of its length.
            const QPointF along = to - from;
            const QPointF bend = (from + to) / 2 + QPointF(along.y(), -along.x()) * RATSNEST_BEND;
            QPainterPath arc(from);
            arc.quadTo(bend, to);
            painter.drawPath(arc);
        } else {
            painter.drawLine(from, to);
        }
        if (m_ratsnestBright) {
            painter.setBrush(paintcolor::ratsnest());
            painter.drawEllipse(toMm(line.from), endRadius, endRadius);
            painter.drawEllipse(toMm(line.to), endRadius, endRadius);
            painter.setBrush(Qt::NoBrush);
        }
    }
    painter.restore();
    // The origin, which coordinates count from: a corner of two arms along
    // +X and +Y, the same size at any zoom; on the screen only.
    const double pixel = 1.0 / painter.worldTransform().m11();
    painter.save();
    painter.setPen(QPen(paintcolor::boardOrigin(), 0));
    const double arm = ORIGIN_ARM_PIXELS * pixel;
    painter.drawPolyline(QPolygonF({QPointF(arm, 0), QPointF(0, 0), QPointF(0, arm)}));
    painter.restore();
    if (editor == nullptr)
        return;

    const QList<OutlineCorner> &outline = board.outline;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = paintcolor::selection();
    fill.setAlpha(40);
    const QPen pen(paintcolor::selection(), 0);

    const int count = int(outline.size());
    QColor band = paintcolor::selection();
    band.setAlpha(110);
    // The outline chosen whole: a band all round it.
    if (editor->outlineChosen() && count > 0) {
        QPolygonF corners;
        for (const OutlineCorner &corner : outline)
            corners.append(toMm(corner.at));
        painter.setPen(QPen(band, LIT_EDGE_PIXELS * pixel, Qt::SolidLine, Qt::RoundCap,
                            Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(corners);
    }
    const int side = editor->litSide();
    if (editor->showsCorners() && side >= 0 && side < count) {
        painter.setPen(QPen(band, LIT_EDGE_PIXELS * pixel, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(toMm(outline.at(side).at), toMm(outline.at((side + 1) % count).at));
    }
    if (editor->showsCorners()) {
        const double half = CORNER_MARK_PIXELS * pixel / 2;
        for (int i = 0; i < count; ++i) {
            painter.setPen(pen);
            painter.setBrush(i == editor->litCorner() ? paintcolor::selection() : fill);
            painter.drawRect(QRectF(toMm(outline.at(i).at) - QPointF(half, half),
                                    QSizeF(2 * half, 2 * half)));
        }
    }
    const QPolygonF drawn = editor->drawnOutline();
    if (!drawn.isEmpty()) {
        painter.setPen(QPen(paintcolor::boardOutline(), 0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(drawn);
        const double half = CORNER_MARK_PIXELS * pixel / 2;
        painter.setPen(pen);
        painter.setBrush(fill);
        for (const QPointF &point : drawn)
            painter.drawRect(QRectF(point - QPointF(half, half), QSizeF(2 * half, 2 * half)));
    }
    // The placements and holes chosen, a text chosen alone, the frame
    // choosing them.
    const QSet<ObjectId> chosen = editor->chosenPlacements();
    painter.setPen(pen);
    painter.setBrush(fill);
    for (const Placement &placement : board.placements) {
        if (!chosen.contains(placement.id))
            continue;
        // A placement chosen with its texts.
        painter.drawRect(placementRect(placement, design.library));
        for (const ComponentText text : {ComponentText::Designator, ComponentText::Value}) {
            if (!placementTextShown(placement, text).isEmpty())
                painter.drawRect(placementTextRect(placement, text));
        }
    }
    // The tracks and the vias chosen: a band round each.
    const QSet<ObjectId> chosenTracks = editor->chosenTracks();
    QColor bandColor = paintcolor::selection();
    bandColor.setAlpha(CHOSEN_TRACK_ALPHA);
    // Of those shown: hidden ones stay out of sight, chosen or not.
    for (const Track &track : shown.tracks) {
        if (!chosenTracks.contains(track.id))
            continue;
        painter.setPen(QPen(bandColor, nmToMm(track.width) + 2 * pixel * CHOSEN_TRACK_PIXELS,
                            Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(toMm(track.from), toMm(track.to));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(bandColor);
    for (const Via &via : shown.vias) {
        if (chosenTracks.contains(via.id)) {
            const double radius = nmToMm(via.size) / 2 + pixel * CHOSEN_TRACK_PIXELS;
            painter.drawEllipse(toMm(via.at), radius, radius);
        }
    }
    // A wire link chosen: its pads and its wire.
    for (const WireLink &link : shown.links) {
        if (!chosenTracks.contains(link.id))
            continue;
        const double radius = nmToMm(link.size) / 2 + pixel * CHOSEN_TRACK_PIXELS;
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(toMm(link.from), radius, radius);
        painter.drawEllipse(toMm(link.to), radius, radius);
        painter.setPen(QPen(bandColor, 2 * pixel * CHOSEN_TRACK_PIXELS, Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(toMm(link.from), toMm(link.to));
    }
    painter.setPen(Qt::NoPen);
    // The track being laid, in its side's copper, a little see-through.
    for (const Track &track : editor->trackPreview()) {
        QColor copper = boardLayerColor(copperLayer(track.side));
        copper.setAlpha(TRACK_PREVIEW_ALPHA);
        painter.setPen(QPen(copper, nmToMm(track.width), Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(toMm(track.from), toMm(track.to));
    }
    painter.setPen(pen);
    painter.setBrush(fill);
    // The holes chosen, and the one to place under the cursor.
    const QSet<ObjectId> chosenHoles = editor->chosenHoles();
    for (const MountingHole &hole : board.holes) {
        if (!chosenHoles.contains(hole.id))
            continue;
        const double radius = nmToMm(holeOuterDiameter(hole)) / 2;
        painter.drawEllipse(toMm(hole.at), radius, radius);
    }
    if (const std::optional<MountingHole> hole = editor->holeToPlace()) {
        const double radius = nmToMm(holeOuterDiameter(*hole)) / 2;
        painter.drawEllipse(toMm(hole->at), radius, radius);
    }
    const PlacementText text = editor->chosenText();
    if (const Placement *owner = findById(board.placements, text.owner))
        painter.drawRect(placementTextRect(*owner, text.text));
    if (const WireLink *owner = findById(board.links, text.owner))
        painter.drawRect(linkTextRect(*owner));
    for (const QLineF &guide : editor->textGuides())
        painter.drawLine(guide);
    if (m_drcMarksShown)
        paintDrcMarks(painter, editor->problems());
    const QRectF frame = editor->chooserFrame();
    if (!frame.isEmpty()) {
        painter.setPen(QPen(paintcolor::selection(), 0, Qt::DashLine));
        painter.drawRect(frame);
    }
    const QRectF cut = editor->cutRect();
    if (!cut.isNull()) {
        painter.setPen(QPen(paintcolor::selection(), 0, Qt::DashLine));
        painter.setBrush(fill);
        painter.drawRect(cut);
    }
    // The coordinates beside the corner being drawn, in the screen's font.
    const std::optional<Point> drawnPoint = editor->drawnPoint();
    if (m_drawingCoordinatesShown && drawnPoint) {
        const QPointF at = painter.worldTransform().map(toMm(*drawnPoint));
        painter.resetTransform();
        painter.setPen(paintcolor::boardOrigin());
        painter.drawText(at + COORDINATES_OFFSET, formatPosition(drawnPoint->x, drawnPoint->y));
    }
    painter.restore();
}

void DesignViewItem::paintLitNet(QPainter &painter, const Sheet &sheet) const
{
    const SheetEditor *editor = activeEditor();
    const Net *net = editor == nullptr ? nullptr : editor->litNet();
    if (net == nullptr)
        return;
    const Design &design = m_document->design();
    const Library &library = design.library;
    const QSet<ObjectId> drawn = net->objects.value(m_page);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor color = paintcolor::litNet();
    color.setAlpha(LIT_NET_ALPHA);
    const QPen band(color, SELECTED_LINE_WIDTH, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    for (const SheetObjectShape &shape :
         sheetObjectShapes(sheet, library, powerShown(design.project, design.schematic))) {
        if (!drawn.contains(shape.id))
            continue;
        if (shape.polyline.isEmpty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRect(shape.rect);
        } else {
            painter.setPen(band);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(shape.polyline);
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const SheetPin &pin : sheetPins(sheet, library)) {
        if (netHasPin(*net, pin.owner, pin.number))
            painter.drawEllipse(toMm(pin.at), LIT_PIN_RADIUS, LIT_PIN_RADIUS);
    }
    painter.restore();
}

void DesignViewItem::paintSelection(QPainter &painter, const Sheet &sheet) const
{
    const SheetEditor *editor = activeEditor();
    if (editor == nullptr)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = paintcolor::selection();
    fill.setAlpha(40);
    // A line one pixel wide at any zoom.
    QPen pen(paintcolor::selection(), 0);
    // A wire or a bus, or a segment of a wire: a wide band along it.
    QColor bandColor = paintcolor::selection();
    bandColor.setAlpha(110);
    const QPen band(bandColor, SELECTED_LINE_WIDTH, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const Design &design = m_document->design();
    const QList<SheetObjectShape> shapes =
        sheetObjectShapes(sheet, design.library, powerShown(design.project, design.schematic));
    for (const SheetObjectShape &shape : shapes) {
        if (!editor->selection().contains(shape.id))
            continue;
        if (shape.polyline.isEmpty()) {
            painter.setPen(pen);
            painter.setBrush(fill);
            painter.drawRect(shape.rect);
        } else {
            painter.setPen(band);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(shape.polyline);
        }
    }
    const QLineF segment = editor->chosenSegment();
    if (!segment.isNull()) {
        painter.setPen(band);
        painter.drawLine(segment);
    }
    const QRectF text = editor->chosenText();
    if (!text.isEmpty()) {
        painter.setPen(pen);
        painter.setBrush(fill);
        painter.drawRect(text);
    }
    const QLineF guide = editor->textGuide();
    if (!guide.isNull()) {
        painter.setPen(pen);
        painter.drawLine(guide);
    }
    const QRectF rubber = editor->rubberBand();
    if (!rubber.isEmpty()) {
        painter.setPen(QPen(paintcolor::selection(), 0, Qt::DashLine));
        painter.setBrush(fill);
        painter.drawRect(rubber);
    }
    painter.restore();
}

void DesignViewItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    BoardEditor *board = activeBoardEditor();
    if (board != nullptr && event->button() == Qt::LeftButton) {
        board->press(event->scenePos(), event->modifiers().testFlag(Qt::ControlModifier),
                     event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return;
    }
    SheetEditor *editor = activeEditor();
    if (editor == nullptr || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    editor->press(event->scenePos(), event->modifiers().testFlag(Qt::ControlModifier),
                  event->modifiers().testFlag(Qt::ShiftModifier));
    event->accept();
}

void DesignViewItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    BoardEditor *board = activeBoardEditor();
    if (board != nullptr && event->button() == Qt::LeftButton) {
        board->doubleClick(event->scenePos());
        event->accept();
        return;
    }
    SheetEditor *editor = activeEditor();
    if (editor == nullptr || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    editor->doubleClick(event->scenePos(), event->modifiers().testFlag(Qt::ControlModifier));
    event->accept();
}

void DesignViewItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    hoverMoveEvent(event);
}

void DesignViewItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    if (BoardEditor *board = activeBoardEditor())
        board->hoverMouse(event->scenePos());
    if (SheetEditor *editor = activeEditor())
        editor->hoverNet(event->scenePos());
}

void DesignViewItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event);
    if (m_editor != nullptr)
        m_editor->clearHoveredNet();
}

void DesignViewItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (BoardEditor *board = activeBoardEditor())
        board->move(event->scenePos());
    if (SheetEditor *editor = activeEditor())
        editor->move(event->scenePos(), event->modifiers().testFlag(Qt::ShiftModifier));
}

void DesignViewItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (BoardEditor *board = activeBoardEditor())
        board->release(event->scenePos());
    if (SheetEditor *editor = activeEditor())
        editor->release(event->scenePos(), event->modifiers().testFlag(Qt::ShiftModifier));
}

} // namespace ppcb
