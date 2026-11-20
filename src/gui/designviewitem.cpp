#include "designviewitem.h"

#include "boardeditor.h"
#include "format.h"
#include "paint.h"
#include "sheeteditor.h"
#include "wiring.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

namespace ppcb {

namespace {

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
    if (m_page == boardPage())
        return boardBounds(design.board, design.library);
    const Sheet &sheet = design.schematic.sheets.at(m_page);
    QRectF bounds =
        sheetDrawingBounds(sheet, design.library, powerShown(design.project, design.schematic));
    if (bounds.isNull())
        return paperRect(sheet);
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
    const QList<OutlineCorner> *preview = editor == nullptr ? nullptr : editor->previewOutline();
    if (preview == nullptr) {
        paintBoard(painter, design.board, design.library, m_boardLayers);
    } else {
        Board board = design.board;
        board.outline = *preview;
        paintBoard(painter, board, design.library, m_boardLayers);
    }
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

    const QList<OutlineCorner> &outline = preview != nullptr ? *preview : design.board.outline;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = paintcolor::selection();
    fill.setAlpha(40);
    const QPen pen(paintcolor::selection(), 0);

    const int count = int(outline.size());
    const int side = editor->litSide();
    if (editor->showsCorners() && side >= 0 && side < count) {
        QColor band = paintcolor::selection();
        band.setAlpha(110);
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
        board->press(event->scenePos());
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
