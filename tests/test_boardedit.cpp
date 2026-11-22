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

    void holesAreFoundAtAPointAndInsideARectangle()
    {
        Board board;
        board.holes = {newHole(mm(0, 0), 3 * MM), newHole(mm(20, 0), MM / 2)};
        // Plated, the first takes its ring too: 3 + 2 * 1 mm across.
        board = withHolesPlated(board, {board.holes.at(0).id}, true);
        const ObjectId big = board.holes.at(0).id;
        const ObjectId small = board.holes.at(1).id;
        QCOMPARE(holeAt(board, QPointF(2.4, 0), 0.1), big);
        QVERIFY(holeAt(board, QPointF(2.6, 0), 0.1).isNull());
        // A small hole is found within the reach of its middle.
        QCOMPARE(holeAt(board, QPointF(20.8, 0), 1.0), small);
        QVERIFY(holeAt(board, QPointF(20.8, 0), 0.1).isNull());
        // The one placed last of two on one place.
        board.holes.append(newHole(mm(0, 0), MM));
        QCOMPARE(holeAt(board, QPointF(0, 0), 0.1), board.holes.at(2).id);
        // Only what lies wholly inside, ring included.
        QCOMPARE(holesInside(board, QRectF(-2.6, -2.6, 5.2, 5.2)),
                 QSet<ObjectId>({big, board.holes.at(2).id}));
        QCOMPARE(holesInside(board, QRectF(-2.4, -2.4, 4.8, 4.8)),
                 QSet<ObjectId>{board.holes.at(2).id});
    }

    void holesAreEdited()
    {
        Board board;
        board.holes = {newHole(mm(0, 0), 3 * MM), newHole(mm(10, 0), 3 * MM)};
        const ObjectId first = board.holes.at(0).id;
        const ObjectId second = board.holes.at(1).id;
        QVERIFY(first != second);
        QCOMPARE(board.holes.at(0).plated, false);
        QCOMPARE(board.holes.at(0).ring, Coord(0));

        const Board moved = withHolesMoved(board, {first}, mm(1, 2));
        QCOMPARE(moved.holes.at(0).at, mm(1, 2));
        QCOMPARE(moved.holes.at(1).at, mm(10, 0));

        const Board drilled = withHolesDrill(board, {second}, 4 * MM);
        QCOMPARE(drilled.holes.at(0).drill, 3 * MM);
        QCOMPARE(drilled.holes.at(1).drill, 4 * MM);

        // Plating gives a ring to a hole without one and keeps one already set.
        Board plated = withHolesPlated(board, {first}, true);
        QCOMPARE(plated.holes.at(0).ring, DEFAULT_HOLE_RING);
        QVERIFY(areHolesPlated(plated, {first}));
        QVERIFY(!areHolesPlated(plated, {first, second}));
        QVERIFY(!areHolesPlated(plated, {}));
        // A pad goes on any hole, plated or not: 4 mm around 3 mm.
        plated = withHolesPad(plated, {first, second}, 4 * MM);
        QCOMPARE(plated.holes.at(0).ring, MM / 2);
        QCOMPARE(plated.holes.at(1).ring, MM / 2);
        QCOMPARE(withHolesPlated(plated, {first}, true).holes.at(0).ring, MM / 2);
        // Plating taken off keeps the ring and its net.
        plated.holes[0].net = QStringLiteral("GND");
        const Board unplated = withHolesPlated(plated, {first}, false);
        QVERIFY(!unplated.holes.at(0).plated);
        QCOMPARE(unplated.holes.at(0).ring, MM / 2);
        QCOMPARE(unplated.holes.at(0).net, QStringLiteral("GND"));
        // A pad of 0 goes off a hole without plating, with its net, and
        // stays on a plated one.
        Board ringless = withHolesPad(unplated, {first, second}, 0);
        QCOMPARE(ringless, board);
        ringless = withHolesPad(plated, {first, second}, 0);
        QCOMPARE(ringless.holes.at(0), plated.holes.at(0));
        QCOMPARE(ringless.holes.at(1).ring, Coord(0));
        // A pad not larger than the hole leaves it as it was.
        QVERIFY(!takesPad(plated.holes.at(1), 3 * MM));
        QVERIFY(!takesPad(plated.holes.at(1), 3 * MM + 1));
        QCOMPARE(withHolesPad(plated, {first, second}, 3 * MM), plated);
        QCOMPARE(padRing(plated.holes.at(1), 5 * MM), MM);

        const Board fewer = withoutHoles(board, {first});
        QCOMPARE(fewer.holes.size(), 1);
        QCOMPARE(fewer.holes.at(0).id, second);
    }

    void padSidesAndNets()
    {
        Board board;
        board.holes = {newHole(mm(0, 0), 3 * MM), newHole(mm(10, 0), 3 * MM)};
        const ObjectId first = board.holes.at(0).id;
        const ObjectId second = board.holes.at(1).id;
        const QSet<ObjectId> both = {first, second};
        // Only a hole with a pad takes a net.
        board = withHolesPad(board, {first}, 5 * MM);
        Board netted = withHolesNet(board, both, QStringLiteral("GND"));
        QCOMPARE(netted.holes.at(0).net, QStringLiteral("GND"));
        QCOMPARE(netted.holes.at(1).net, QString());
        // The side goes on a hole without plating; plating puts it back on both.
        Board sided = withHolesPadSides(netted, both, CopperSides::Bottom);
        QVERIFY(sided.holes.at(0).padSides == CopperSides::Bottom);
        sided = withHolesPlated(sided, {first}, true);
        QVERIFY(sided.holes.at(0).padSides == CopperSides::Both);
        QCOMPARE(withHolesPadSides(sided, {first}, CopperSides::Top), sided);
        // A locked hole keeps them.
        const Board locked = withHolesLocked(netted, {first}, true);
        QCOMPARE(withHolesNet(locked, both, QString()), locked);
        QCOMPARE(withHolesPadSides(locked, {first}, CopperSides::Top), locked);
    }

    void lockedHolesStay()
    {
        Board board;
        board.holes = {newHole(mm(0, 0), 3 * MM), newHole(mm(10, 0), 3 * MM)};
        const ObjectId first = board.holes.at(0).id;
        const ObjectId second = board.holes.at(1).id;
        const QSet<ObjectId> both = {first, second};
        QVERIFY(!areHolesLocked(board, both));
        const Board locked = withHolesLocked(board, {first}, true);
        QVERIFY(areHolesLocked(locked, {first}));
        QVERIFY(!areHolesLocked(locked, both));
        QVERIFY(!areHolesLocked(locked, {}));

        // Every change passes the locked one by and reaches the other.
        const Board moved = withHolesMoved(locked, both, mm(1, 1));
        QCOMPARE(moved.holes.at(0), locked.holes.at(0));
        QCOMPARE(moved.holes.at(1).at, mm(11, 1));
        const Board drilled = withHolesDrill(locked, both, 4 * MM);
        QCOMPARE(drilled.holes.at(0), locked.holes.at(0));
        QCOMPARE(drilled.holes.at(1).drill, 4 * MM);
        const Board plated = withHolesPad(withHolesPlated(locked, both, true), both, 4 * MM);
        QCOMPARE(plated.holes.at(0), locked.holes.at(0));
        QCOMPARE(plated.holes.at(1).ring, MM / 2);
        const Board fewer = withoutHoles(locked, both);
        QCOMPARE(fewer.holes.size(), 1);
        QCOMPARE(fewer.holes.at(0).id, first);
        QCOMPARE(withHolesLocked(locked, {first}, false), board);
    }
};

QTEST_APPLESS_MAIN(TestBoardEdit)
#include "test_boardedit.moc"
