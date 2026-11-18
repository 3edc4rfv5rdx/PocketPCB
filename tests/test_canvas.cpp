#include "appsettings.h"
#include "canvasview.h"
#include "grid.h"

#include <QSignalSpy>
#include <QStandardPaths>
#include <QtTest>

#include <cstdlib>

using namespace ppcb;

namespace {

// Scroll bars move in whole pixels, so positions are compared with a tolerance.
const int PIXEL_TOLERANCE = 2;

bool isNear(const QPoint &a, const QPoint &b)
{
    return std::abs(a.x() - b.x()) <= PIXEL_TOLERANCE && std::abs(a.y() - b.y()) <= PIXEL_TOLERANCE;
}

void sendMouse(QWidget *widget, QEvent::Type type, const QPoint &pos, Qt::MouseButton button)
{
    QMouseEvent event(type, QPointF(pos), QPointF(widget->mapToGlobal(pos)), button,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void sendWheel(QWidget *widget, const QPoint &pos, int angleDelta)
{
    QWheelEvent event(QPointF(pos), QPointF(widget->mapToGlobal(pos)), QPoint(),
                      QPoint(0, angleDelta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                      false);
    QApplication::sendEvent(widget, &event);
}

const int WHEEL_NOTCH = 120;

} // namespace

class TestCanvas : public QObject
{
    Q_OBJECT

private:
    // Shows the view at a known size.
    static bool showView(CanvasView &view)
    {
        view.resize(800, 600);
        view.show();
        return QTest::qWaitForWindowExposed(&view);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        AppSettings::initApplication();
    }

    void initialState()
    {
        CanvasView view;
        QCOMPARE(view.gridStep(), DEFAULT_GRID_STEP);
        QCOMPARE(view.pixelsPerMm(), CanvasView::INITIAL_PIXELS_PER_MM);
    }

    void aNarrowerViewKeepsTheDrawingInPlace()
    {
        CanvasView view;
        QVERIFY(showView(view));
        const QPointF corner = view.mapToScene(QPoint(0, 0));
        // As a dock opening on the right takes width from the canvas.
        view.resize(500, 600);
        QTRY_COMPARE(view.viewport()->width(), 500);
        QCOMPARE(view.mapToScene(QPoint(0, 0)), corner);
    }

    void zoomCenterAndWarpCentresThePoint()
    {
        CanvasView view;
        QVERIFY(showView(view));
        view.setZoomCenterAndWarp(true);

        const QPoint mouse(200, 150);
        const QPointF scenePos = view.mapToScene(mouse);
        sendWheel(view.viewport(), mouse, WHEEL_NOTCH);

        QCOMPARE(view.pixelsPerMm(), CanvasView::INITIAL_PIXELS_PER_MM * CanvasView::ZOOM_STEP);
        QVERIFY(isNear(view.mapFromScene(scenePos), view.viewport()->rect().center()));
    }

    void plainZoomKeepsThePointUnderTheMouse()
    {
        CanvasView view;
        QVERIFY(showView(view));
        view.setZoomCenterAndWarp(false);

        const QPoint mouse(200, 150);
        const QPointF scenePos = view.mapToScene(mouse);
        sendWheel(view.viewport(), mouse, WHEEL_NOTCH);
        QCOMPARE(view.pixelsPerMm(), CanvasView::INITIAL_PIXELS_PER_MM * CanvasView::ZOOM_STEP);
        QVERIFY(isNear(view.mapFromScene(scenePos), mouse));

        sendWheel(view.viewport(), mouse, -2 * WHEEL_NOTCH);
        QCOMPARE(view.pixelsPerMm(), CanvasView::INITIAL_PIXELS_PER_MM / CanvasView::ZOOM_STEP);
        QVERIFY(isNear(view.mapFromScene(scenePos), mouse));
    }

    void zoomStopsAtTheLimits()
    {
        CanvasView view;
        QVERIFY(showView(view));
        const QPoint mouse(400, 300);

        for (int i = 0; i < 100; ++i)
            sendWheel(view.viewport(), mouse, WHEEL_NOTCH);
        QCOMPARE(view.pixelsPerMm(), CanvasView::MAX_PIXELS_PER_MM);

        for (int i = 0; i < 100; ++i)
            sendWheel(view.viewport(), mouse, -WHEEL_NOTCH);
        QCOMPARE(view.pixelsPerMm(), CanvasView::MIN_PIXELS_PER_MM);
    }

    void shortRightClickRequestsTheContextMenu()
    {
        CanvasView view;
        QVERIFY(showView(view));
        QSignalSpy menuSpy(&view, &CanvasView::contextMenuRequested);

        const QPoint pos(400, 300);
        sendMouse(view.viewport(), QEvent::MouseButtonPress, pos, Qt::RightButton);
        sendMouse(view.viewport(), QEvent::MouseMove, pos + QPoint(1, 1), Qt::NoButton); // hand jitter
        sendMouse(view.viewport(), QEvent::MouseButtonRelease, pos + QPoint(1, 1), Qt::RightButton);
        QCOMPARE(menuSpy.count(), 1);
    }

    void rightDragPansWithoutContextMenu()
    {
        CanvasView view;
        QVERIFY(showView(view));
        QSignalSpy menuSpy(&view, &CanvasView::contextMenuRequested);

        const QPoint from(400, 300);
        const QPoint to(340, 270);
        const QPointF grabbed = view.mapToScene(from);
        sendMouse(view.viewport(), QEvent::MouseButtonPress, from, Qt::RightButton);
        sendMouse(view.viewport(), QEvent::MouseMove, to, Qt::NoButton);
        sendMouse(view.viewport(), QEvent::MouseButtonRelease, to, Qt::RightButton);

        // The grabbed point of the scene followed the mouse.
        QVERIFY(isNear(view.mapFromScene(grabbed), to));
        QCOMPARE(menuSpy.count(), 0);

        // Panning has ended: a further move must not scroll.
        sendMouse(view.viewport(), QEvent::MouseMove, from, Qt::NoButton);
        QVERIFY(isNear(view.mapFromScene(grabbed), to));
    }

    void middleButtonPansAndNeverRequestsTheMenu()
    {
        CanvasView view;
        QVERIFY(showView(view));
        QSignalSpy menuSpy(&view, &CanvasView::contextMenuRequested);

        const QPoint from(400, 300);
        const QPoint to(450, 330);
        const QPointF grabbed = view.mapToScene(from);
        sendMouse(view.viewport(), QEvent::MouseButtonPress, from, Qt::MiddleButton);
        sendMouse(view.viewport(), QEvent::MouseMove, to, Qt::NoButton);
        sendMouse(view.viewport(), QEvent::MouseButtonRelease, to, Qt::MiddleButton);
        QVERIFY(isNear(view.mapFromScene(grabbed), to));

        // A short middle click is not a context menu either.
        sendMouse(view.viewport(), QEvent::MouseButtonPress, from, Qt::MiddleButton);
        sendMouse(view.viewport(), QEvent::MouseButtonRelease, from, Qt::MiddleButton);
        QCOMPARE(menuSpy.count(), 0);
    }

    void cursorIsReportedSnappedToTheGrid()
    {
        CanvasView view;
        QVERIFY(showView(view));
        QSignalSpy cursorSpy(&view, &CanvasView::cursorMoved);

        // The mouse goes to where a known grid node is on screen; one pixel
        // is far less than half a grid step, so the node itself must come back.
        const Coord nodeX = -20 * view.gridStep();
        const Coord nodeY = -15 * view.gridStep();
        const QPoint mouse = view.mapFromScene(QPointF(nmToMm(nodeX), nmToMm(nodeY)));
        QVERIFY(view.viewport()->rect().contains(mouse));

        sendMouse(view.viewport(), QEvent::MouseMove, mouse, Qt::NoButton);
        QCOMPARE(cursorSpy.count(), 1);
        QCOMPARE(Coord(cursorSpy.at(0).at(0).toLongLong()), nodeX);
        QCOMPARE(Coord(cursorSpy.at(0).at(1).toLongLong()), nodeY);

        // A move that stays nearest to the same node is not reported again.
        sendMouse(view.viewport(), QEvent::MouseMove, mouse + QPoint(1, 1), Qt::NoButton);
        QCOMPARE(cursorSpy.count(), 1);

        // A coarser grid moves the snapped position without any mouse move.
        view.setGridStep(mmToNm(10.16));
        QCOMPARE(cursorSpy.count(), 2);
        QCOMPARE(Coord(cursorSpy.at(1).at(0).toLongLong()), -5 * mmToNm(10.16));
        QCOMPARE(Coord(cursorSpy.at(1).at(1).toLongLong()), -4 * mmToNm(10.16));
    }

    void gridStepChanges()
    {
        CanvasView view;
        QSignalSpy stepSpy(&view, &CanvasView::gridStepChanged);

        view.setGridStep(0);
        view.setGridStep(-5);
        view.setGridStep(DEFAULT_GRID_STEP); // already set
        QCOMPARE(stepSpy.count(), 0);
        QCOMPARE(view.gridStep(), DEFAULT_GRID_STEP);

        view.setGridStep(mmToNm(1.27));
        QCOMPARE(stepSpy.count(), 1);
        QCOMPARE(view.gridStep(), mmToNm(1.27));
    }
};

QTEST_MAIN(TestCanvas)
#include "test_canvas.moc"
