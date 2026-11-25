#include "canvasview.h"

#include "grid.h"
#include "paint.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

// The scene is a square around the origin, in millimetres.
const double SCENE_HALF_SIZE_MM = 500.0;

// The grid is not drawn when its step is smaller than this on screen.
const double MIN_GRID_PIXELS = 8.0;
const double GRID_DOT_PIXELS = 2.0;
const double SMALL_CROSS_HALF_PIXELS = 12.0;
// Wheel angle of one notch, in the units of QWheelEvent::angleDelta().
const double WHEEL_NOTCH = 120.0;
// fitRect() leaves this many pixels of margin on each side of the rectangle,
// so that its edge stays in sight.
const int FIT_MARGIN = 4;

// The grid and the cursor on a light background, and on a dark one (the
// board).
const QColor GRID_LINE_COLOR(205, 205, 205);
const QColor GRID_DOT_COLOR(125, 125, 125);
const QColor CURSOR_COLOR(60, 60, 60);
const QColor DARK_GRID_LINE_COLOR(65, 65, 65);
const QColor DARK_GRID_DOT_COLOR(135, 135, 135);
// The grid while it is lit (setGridBright()).
const QColor BRIGHT_GRID_LINE_COLOR(170, 170, 170);
const QColor BRIGHT_GRID_DOT_COLOR(80, 80, 80);
const QColor DARK_BRIGHT_GRID_LINE_COLOR(100, 100, 100);
const QColor DARK_BRIGHT_GRID_DOT_COLOR(180, 180, 180);
const QColor DARK_CURSOR_COLOR(210, 210, 210);
// A background darker than this lightness (0 to 255) is dark.
const int DARK_LIGHTNESS = 128;

} // namespace

CanvasView::CanvasView(QWidget *parent)
    : QGraphicsView(parent)
    , m_gridStep(DEFAULT_GRID_STEP)
    , m_background(paintcolor::background())
{
    setScene(new QGraphicsScene(this));
    scene()->setSceneRect(-SCENE_HALF_SIZE_MM, -SCENE_HALF_SIZE_MM,
                          2 * SCENE_HALF_SIZE_MM, 2 * SCENE_HALF_SIZE_MM);

    // Zooming positions the view itself (see zoomAt). A resize keeps the
    // top left corner: a dock opened or closed beside the canvas leaves
    // the drawing where it is.
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    // The grid cursor is drawn over the whole view, so partial updates
    // would leave traces of it.
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);

    scale(INITIAL_PIXELS_PER_MM, INITIAL_PIXELS_PER_MM);
    centerOn(0.0, 0.0);
}

double CanvasView::pixelsPerMm() const
{
    return transform().m11();
}

Point CanvasView::snapped(const QPointF &at) const
{
    return Point{snapToGrid(mmToNm(at.x()), m_gridStep), snapToGrid(mmToNm(at.y()), m_gridStep)};
}

void CanvasView::setGridStep(Coord step)
{
    if (step <= 0 || step == m_gridStep)
        return;
    m_gridStep = step;
    emit gridStepChanged(step);
    updateCursor();
}

void CanvasView::setGridStyle(GridStyle style)
{
    m_gridStyle = style;
    viewport()->update();
}

void CanvasView::setGridBright(bool bright)
{
    if (bright == m_gridBright)
        return;
    m_gridBright = bright;
    viewport()->update();
}

void CanvasView::setCursorStyle(CursorStyle style)
{
    m_cursorStyle = style;
    viewport()->update();
}

void CanvasView::setZoomCenterAndWarp(bool on)
{
    m_zoomCenterAndWarp = on;
}

void CanvasView::fitRect(const QRectF &rect)
{
    if (rect.width() <= 0 || rect.height() <= 0)
        return;
    const QSize view = viewport()->size();
    const double wanted = std::min((view.width() - 2 * FIT_MARGIN) / rect.width(),
                                   (view.height() - 2 * FIT_MARGIN) / rect.height());
    const double target = qBound(MIN_PIXELS_PER_MM, wanted, MAX_PIXELS_PER_MM);
    const double current = pixelsPerMm();
    scale(target / current, target / current);
    centerOn(rect.center());
    emit viewMoved();
    updateCursor();
}

QPointF CanvasView::viewCenter() const
{
    const QRectF view = viewport()->rect();
    return viewportTransform().inverted().map(view.center());
}

void CanvasView::setView(const QPointF &center, double pixelsPerMm)
{
    const double target = qBound(MIN_PIXELS_PER_MM, pixelsPerMm, MAX_PIXELS_PER_MM);
    const double current = this->pixelsPerMm();
    scale(target / current, target / current);
    centerOn(center);
    emit viewMoved();
    updateCursor();
}

void CanvasView::setBackgroundColor(const QColor &color)
{
    if (color == m_background)
        return;
    m_background = color;
    viewport()->update();
}

bool CanvasView::isDark() const
{
    return m_background.lightness() < DARK_LIGHTNESS;
}

void CanvasView::drawBackground(QPainter *painter, const QRectF &rect)
{
    painter->fillRect(rect, m_background);

    const double step = nmToMm(m_gridStep);
    if (step * pixelsPerMm() < MIN_GRID_PIXELS)
        return;

    // Grid lines are counted from the origin (index * step), not added up,
    // so no error accumulates across the view.
    const qint64 firstColumn = static_cast<qint64>(std::floor(rect.left() / step));
    const qint64 lastColumn = static_cast<qint64>(std::ceil(rect.right() / step));
    const qint64 firstRow = static_cast<qint64>(std::floor(rect.top() / step));
    const qint64 lastRow = static_cast<qint64>(std::ceil(rect.bottom() / step));

    if (m_gridStyle == GridStyle::Lines) {
        QVector<QLineF> lines;
        for (qint64 column = firstColumn; column <= lastColumn; ++column) {
            const double x = column * step;
            lines.append(QLineF(x, rect.top(), x, rect.bottom()));
        }
        for (qint64 row = firstRow; row <= lastRow; ++row) {
            const double y = row * step;
            lines.append(QLineF(rect.left(), y, rect.right(), y));
        }
        // Width 0: one pixel at any zoom.
        const QColor color = m_gridBright
                                 ? (isDark() ? DARK_BRIGHT_GRID_LINE_COLOR : BRIGHT_GRID_LINE_COLOR)
                                 : (isDark() ? DARK_GRID_LINE_COLOR : GRID_LINE_COLOR);
        painter->setPen(QPen(color, 0));
        painter->drawLines(lines);
    } else {
        QVector<QPointF> dots;
        for (qint64 row = firstRow; row <= lastRow; ++row) {
            for (qint64 column = firstColumn; column <= lastColumn; ++column)
                dots.append(QPointF(column * step, row * step));
        }
        const QColor color = m_gridBright
                                 ? (isDark() ? DARK_BRIGHT_GRID_DOT_COLOR : BRIGHT_GRID_DOT_COLOR)
                                 : (isDark() ? DARK_GRID_DOT_COLOR : GRID_DOT_COLOR);
        QPen pen(color, GRID_DOT_PIXELS);
        pen.setCosmetic(true); // width in pixels, not in millimetres
        painter->setPen(pen);
        painter->drawPoints(dots.constData(), int(dots.size()));
    }
}

void CanvasView::drawForeground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect);
    if (m_cursorStyle == CursorStyle::Arrow || !m_cursorInside)
        return;

    const double x = nmToMm(m_cursorX);
    const double y = nmToMm(m_cursorY);
    painter->setPen(QPen(isDark() ? DARK_CURSOR_COLOR : CURSOR_COLOR, 0));

    if (m_cursorStyle == CursorStyle::FullCross) {
        const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
        painter->drawLine(QLineF(visible.left(), y, visible.right(), y));
        painter->drawLine(QLineF(x, visible.top(), x, visible.bottom()));
    } else {
        const double half = SMALL_CROSS_HALF_PIXELS / pixelsPerMm();
        painter->drawLine(QLineF(x - half, y, x + half, y));
        painter->drawLine(QLineF(x, y - half, x, y + half));
    }
}

void CanvasView::wheelEvent(QWheelEvent *event)
{
    event->accept();
    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;
    zoomAt(event->position().toPoint(), std::pow(ZOOM_STEP, delta / WHEEL_NOTCH));
}

void CanvasView::zoomAt(const QPoint &viewPos, double factor)
{
    const double current = pixelsPerMm();
    const double target = qBound(MIN_PIXELS_PER_MM, current * factor, MAX_PIXELS_PER_MM);
    if (qFuzzyCompare(target, current))
        return;

    const QPointF scenePos = mapToScene(viewPos);
    scale(target / current, target / current);

    if (m_zoomCenterAndWarp) {
        centerOn(scenePos);
        // At the edge of the scene the point cannot reach the centre, so the
        // mouse goes to where the point really ended up.
        QCursor::setPos(viewport()->mapToGlobal(mapFromScene(scenePos)));
    } else {
        // Bring the point back under the mouse.
        scrollBy(mapFromScene(scenePos) - viewPos);
    }
    // A scale alone, with nothing scrolled, is not reported otherwise.
    emit viewMoved();
    updateCursor();
}

void CanvasView::scrollBy(const QPoint &pixels)
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + pixels.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + pixels.y());
}

void CanvasView::mousePressEvent(QMouseEvent *event)
{
    const bool panButton = event->button() == Qt::RightButton
                           || event->button() == Qt::MiddleButton;
    if (panButton && m_panButton == Qt::NoButton) {
        m_panButton = event->button();
        m_panStartPos = event->position().toPoint();
        m_panLastPos = m_panStartPos;
        m_panMoved = false;
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void CanvasView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();
    m_cursorViewPos = pos;
    m_cursorInside = true;

    if (m_panButton == Qt::NoButton) {
        QGraphicsView::mouseMoveEvent(event);
        updateCursor();
        return;
    }

    const QPoint delta = pos - m_panLastPos;
    m_panLastPos = pos;
    if (!m_panMoved
        && (pos - m_panStartPos).manhattanLength() >= QApplication::startDragDistance()) {
        m_panMoved = true;
        viewport()->setCursor(Qt::ClosedHandCursor);
    }
    // The scene follows the mouse: dragging right shows what is on the left.
    if (m_panMoved)
        scrollBy(-delta);
    updateCursor();
    event->accept();
}

void CanvasView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panButton == Qt::NoButton || event->button() != m_panButton) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    const bool wasClick = !m_panMoved;
    const bool wasRightButton = m_panButton == Qt::RightButton;
    m_panButton = Qt::NoButton;
    m_panMoved = false;
    viewport()->unsetCursor();
    event->accept();

    if (wasClick && wasRightButton)
        emit contextMenuRequested(event->globalPosition().toPoint());
}

void CanvasView::contextMenuEvent(QContextMenuEvent *event)
{
    // The system sends this on right button press; the canvas decides on
    // release instead (see mouseReleaseEvent), so the press must not open a menu.
    event->accept();
}

bool CanvasView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Leave) {
        m_cursorInside = false;
        viewport()->update();
    }
    return QGraphicsView::viewportEvent(event);
}

void CanvasView::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    emit viewMoved();
}

void CanvasView::followGlobalPos(const QPoint &globalPos)
{
    const QPoint pos = viewport()->mapFromGlobal(globalPos);
    m_cursorInside = viewport()->rect().contains(pos);
    if (m_cursorInside)
        m_cursorViewPos = pos;
    updateCursor();
}

bool CanvasView::sceneAtGlobal(const QPoint &globalPos, QPointF &scenePos) const
{
    const QPoint pos = viewport()->mapFromGlobal(globalPos);
    if (!viewport()->rect().contains(pos))
        return false;
    scenePos = mapToScene(pos);
    return true;
}

void CanvasView::updateCursor()
{
    if (m_cursorInside) {
        const QPointF scenePos = mapToScene(m_cursorViewPos);
        Coord x = 0;
        Coord y = 0;
        if (tryMmToNm(scenePos.x(), x) && tryMmToNm(scenePos.y(), y)) {
            x = snapToGrid(x, m_gridStep);
            y = snapToGrid(y, m_gridStep);
            if (x != m_cursorX || y != m_cursorY) {
                m_cursorX = x;
                m_cursorY = y;
                emit cursorMoved(x, y);
            }
        }
    }
    viewport()->update();
}

} // namespace ppcb
