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
    , m_radiusEdit(canvas)
{
    connect(&m_radiusEdit, &InPlaceEdit::accepted, this, &BoardEditor::radiusTyped);
    // Undo, Redo or a project opened: what was dragged or lit may be gone.
    connect(m_document, &Document::designChanged, this, [this] {
        m_radiusEdit.close(false);
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
    case Action::RoundCorner:
        return tr("Round board corner");
    }
    return QString();
}

void BoardEditor::setActive(bool active)
{
    m_radiusEdit.close(true);
    m_active = active;
    m_tool = Tool::Select;
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
    if (!m_active)
        return;
    chooseSelectTool();
    m_tool = Tool::Outline;
    // Where the cursor is now, before it moves.
    m_cursor = Point{m_canvas->cursorX(), m_canvas->cursorY()};
    emit changed();
}

void BoardEditor::startCuttingOut()
{
    if (!m_active)
        return;
    chooseSelectTool();
    m_tool = Tool::CutOut;
    // Where the cursor is now, before it moves.
    m_cursor = Point{m_canvas->cursorX(), m_canvas->cursorY()};
    emit changed();
}

void BoardEditor::makeRectangleBoard(Coord width, Coord height)
{
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

void BoardEditor::press(const QPointF &at)
{
    if (!m_active)
        return;
    m_radiusEdit.close(true);
    switch (m_tool) {
    case Tool::Outline:
        outlineClick(m_canvas->snapped(at));
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
    m_pressed = true;
    m_pressAt = at;
    m_pressGrid = m_canvas->snapped(at);
    m_dragCorner = outlineCornerAt(outline(), at, m_canvas->pixelsToMm(CanvasView::END_PIXELS));
    if (m_dragCorner < 0)
        m_dragSide = outlineSideAt(outline(), at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
    if (m_dragSide >= 0) {
        const int count = int(outline().size());
        const QPointF start = toMm(outline().at(m_dragSide).at);
        const QPointF end = toMm(outline().at((m_dragSide + 1) % count).at);
        m_dragSideEnd = QLineF(at, end).length() < QLineF(at, start).length();
    }
    setLit(m_dragCorner, m_dragSide);
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
    if (!m_pressed || (m_dragCorner < 0 && m_dragSide < 0))
        return;
    if (!m_dragging) {
        if (QLineF(at, m_pressAt).length()
            < m_canvas->pixelsToMm(QApplication::startDragDistance()))
            return;
        m_dragging = true;
        m_preview = outline();
    }
    // Where the outline would go wrong, it stays as it was last right.
    const Point cursor = m_canvas->snapped(at);
    const std::optional<QList<OutlineCorner>> moved =
        m_dragCorner >= 0 ? withOutlineCornerMoved(outline(), m_dragCorner, cursor)
                          : withOutlineSideMoved(outline(), m_dragSide, cursor - m_pressGrid);
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
    const std::optional<QList<OutlineCorner>> moved = m_dragging ? m_preview : std::nullopt;
    const bool corner = m_dragCorner >= 0;
    dropDrag();
    if (moved && *moved != outline())
        apply(corner ? Action::MoveCorner : Action::MoveSide, *moved);
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
    if (corner >= 0)
        editRadius(corner);
}

void BoardEditor::hover(const Point &at)
{
    if (m_cursor == at)
        return;
    m_cursor = at;
    // The side being drawn and the coordinates follow it.
    if (m_active && (m_tool == Tool::Outline || m_tool == Tool::CutOut))
        emit changed();
}

void BoardEditor::hoverMouse(const QPointF &at)
{
    if (!m_active || m_tool != Tool::Select || m_pressed)
        return;
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
    if (m_dragging && m_preview) {
        const int count = int(m_preview->size());
        int corner = m_dragCorner;
        if (corner < 0 && m_dragSide >= 0 && m_dragSide < count)
            corner = m_dragSideEnd ? (m_dragSide + 1) % count : m_dragSide;
        if (corner >= 0 && corner < count)
            return m_preview->at(corner).at;
    }
    if (m_tool == Tool::Outline || m_tool == Tool::CutOut)
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
    m_radiusEdit.close(true);
    if (corner >= outline().size())
        return;
    m_radiusCorner = corner;
    const Point at = outline().at(corner).at;
    m_radiusEdit.open(formatMmShort(outline().at(corner).radius), QRectF(toMm(at), QSizeF(0, 0)));
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
    m_preview.reset();
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
