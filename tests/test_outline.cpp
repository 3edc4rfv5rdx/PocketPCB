#include "outline.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(double x, double y)
{
    return Point{mmToNm(x), mmToNm(y)};
}

QList<Point> pointsOf(const QList<OutlineCorner> &outline)
{
    QList<Point> points;
    for (const OutlineCorner &corner : outline)
        points.append(corner.at);
    return points;
}

QList<OutlineCorner> outlineOf(const QList<Point> &points)
{
    QList<OutlineCorner> outline;
    for (const Point &point : points)
        outline.append(OutlineCorner{point, 0});
    return outline;
}

bool onGrid(const Point &point, Coord grid)
{
    return point.x % grid == 0 && point.y % grid == 0;
}

} // namespace

class TestOutline : public QObject
{
    Q_OBJECT

private slots:
    void problems()
    {
        QCOMPARE(outlineProblem(rectangleOutline(mm(0, 0), 50 * MM, 30 * MM)),
                 OutlineProblem::None);
        QCOMPARE(outlineProblem(outlineOf({mm(0, 0), mm(10, 0)})), OutlineProblem::TooFewCorners);
        QCOMPARE(outlineProblem(outlineOf({mm(0, 0), mm(10, 0), mm(10, 0), mm(10, 10)})),
                 OutlineProblem::ZeroSide);
        // Any angle will do.
        QCOMPARE(outlineProblem(outlineOf({mm(0, 0), mm(10, 0), mm(11, 3), mm(0, 3)})),
                 OutlineProblem::None);
        // A bow tie.
        QCOMPARE(outlineProblem(outlineOf({mm(0, 0), mm(10, 10), mm(10, 0), mm(0, 10)})),
                 OutlineProblem::Crossing);
        // A side turning back over the one before.
        QCOMPARE(outlineProblem(outlineOf({mm(0, 0), mm(10, 0), mm(5, 0), mm(5, 5)})),
                 OutlineProblem::Crossing);
        // A notch whose bottom touches the far side.
        QCOMPARE(outlineProblem(outlineOf({mm(0, 0), mm(4, 0), mm(4, 10), mm(6, 10), mm(6, 0),
                                           mm(10, 0), mm(10, 10), mm(0, 10)})),
                 OutlineProblem::Crossing);
    }

    void cornerMoved()
    {
        QList<OutlineCorner> board = rectangleOutline(mm(0, 0), 50 * MM, 30 * MM);
        board[2].radius = 2 * MM;
        std::optional<QList<OutlineCorner>> moved = withOutlineCornerMoved(board, 2, mm(43, 35));
        QVERIFY(moved);
        QCOMPARE(moved->at(2).at, mm(43, 35));
        // Radii stay.
        QCOMPARE(moved->at(2).radius, 2 * MM);
        // Over the far side: the outline would cross itself.
        QVERIFY(!withOutlineCornerMoved(board, 2, mm(-10, 15)));
        // Onto the next corner: a side of no length.
        QVERIFY(!withOutlineCornerMoved(board, 2, mm(0, 30)));
    }

    void sideMoved()
    {
        // A notch in the top: the bottom of the notch made deeper.
        const QList<OutlineCorner> notched = outlineOf({mm(0, 0), mm(20, 0), mm(20, 5), mm(30, 5),
                                                        mm(30, 0), mm(50, 0), mm(50, 30),
                                                        mm(0, 30)});
        std::optional<QList<OutlineCorner>> moved = withOutlineSideMoved(notched, 2, mm(3, 5));
        QVERIFY(moved);
        // A level side goes only down: it moved 5 mm, not sideways.
        QCOMPARE(pointsOf(*moved), (QList<Point>{mm(0, 0), mm(20, 0), mm(20, 10), mm(30, 10),
                                                 mm(30, 0), mm(50, 0), mm(50, 30), mm(0, 30)}));
        // Pulled up beyond the top edge, the sides beside it would turn round.
        QVERIFY(!withOutlineSideMoved(notched, 2, mm(0, -6)));
        // Down onto the bottom edge, it would touch it.
        QVERIFY(!withOutlineSideMoved(notched, 2, mm(0, 25)));
        // Nothing across it: unchanged.
        moved = withOutlineSideMoved(notched, 2, mm(4, 0));
        QVERIFY(moved);
        QCOMPARE(*moved, notched);

        // An upright side beside a slanted one goes only sideways: its
        // corners keep their heights, the slanted side stretches (the
        // step the author dragged: the corner slid down the slant).
        const QList<OutlineCorner> slanted =
            outlineOf({mm(0, 0), mm(50.8, 0), mm(50.8, 11.43), mm(34.29, 7.62), mm(0, 7.62)});
        moved = withOutlineSideMoved(slanted, 1, mm(45.72, 2.54));
        QVERIFY(moved);
        QCOMPARE(moved->at(1).at, mm(96.52, 0));
        QCOMPARE(moved->at(2).at, mm(96.52, 11.43));

        // A slanted side follows the cursor along the grid.
        const QList<OutlineCorner> chamfered =
            outlineOf({mm(0, 0), mm(40, 0), mm(50, 10), mm(50, 30), mm(0, 30)});
        moved = withOutlineSideMoved(chamfered, 1, mm(-5, 5));
        QVERIFY(moved);
        QCOMPARE(pointsOf(*moved),
                 (QList<Point>{mm(0, 0), mm(35, 5), mm(45, 15), mm(50, 30), mm(0, 30)}));
    }

    void cut()
    {
        const QList<OutlineCorner> board = rectangleOutline(mm(0, 0), 50 * MM, 30 * MM);
        // A notch in the top edge, the rectangle reaching beyond it.
        OutlineCut cut = withOutlineCut(board, mm(20, -5), mm(30, 10), MM);
        QCOMPARE(cut.problem, OutlineCutProblem::None);
        QCOMPARE(pointsOf(cut.outline),
                 (QList<Point>{mm(0, 0), mm(20, 0), mm(20, 10), mm(30, 10), mm(30, 0), mm(50, 0),
                               mm(50, 30), mm(0, 30)}));
        // A corner cut off; the radius of a corner that stays is kept.
        QList<OutlineCorner> rounded = board;
        rounded[3].radius = 2 * MM;
        cut = withOutlineCut(rounded, mm(60, 40), mm(40, 20), MM);
        QCOMPARE(cut.problem, OutlineCutProblem::None);
        QCOMPARE(pointsOf(cut.outline), (QList<Point>{mm(0, 0), mm(50, 0), mm(50, 20), mm(40, 20),
                                                      mm(40, 30), mm(0, 30)}));
        QCOMPARE(cut.outline.at(5).radius, 2 * MM);
        QCOMPARE(cut.outline.at(3).radius, Coord(0));

        // A rectangle off the board, one inside it, one across it, one over it.
        QCOMPARE(withOutlineCut(board, mm(60, 0), mm(70, 10), MM).problem,
                 OutlineCutProblem::Misses);
        QCOMPARE(withOutlineCut(board, mm(10, 10), mm(20, 20), MM).problem,
                 OutlineCutProblem::Inside);
        QCOMPARE(withOutlineCut(board, mm(20, -5), mm(30, 35), MM).problem,
                 OutlineCutProblem::Splits);
        QCOMPARE(withOutlineCut(board, mm(-5, -5), mm(55, 35), MM).problem,
                 OutlineCutProblem::Splits);
        QCOMPARE(withOutlineCut(board, mm(10, 10), mm(10, 20), MM).problem,
                 OutlineCutProblem::Misses);
        // A problem leaves the outline as it was.
        QCOMPARE(withOutlineCut(board, mm(10, 10), mm(20, 20), MM).outline, board);

        // A cut across a slanted side: the corners it makes go to the grid.
        const QList<OutlineCorner> slanted = outlineOf({mm(0, 0), mm(40, 23), mm(0, 46)});
        QCOMPARE(outlineProblem(slanted), OutlineProblem::None);
        cut = withOutlineCut(slanted, mm(10, -10), mm(20, 8), MM);
        QCOMPARE(cut.problem, OutlineCutProblem::None);
        QCOMPARE(outlineProblem(cut.outline), OutlineProblem::None);
        for (const OutlineCorner &corner : cut.outline)
            QVERIFY(onGrid(corner.at, MM));
    }

    void hits()
    {
        const QList<OutlineCorner> board = rectangleOutline(mm(0, 0), 50 * MM, 30 * MM);
        QCOMPARE(outlineCornerAt(board, QPointF(49.5, 30.2), 1), 2);
        QCOMPARE(outlineCornerAt(board, QPointF(25, 15), 1), -1);
        QCOMPARE(outlineSideAt(board, QPointF(25, 29.6), 1), 2);
        QCOMPARE(outlineSideAt(board, QPointF(-0.5, 15), 1), 3);
        QCOMPARE(outlineSideAt(board, QPointF(25, 15), 1), -1);
    }
};

QTEST_APPLESS_MAIN(TestOutline)
#include "test_outline.moc"
