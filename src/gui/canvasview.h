#pragma once

#include "appsettings.h"
#include "units.h"

#include <QGraphicsView>

namespace ppcb {

// The editor canvas: grid, wheel zoom, panning and the grid cursor.
// Scene coordinates are millimetres (double); everything this class reports
// to the outside is in nanometres.
class CanvasView : public QGraphicsView
{
    Q_OBJECT

public:
    // One wheel notch changes the scale by this factor.
    static constexpr double ZOOM_STEP = 1.25;
    // Scale limits and the scale at start, in screen pixels per millimetre.
    static constexpr double MIN_PIXELS_PER_MM = 0.5;
    static constexpr double MAX_PIXELS_PER_MM = 500.0;
    static constexpr double INITIAL_PIXELS_PER_MM = 4.0;

    explicit CanvasView(QWidget *parent = nullptr);

    Coord gridStep() const { return m_gridStep; }
    GridStyle gridStyle() const { return m_gridStyle; }
    CursorStyle cursorStyle() const { return m_cursorStyle; }
    bool zoomCenterAndWarp() const { return m_zoomCenterAndWarp; }
    double pixelsPerMm() const;

    // Ignores a step that is not positive.
    void setGridStep(ppcb::Coord step);
    void setGridStyle(ppcb::GridStyle style);
    void setCursorStyle(ppcb::CursorStyle style);
    void setZoomCenterAndWarp(bool on);

    // Zooms and scrolls so that 'rect' (scene millimetres) fills most of the
    // view. Does nothing for an empty rectangle.
    void fitRect(const QRectF &rect);

signals:
    void gridStepChanged(ppcb::Coord step);
    // Cursor position snapped to the grid.
    void cursorMoved(ppcb::Coord x, ppcb::Coord y);
    // A short right click without movement.
    void contextMenuRequested(const QPoint &globalPos);

protected:
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool viewportEvent(QEvent *event) override;

private:
    void zoomAt(const QPoint &viewPos, double factor);
    void scrollBy(const QPoint &pixels);
    // Recomputes the snapped cursor position from the last known mouse
    // position; call after anything that moves the scene under the mouse.
    void updateCursor();

    Coord m_gridStep;
    GridStyle m_gridStyle = GridStyle::Lines;
    CursorStyle m_cursorStyle = CursorStyle::SmallCross;
    bool m_zoomCenterAndWarp = true;

    // Mouse position in viewport pixels; valid while m_cursorInside.
    QPoint m_cursorViewPos;
    bool m_cursorInside = false;
    // Snapped cursor position; valid while m_cursorInside.
    Coord m_cursorX = 0;
    Coord m_cursorY = 0;

    // Panning: the button that started it, or Qt::NoButton.
    Qt::MouseButton m_panButton = Qt::NoButton;
    QPoint m_panStartPos;
    QPoint m_panLastPos;
    // Set once the mouse has travelled far enough to be a drag, not a click.
    bool m_panMoved = false;
};

} // namespace ppcb
