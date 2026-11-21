#include "boardedit.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

// A footprint "fp" with pads at (-5, 0) and (5, 0) mm, 2 mm wide.
Library library()
{
    Footprint footprint;
    Pad pad;
    pad.number = QStringLiteral("1");
    pad.at = mm(-5, 0);
    pad.sizeX = 2 * MM;
    pad.sizeY = 2 * MM;
    pad.hole = MM;
    footprint.pads.append(pad);
    pad.number = QStringLiteral("2");
    pad.at = mm(5, 0);
    footprint.pads.append(pad);
    Library result;
    result.footprints.insert(QStringLiteral("fp"), footprint);
    return result;
}

Placement placement(const QString &designator, const Point &at)
{
    Placement result;
    result.id = newObjectId();
    result.component = newObjectId();
    result.footprint = QStringLiteral("fp");
    result.designator = designator;
    result.value = QStringLiteral("1k");
    result.at = at;
    result.refAt = mm(0, -3);
    result.valueAt = mm(0, 3);
    return result;
}

// R1 at (0, 0) and R2 at (20, 10).
Board twoPlacements()
{
    Board board;
    board.placements = {placement(QStringLiteral("R1"), mm(0, 0)),
                        placement(QStringLiteral("R2"), mm(20, 10))};
    return board;
}

} // namespace

class TestBoardEdit : public QObject
{
    Q_OBJECT

private slots:
    void placementsAreFoundAtAPointAndInsideARectangle()
    {
        const Board board = twoPlacements();
        const ObjectId r1 = board.placements.at(0).id;
        const ObjectId r2 = board.placements.at(1).id;
        QCOMPARE(placementAt(board, library(), QPointF(5, 0)), r1);
        QCOMPARE(placementAt(board, library(), QPointF(22, 10)), r2);
        QVERIFY(placementAt(board, library(), QPointF(10, 5)).isNull());
        QCOMPARE(placementsInside(board, library(), QRectF(-10, -5, 20, 10)), QSet<ObjectId>{r1});
        // Only what lies wholly inside.
        QCOMPARE(placementsInside(board, library(), QRectF(0, -5, 40, 20)), QSet<ObjectId>{r2});
        const QRectF bounds = placementsBounds(board, library(), {r1, r2});
        QVERIFY(bounds.contains(QPointF(-6, 0)) && bounds.contains(QPointF(26, 10)));
        QVERIFY(placementsBounds(board, library(), {}).isNull());
    }

    void theDesignatorAndTheValueAreFoundWhereTheyAreShown()
    {
        Board board = twoPlacements();
        const ObjectId r1 = board.placements.at(0).id;
        const PlacementText designator{r1, ComponentText::Designator};
        QCOMPARE(placementTextAt(board, QPointF(0, -3)), designator);
        // The value is not shown, so it is not found.
        QVERIFY(placementTextAt(board, QPointF(0, 3)).isNull());
        board.placements[0].showValue = true;
        QCOMPARE(placementTextAt(board, QPointF(0, 3)),
                 (PlacementText{r1, ComponentText::Value}));
        QCOMPARE(placementTextPoint(board, designator), mm(0, -3));
    }

    void placementsAreMovedAndTurnedWithTheirTexts()
    {
        const Board board = twoPlacements();
        const ObjectId r2 = board.placements.at(1).id;
        const Board moved = withPlacementsMoved(board, {r2}, mm(5, -5));
        QCOMPARE(moved.placements.at(0), board.placements.at(0));
        QCOMPARE(moved.placements.at(1).at, mm(25, 5));
        QCOMPARE(moved.placements.at(1).refAt, mm(0, -3));

        // A quarter turn clockwise about R1's point: R2, right of and
        // below it, goes below and left; the text above goes to the right.
        const Board turned = withPlacementsRotated(board, {r2}, mm(0, 0));
        QCOMPARE(turned.placements.at(1).at, mm(-10, 20));
        QCOMPARE(turned.placements.at(1).rotation, 90);
        QCOMPARE(turned.placements.at(1).refAt, mm(3, 0));
        QCOMPARE(turned.placements.at(1).valueAt, mm(-3, 0));
        Board round = board;
        for (int i = 0; i < 4; ++i)
            round = withPlacementsRotated(round, {r2}, mm(7, 3));
        QCOMPARE(round, board);
    }

    // R1's designator, at (0, -3), moved up to (0, -8): with its line, the
    // silkscreen joins the bottom of the text to the part's point.
    void aTextWithItsLineIsJoinedToThePart()
    {
        Placement r1 = twoPlacements().placements.at(0);
        r1.refAt = mm(0, -8);
        QVERIFY(leaderLine(r1, ComponentText::Designator).isNull());
        r1.refLine = true;
        const QLineF line = leaderLine(r1, ComponentText::Designator);
        QVERIFY(!line.isNull());
        QCOMPARE(line.p2(), QPointF(0, 0));
        QCOMPARE(line.p1(), QPointF(0, placementTextRect(r1, ComponentText::Designator).bottom()));
        // On the part, or hidden: no line.
        r1.refAt = mm(0, 0);
        QVERIFY(leaderLine(r1, ComponentText::Designator).isNull());
        r1.refAt = mm(0, -8);
        r1.showRef = false;
        QVERIFY(leaderLine(r1, ComponentText::Designator).isNull());
    }

    void aLockedPlacementStaysButItsTextMoves()
    {
        const Board board = twoPlacements();
        const ObjectId r1 = board.placements.at(0).id;
        const ObjectId r2 = board.placements.at(1).id;
        QVERIFY(!arePlacementsLocked(board, {r1}));
        const Board locked = withPlacementsLocked(board, {r1}, true);
        QVERIFY(arePlacementsLocked(locked, {r1}));
        QVERIFY(!arePlacementsLocked(locked, {r1, r2}));
        QVERIFY(!arePlacementsLocked(locked, {}));
        const QSet<ObjectId> both = {r1, r2};
        const Board moved = withPlacementsMoved(locked, both, mm(1, 1));
        QCOMPARE(moved.placements.at(0).at, mm(0, 0));
        QCOMPARE(moved.placements.at(1).at, mm(21, 11));
        const Board turned = withPlacementsRotated(locked, both, mm(0, 0));
        QCOMPARE(turned.placements.at(0), locked.placements.at(0));
        const Board text =
            withPlacementTextMoved(locked, PlacementText{r1, ComponentText::Designator}, mm(2, 0));
        QCOMPARE(text.placements.at(0).refAt, mm(2, -3));
        QCOMPARE(text.placements.at(0).at, mm(0, 0));
        QCOMPARE(withPlacementsLocked(locked, {r1}, false), board);
    }
};

QTEST_APPLESS_MAIN(TestBoardEdit)
#include "test_boardedit.moc"
