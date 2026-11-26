#include "graphicedit.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

} // namespace

class TestGraphicEdit : public QObject
{
    Q_OBJECT

private slots:
    void drawingsAreFoundByTheirLineOrTheirArea()
    {
        const QList<Graphic> graphics = {lineBetween(mm(0, 0), mm(10, 0)),
                                         rectBetween(mm(0, 5), mm(10, 10)),
                                         textAt(mm(20, 0), QStringLiteral("A"))};
        QCOMPARE(graphicAt(graphics, QPointF(5, 0.15), 0.1), 0);
        // Inside a rectangle that is not filled: nothing.
        QCOMPARE(graphicAt(graphics, QPointF(5, 7.5), 0.1), -1);
        QCOMPARE(graphicAt(graphics, QPointF(10, 7), 0.1), 1);
        QCOMPARE(graphicAt(graphics, QPointF(20, 0), 0.1), 2);
        QCOMPARE(graphicsInside(graphics, QRectF(-1, -1, 12, 2)), QSet<int>({0}));
        // A line across has no height, and still counts.
        const QRectF bounds = graphicsBounds(graphics, {0, 1});
        QVERIFY(bounds.contains(QPointF(0, 0)) && bounds.contains(QPointF(10, 10)));
        QVERIFY(graphicsBounds(graphics, {}).isNull());
    }

    void drawingsMoveTurnAndGo()
    {
        Graphic arc = arcThrough(mm(0, 0), mm(5, 0), mm(0, 5));
        QCOMPARE(arc.radius, 5 * MM);
        QCOMPARE(arc.startAngle, 0);
        QCOMPARE(arc.spanAngle, 90);
        const QList<Graphic> graphics = {lineBetween(mm(1, 0), mm(3, 0)), arc};
        const QList<Graphic> moved = withGraphicsMoved(graphics, {0}, mm(1, 1));
        QCOMPARE(moved.at(0).a, mm(2, 1));
        QCOMPARE(moved.at(1), arc);
        const QList<Graphic> turned = withGraphicsRotated(graphics, {0, 1}, mm(0, 0));
        // Right of the centre goes below it.
        QCOMPARE(turned.at(0).a, mm(0, 1));
        QCOMPARE(turned.at(0).b, mm(0, 3));
        QCOMPARE(turned.at(1).startAngle, 90);
        QCOMPARE(withoutGraphics(graphics, {0}), QList<Graphic>({arc}));
    }

    void cornersAreDraggedOneByOne()
    {
        const Graphic rect = rectBetween(mm(0, 0), mm(10, 5));
        QCOMPARE(graphicCorners(rect),
                 QList<Point>({mm(0, 0), mm(10, 0), mm(10, 5), mm(0, 5)}));
        QCOMPARE(cornerAt(rect, QPointF(9.8, 0.1), 0.5), 1);
        QCOMPARE(cornerAt(rect, QPointF(5, 0), 0.5), -1);
        const Graphic moved = withCornerMoved(rect, 1, mm(12, -2));
        // The corner across from it stays.
        QCOMPARE(moved.a, mm(0, -2));
        QCOMPARE(moved.b, mm(12, 5));
        const Graphic line = withCornerMoved(lineBetween(mm(0, 0), mm(1, 1)), 1, mm(4, 4));
        QCOMPARE(line.b, mm(4, 4));
        QVERIFY(graphicCorners(circleThrough(mm(0, 0), mm(3, 4))).isEmpty());
        QCOMPARE(circleThrough(mm(0, 0), mm(3, 4)).radius, 5 * MM);
    }

    void linesKeepToEightWays()
    {
        QCOMPARE(keptStraightOrDiagonal(mm(0, 0), mm(10, 2)), mm(10, 0));
        QCOMPARE(keptStraightOrDiagonal(mm(0, 0), mm(-2, 10)), mm(0, 10));
        QCOMPARE(keptStraightOrDiagonal(mm(0, 0), mm(4, -6)), mm(5, -5));
        QCOMPARE(keptStraightOrDiagonal(mm(1, 1), mm(1, 1)), mm(1, 1));
    }

    void anArcEndingOnItsStartGoesRound()
    {
        const Graphic arc = arcThrough(mm(0, 0), mm(0, -5), mm(0, -3));
        QCOMPARE(arc.startAngle, 270);
        QCOMPARE(arc.spanAngle, 360);
        // Back the other way: three quarters clockwise.
        QCOMPARE(arcThrough(mm(0, 0), mm(5, 0), mm(0, -5)).spanAngle, 270);
    }
};

QTEST_APPLESS_MAIN(TestGraphicEdit)
#include "test_graphicedit.moc"
