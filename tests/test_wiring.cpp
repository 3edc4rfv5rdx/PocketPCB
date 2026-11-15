#include "sheetedit.h"
#include "wiring.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

// A symbol with pins at (-5, 0) and (5, 0) mm, wired from the left and from the right.
Library twoPinLibrary()
{
    Symbol symbol;
    Pin left;
    left.number = QStringLiteral("1");
    left.at = mm(-5, 0);
    left.side = PinSide::Left;
    Pin right = left;
    right.number = QStringLiteral("2");
    right.side = PinSide::Right;
    right.at = mm(5, 0);
    symbol.pins = {left, right};
    Library library;
    library.symbols.insert(QStringLiteral("two"), symbol);
    Part part;
    part.symbol = QStringLiteral("two");
    library.parts.insert(QStringLiteral("two"), part);
    return library;
}

Component twoPin(const Point &at, int rotation = 0, bool mirrored = false)
{
    Component component;
    component.id = newObjectId();
    component.part = QStringLiteral("two");
    component.at = at;
    component.rotation = rotation;
    component.mirrored = mirrored;
    return component;
}

Wire wire(const QList<Point> &points)
{
    Wire result;
    result.id = newObjectId();
    result.points = points;
    return result;
}

} // namespace

class TestWiring : public QObject
{
    Q_OBJECT

private slots:
    void pinsAreWhereTheComponentPutsThem()
    {
        const Point p = mm(3, 1);
        QCOMPARE(componentPoint(twoPin(mm(10, 10)), p), mm(13, 11));
        // A quarter turn clockwise on screen: right goes down.
        QCOMPARE(componentPoint(twoPin(mm(10, 10), 90), p), mm(9, 13));
        QCOMPARE(componentPoint(twoPin(mm(10, 10), 180), p), mm(7, 9));
        QCOMPARE(componentPoint(twoPin(mm(10, 10), 270), p), mm(11, 7));
        // Mirrored first, then turned.
        QCOMPARE(componentPoint(twoPin(mm(10, 10), 0, true), p), mm(7, 11));
        QCOMPARE(componentPoint(twoPin(mm(10, 10), 90, true), p), mm(9, 7));

        Sheet sheet;
        sheet.components = {twoPin(mm(10, 10), 90)};
        Component missing = twoPin(mm(50, 50));
        missing.part = QStringLiteral("absent");
        sheet.components.append(missing);
        QCOMPARE(pinPoints(sheet, twoPinLibrary()), QList<Point>({mm(10, 5), mm(10, 15)}));
    }

    void pointsOnWires()
    {
        const Wire w = wire({mm(0, 0), mm(10, 0), mm(15, 5)});
        QVERIFY(isOnWire(w, mm(0, 0)));
        QVERIFY(isOnWire(w, mm(4, 0)));
        QVERIFY(isOnWire(w, mm(12, 2)));
        QVERIFY(isOnWire(w, mm(15, 5)));
        QVERIFY(!isOnWire(w, mm(16, 6)));
        QVERIFY(!isOnWire(w, mm(-1, 0)));
        QVERIFY(!isOnWire(w, Point{4 * MM, 1}));

        Sheet sheet;
        sheet.components = {twoPin(mm(20, 20))};
        sheet.wires = {w};
        const Library library = twoPinLibrary();
        QVERIFY(isConnectionPoint(sheet, library, mm(15, 20)));
        QVERIFY(isConnectionPoint(sheet, library, mm(5, 0)));
        QVERIFY(!isConnectionPoint(sheet, library, mm(20, 20)));
    }

    void dotsWhereThreeMeet()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        // A wire from a pin: two things on the point, no dot.
        sheet.components = {twoPin(mm(20, 0))};
        sheet.wires = {wire({mm(0, 0), mm(15, 0)})};
        QVERIFY(junctions(sheet, library).isEmpty());

        // A wire ending on the middle of another: a dot there.
        sheet.wires.append(wire({mm(5, 10), mm(5, 0)}));
        QCOMPARE(junctions(sheet, library), QList<Point>({mm(5, 0)}));

        // Crossing is not connecting.
        sheet.wires.append(wire({mm(8, -5), mm(8, 5)}));
        QCOMPARE(junctions(sheet, library), QList<Point>({mm(5, 0)}));

        // Three ends on one point, and a second wire on the pin.
        sheet.wires.append(wire({mm(30, 0), mm(30, 10)}));
        sheet.wires.append(wire({mm(30, 10), mm(40, 10)}));
        sheet.wires.append(wire({mm(30, 10), mm(30, 20)}));
        sheet.wires.append(wire({mm(15, 0), mm(15, 8)}));
        const QList<Point> dots = junctions(sheet, library);
        QCOMPARE(dots.size(), 3);
        QVERIFY(dots.contains(mm(5, 0)));
        QVERIFY(dots.contains(mm(30, 10)));
        QVERIFY(dots.contains(mm(15, 0)));

        // A corner of a single wire is no junction.
        Sheet bent;
        bent.wires = {wire({mm(0, 0), mm(10, 0), mm(10, 10)})};
        QVERIFY(junctions(bent, library).isEmpty());
    }

    void routesTurnOnce()
    {
        const Point from = mm(0, 0);
        QCOMPARE(wireRoute(from, mm(10, 4), false, false), QList<Point>({mm(10, 0), mm(10, 4)}));
        QCOMPARE(wireRoute(from, mm(10, 4), true, false), QList<Point>({mm(0, 4), mm(10, 4)}));
        // Straight on: no corner.
        QCOMPARE(wireRoute(from, mm(10, 0), false, false), QList<Point>({mm(10, 0)}));
        QCOMPARE(wireRoute(from, mm(0, -7), false, false), QList<Point>({mm(0, -7)}));
        QVERIFY(wireRoute(from, from, false, false).isEmpty());
        // With a slant: straight then slanted, or slanted then straight.
        QCOMPARE(wireRoute(from, mm(10, 4), false, true), QList<Point>({mm(6, 0), mm(10, 4)}));
        QCOMPARE(wireRoute(from, mm(10, 4), true, true), QList<Point>({mm(4, 4), mm(10, 4)}));
        QCOMPARE(wireRoute(from, mm(-3, -9), false, true), QList<Point>({mm(0, -6), mm(-3, -9)}));
        QCOMPARE(wireRoute(from, mm(5, 5), false, true), QList<Point>({mm(5, 5)}));
    }

    void wiresAreSimplified()
    {
        QCOMPARE(simplifiedWire({mm(0, 0), mm(0, 0), mm(5, 0), mm(10, 0), mm(10, 5), mm(10, 5)}),
                 QList<Point>({mm(0, 0), mm(10, 0), mm(10, 5)}));
        // Going back is kept.
        QCOMPARE(simplifiedWire({mm(0, 0), mm(10, 0), mm(5, 0)}),
                 QList<Point>({mm(0, 0), mm(10, 0), mm(5, 0)}));
    }

    // A wire from (-20, 0) to the left pin of a part at the origin.
    void wiresFollowAMovedPart()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(-20, 0), mm(-5, 0)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const auto dragged = [&](const Point &delta) {
            return withWiresFollowing(sheet, movedObjects(sheet, part, delta), part, library)
                .wires;
        };

        // Across the wire: a bend.
        QCOMPARE(dragged(mm(0, 5)).first().points, QList<Point>({mm(-20, 0), mm(-5, 0), mm(-5, 5)}));
        // Along it: longer or shorter, still straight.
        QCOMPARE(dragged(mm(3, 0)).first().points, QList<Point>({mm(-20, 0), mm(-2, 0)}));
        QCOMPARE(dragged(mm(-3, 0)).first().points, QList<Point>({mm(-20, 0), mm(-8, 0)}));
        // Both ways: along the wire first, then across.
        QCOMPARE(dragged(mm(4, 6)).first().points, QList<Point>({mm(-20, 0), mm(-1, 0), mm(-1, 6)}));
        // Past the other end: the wire turns round and does not go back over itself.
        QCOMPARE(dragged(mm(-20, 0)).first().points, QList<Point>({mm(-20, 0), mm(-25, 0)}));
        // Onto the other end: no length left, so no wire.
        QVERIFY(dragged(mm(-15, 0)).isEmpty());
    }

    void wiresFollowOnlyWhatLiesOnTheMovedObjects()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        // Two parts joined pin to pin by a bent wire, and a wire hanging free.
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(20, 10))};
        sheet.wires = {wire({mm(5, 0), mm(15, 0), mm(15, 10)}), wire({mm(30, 30), mm(40, 30)})};
        QSet<ObjectId> both = {sheet.components.at(0).id, sheet.components.at(1).id};
        Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, both, mm(2, 3)), both, library);
        // Both ends went the same way: the whole wire goes along.
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(7, 3), mm(17, 3), mm(17, 13)}));
        QCOMPARE(moved.wires.at(1).points, sheet.wires.at(1).points);

        // The wire is moved itself: the parts stay, and bends lead its ends
        // back to their pins.
        const QSet<ObjectId> line = {sheet.wires.at(0).id};
        moved = withWiresFollowing(sheet, movedObjects(sheet, line, mm(2, 3)), line, library);
        // Halfway down to the pin, on the half grid: 11.43 mm.
        const Coord halfway = 11430000;
        QCOMPARE(moved.wires.at(0).points,
                 QList<Point>({mm(5, 0), mm(5, 3), mm(17, 3), Point{17 * MM, halfway},
                               Point{15 * MM, halfway}, mm(15, 10)}));
        QCOMPARE(moved.components, sheet.components);
    }

    // Two parts joined pin to pin by a wire, with another wire going down
    // from its middle. Both parts go up: the wire between them goes whole,
    // and the end of the other one goes with it.
    void aWireOnAWireThatGoesWholeFollowsIt()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(20, 0))};
        sheet.wires = {wire({mm(10, 0), mm(10, 10)}), wire({mm(5, 0), mm(15, 0)})};
        const QSet<ObjectId> both = {sheet.components.at(0).id, sheet.components.at(1).id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, both, mm(0, -4)), both, library);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(10, -4), mm(10, 10)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, -4), mm(15, -4)}));
    }

    // Two wires end on the right pin of a part at the origin, one going
    // right and one going down. The part goes up: the first is led to the
    // pin through the old junction, and the second stays joined there.
    void wiresEndingTogetherKeepTheirJunction()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(5, 0), mm(20, 0)}), wire({mm(5, 0), mm(5, 20)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, part, mm(0, -4)), part, library);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(5, -4), mm(5, 0), mm(20, 0)}));
        QCOMPARE(moved.wires.at(1).points, sheet.wires.at(1).points);
        QVERIFY(junctions(moved, library).contains(mm(5, 0)));
    }

    // Two wires end on the right pin of a part at the origin, one going
    // right and one going down. The part goes right: the first grows short,
    // and the second bends over where the first was, which is free now,
    // instead of going round it.
    void aWireLaidAgainIsNoObstacleWhereItWas()
    {
        Library library = twoPinLibrary();
        // Pins of no length, so that no wire runs along one.
        for (Pin &pin : library.symbols[QStringLiteral("two")].pins)
            pin.length = 0;
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(5, 0), mm(20, 0)}), wire({mm(5, 0), mm(5, 20)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, part, mm(3, 0)), part, library);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(8, 0), mm(20, 0)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(8, 0), mm(5, 0), mm(5, 20)}));
    }

    // Two wires end on the right pin of a part at the origin, one going
    // right and one going down. The part and the first wire go up together:
    // that wire just goes, with no bend back to where the second one was,
    // and the second follows the pin.
    void aWireMovedWithItsPinIsNotLedBack()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(5, 0), mm(20, 0)}), wire({mm(5, 0), mm(5, 20)})};
        const QSet<ObjectId> both = {sheet.components.first().id, sheet.wires.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, both, mm(0, -4)), both, library);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(5, -4), mm(20, -4)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, -4), mm(5, 20)}));
    }

    void aWireEndOnAPinLeftInPlaceStays()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        // The right pin of the first part lies on the left pin of the second.
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(10, 0))};
        sheet.wires = {wire({mm(5, 0), mm(5, 10)})};
        const QSet<ObjectId> first = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, first, mm(0, -5)), first, library);
        QCOMPARE(moved.wires.first().points, sheet.wires.first().points);
        // The pins that touched are joined by a new wire.
        QCOMPARE(moved.wires.size(), 2);
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, 0), mm(5, -5)}));
    }

    // Part A at the origin and part B touch pin to pin at (5, 0), where a
    // wire goes up from. B goes up and right: its new wire joins that wire
    // straight across instead of going back to the pins.
    void aNewWireJoinsAWireAtThePinsItLeft()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(10, 0))};
        sheet.wires = {wire({mm(5, 0), mm(5, -10)})};
        const QSet<ObjectId> b = {sheet.components.at(1).id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, b, mm(5, -6)), b, library);
        QCOMPARE(moved.wires.size(), 2);
        QCOMPARE(moved.wires.at(0).points, sheet.wires.at(0).points);
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, -6), mm(10, -6)}));
        QVERIFY(junctions(moved, library).contains(mm(5, -6)));
    }

    // The same, with B going straight up: its pin lands on the middle of the
    // wire, which is cut there, so that two of its ends meet the pin.
    void aPinLandingOnAWireCutsIt()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(10, 0))};
        sheet.wires = {wire({mm(5, 0), mm(5, -10)})};
        const QSet<ObjectId> b = {sheet.components.at(1).id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, b, mm(0, -6)), b, library);
        QCOMPARE(moved.wires.size(), 2);
        QVERIFY(moved.wires.at(0).id == sheet.wires.at(0).id);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(5, 0), mm(5, -6)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, -6), mm(5, -10)}));
    }

    // A wire passes through the right pin of a part at the origin without
    // ending there. The part goes up: the wire is cut at the pin, and both
    // parts go with it.
    void aWirePassingThroughAPinFollowsIt()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(5, -10), mm(5, 10)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, part, mm(0, -4)), part, library);
        QCOMPARE(moved.wires.size(), 2);
        QVERIFY(moved.wires.at(0).id == sheet.wires.at(0).id);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(5, -10), mm(5, -4)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, -4), mm(5, 10)}));

        // A part that does not move leaves the wire whole.
        QCOMPARE(withWiresFollowing(sheet, sheet, part, library), sheet);
    }

    // Part A at the origin and part B touch pin to pin at (5, 0). B goes
    // up, and its pin lands on a wire that has nothing to do with A: the
    // pins still get their wire, which ends on that one.
    void aPinLandingOnAnotherWireKeepsThePinItTouched()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(10, 0))};
        sheet.wires = {wire({mm(0, -6), mm(8, -6)})};
        const QSet<ObjectId> b = {sheet.components.at(1).id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, b, mm(0, -6)), b, library);
        QCOMPARE(moved.wires.size(), 2);
        QCOMPARE(moved.wires.at(0).points, sheet.wires.at(0).points);
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, 0), mm(5, -6)}));
    }

    void pinsThatTouchedAreJoinedByANewWire()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(10, 0))};
        const QSet<ObjectId> second = {sheet.components.at(1).id};
        const auto dragged = [&](const Point &delta) {
            return withWiresFollowing(sheet, movedObjects(sheet, second, delta), second, library)
                .wires;
        };
        QCOMPARE(dragged(mm(0, 5)).size(), 1);
        QCOMPARE(dragged(mm(0, 5)).first().points, QList<Point>({mm(5, 0), mm(5, 5)}));
        QCOMPARE(dragged(mm(10, 0)).first().points, QList<Point>({mm(5, 0), mm(15, 0)}));
        // Both moved together: nothing to join.
        const QSet<ObjectId> both = {sheet.components.at(0).id, sheet.components.at(1).id};
        QVERIFY(withWiresFollowing(sheet, movedObjects(sheet, both, mm(0, 5)), both, library)
                    .wires.isEmpty());
    }

    // A part standing upright, its top pin ending at (0, -5) and wired from
    // the left: a wire never comes into the pin along the pin.
    void wiresComeIntoAPinClearOfIt()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0), 90)};
        sheet.wires = {wire({mm(-20, -5), mm(0, -5)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const auto dragged = [&](const Point &delta) {
            return withWiresFollowing(sheet, movedObjects(sheet, part, delta), part, library)
                .wires.first()
                .points;
        };
        // Down: the wire comes into the pin from above, the way it points.
        QCOMPARE(dragged(mm(0, 5)), QList<Point>({mm(-20, -5), mm(0, -5), mm(0, 0)}));
        // Up: from above the old end it would run along the pin, so the bend
        // starts earlier and the wire comes in from the side.
        QCOMPARE(dragged(mm(0, -5)), QList<Point>({mm(-20, -5), mm(-20, -10), mm(0, -10)}));
    }

    // A wire from (-20, 0) to the left pin of part A at the origin; part B,
    // with a body 4 by 2 mm, stands at (-5, 5), across the way down from
    // that pin.
    void wiresGoRoundTheBodiesOfParts()
    {
        Library library = twoPinLibrary();
        Graphic body;
        body.kind = GraphicKind::Rect;
        body.a = mm(-2, -1);
        body.b = mm(2, 1);
        library.symbols[QStringLiteral("two")].graphics.append(body);
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(-5, 5))};
        sheet.wires = {wire({mm(-20, 0), mm(-5, 0)})};
        const QSet<ObjectId> a = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, a, mm(0, 10)), a, library);
        // Down from the old end would cross B: the bend turns the other way.
        QCOMPARE(moved.wires.first().points, QList<Point>({mm(-20, 0), mm(-20, 10), mm(-5, 10)}));
    }

    // Part A stays at the origin; part B, touching it pin to pin at (5, 0),
    // goes up and to the left, so that one bend would run along a pin or
    // through a body either way round: the new wire takes two.
    void aNewWireTakesTwoBendsToKeepClear()
    {
        Library library = twoPinLibrary();
        Graphic body;
        body.kind = GraphicKind::Rect;
        body.a = mm(-2, -1);
        body.b = mm(2, 1);
        library.symbols[QStringLiteral("two")].graphics.append(body);
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0)), twoPin(mm(10, 0))};
        const QSet<ObjectId> b = {sheet.components.at(1).id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, b, mm(-5, -10)), b, library);
        QCOMPARE(moved.wires.size(), 1);
        // Halfway is on the half grid: -5.08 mm.
        const Coord halfway = -5080000;
        QCOMPARE(moved.wires.first().points,
                 QList<Point>({mm(5, 0), Point{5 * MM, halfway}, Point{0, halfway}, mm(0, -10)}));
    }

    // Part A stands upright, its bottom pin wired from the left by a wire
    // that runs along y = 0 first. A is dragged down onto that run: the wire
    // is laid again from its far end, round the body instead of through it.
    void aWireIsLaidAgainAsFarBackAsNeeded()
    {
        Library library = twoPinLibrary();
        Graphic body;
        body.kind = GraphicKind::Rect;
        body.a = mm(-2, -1);
        body.b = mm(2, 1);
        library.symbols[QStringLiteral("two")].graphics.append(body);
        Sheet sheet;
        sheet.components = {twoPin(mm(0, -10), 90)};
        sheet.wires = {wire({mm(20, 0), mm(-10, 0), mm(-10, -5), mm(0, -5)})};
        const QSet<ObjectId> a = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, a, mm(0, 10)), a, library);
        QCOMPARE(moved.wires.first().points, QList<Point>({mm(20, 0), mm(20, 5), mm(0, 5)}));
    }

    // The same, with a wire from below ending on the run at (10, 0): the
    // wire is laid again from there, and that wire stays joined.
    void aWireIsLaidAgainFromWhereAnotherJoinsIt()
    {
        Library library = twoPinLibrary();
        Graphic body;
        body.kind = GraphicKind::Rect;
        body.a = mm(-2, -1);
        body.b = mm(2, 1);
        library.symbols[QStringLiteral("two")].graphics.append(body);
        Sheet sheet;
        sheet.components = {twoPin(mm(0, -10), 90)};
        sheet.wires = {wire({mm(30, 0), mm(-10, 0), mm(-10, -5), mm(0, -5)}),
                       wire({mm(10, 10), mm(10, 0)})};
        const QSet<ObjectId> a = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, a, mm(0, 10)), a, library);
        // Not down over the wire from below: round it.
        QCOMPARE(moved.wires.at(0).points,
                 QList<Point>({mm(30, 0), Point{5080000, 0}, Point{5080000, 5 * MM}, mm(0, 5)}));
        QCOMPARE(moved.wires.at(1).points, sheet.wires.at(1).points);
    }

    // A wire coming down at x = -20 and turning right into the left pin of
    // a part at the origin.
    void theLastSegmentSlidesWithThePin()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(-20, 10), mm(-20, 0), mm(-5, 0)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const auto dragged = [&](const Sheet &from, const Point &delta) {
            return withWiresFollowing(from, movedObjects(from, part, delta), part, library)
                .wires.first()
                .points;
        };
        // No bend is added: the corner goes along.
        QCOMPARE(dragged(sheet, mm(0, 5)), QList<Point>({mm(-20, 10), mm(-20, 5), mm(-5, 5)}));
        QCOMPARE(dragged(sheet, mm(3, -15)), QList<Point>({mm(-20, 10), mm(-20, -15), mm(-2, -15)}));

        // A wire ends on the corner: the corner stays, and a bend is added.
        Sheet joined = sheet;
        joined.wires.append(wire({mm(-30, 0), mm(-20, 0)}));
        QCOMPARE(dragged(joined, mm(0, 5)),
                 QList<Point>({mm(-20, 10), mm(-20, 0), mm(-5, 0), mm(-5, 5)}));
    }

    void wiresEndingOnAMovedWireFollowIt()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        // A wire ending in the middle of another one, and one ending on its corner.
        sheet.wires = {wire({mm(0, 0), mm(10, 0), mm(10, 10)}), wire({mm(4, 0), mm(4, -6)}),
                       wire({mm(10, 0), mm(20, 0)})};
        const QSet<ObjectId> line = {sheet.wires.first().id};
        // Turned a quarter about (10, 0): (0, 0) goes to (10, -10).
        const Sheet turned = rotatedObjects(sheet, line, mm(10, 0));
        QCOMPARE(turned.wires.first().points, QList<Point>({mm(10, -10), mm(10, 0), mm(0, 0)}));
        const Sheet moved = withWiresFollowing(sheet, turned, line, library);
        // (4, 0), 4 mm along the first segment, goes 4 mm along it turned:
        // to (10, -6), where the bend from (4, 0) is cut back to a straight wire.
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(10, -6), mm(4, -6)}));
        // On the corner, which stays where it was.
        QCOMPARE(moved.wires.at(2).points, sheet.wires.at(2).points);
    }

    void joinedWiresAreFoundOneThroughAnother()
    {
        Sheet sheet;
        // A runs right; B ends on its middle; C shares B's other end; D only
        // crosses A; E stands apart.
        sheet.wires = {wire({mm(0, 0), mm(20, 0)}), wire({mm(10, 0), mm(10, 10)}),
                       wire({mm(10, 10), mm(30, 10)}), wire({mm(15, -5), mm(15, 5)}),
                       wire({mm(40, 0), mm(50, 0)})};
        const QList<Wire> &w = sheet.wires;
        QCOMPARE(joinedWires(sheet, w.at(2).id), QSet<ObjectId>({w.at(0).id, w.at(1).id, w.at(2).id}));
        QCOMPARE(joinedWires(sheet, w.at(3).id), QSet<ObjectId>({w.at(3).id}));
        QVERIFY(joinedWires(sheet, newObjectId()).isEmpty());
    }

    void aWireEndIsDraggedAlone()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        // A wire from (0, 0) to (10, 0), another one ending on its middle.
        sheet.wires = {wire({mm(0, 0), mm(10, 0)}), wire({mm(5, 0), mm(5, 5)})};
        const ObjectId id = sheet.wires.first().id;
        const auto ended = [&](bool last, const Point &to) {
            return withWireEndAt(sheet, library, id, last, to).wires;
        };
        QCOMPARE(ended(true, mm(10, 5)).first().points,
                 QList<Point>({mm(0, 0), mm(10, 0), mm(10, 5)}));
        QCOMPARE(ended(false, mm(-5, 0)).first().points, QList<Point>({mm(-5, 0), mm(10, 0)}));
        // The other wire stays as it was.
        QCOMPARE(ended(true, mm(10, 5)).at(1).points, sheet.wires.at(1).points);
        // Onto the other end: no length left, so no wire.
        QCOMPARE(ended(true, mm(0, 0)).size(), 1);
        QVERIFY(ended(true, mm(0, 0)).first().id == sheet.wires.at(1).id);
        // A wire it does not have: the sheet as it is.
        QCOMPARE(withWireEndAt(sheet, library, newObjectId(), true, mm(3, 3)).wires.size(), 2);
    }

    // A wire from the right pin of a part at the origin, (5, 0), right to
    // (15, 0), down to (15, 10) and right to a free end at (25, 10); another
    // wire ends on its middle segment at (15, 5).
    void aSegmentIsMovedAcrossItself()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(5, 0), mm(15, 0), mm(15, 10), mm(25, 10)}),
                       wire({mm(30, 5), mm(15, 5)})};
        const ObjectId id = sheet.wires.first().id;
        const auto moved = [&](int index, const Point &delta) {
            return withSegmentMoved(sheet, library, id, index, delta).wires;
        };
        // The upright middle one goes sideways only; the others stretch, and
        // the wire ending on it goes along.
        QList<Wire> wires = moved(1, mm(3, 4));
        QCOMPARE(wires.at(0).points, QList<Point>({mm(5, 0), mm(18, 0), mm(18, 10), mm(25, 10)}));
        QCOMPARE(wires.at(1).points, QList<Point>({mm(30, 5), mm(18, 5)}));
        // The first one ends on a pin: a bend leads back to it.
        wires = moved(0, mm(0, -3));
        QCOMPARE(wires.at(0).points,
                 QList<Point>({mm(5, 0), mm(5, -3), mm(15, -3), mm(15, 10), mm(25, 10)}));
        // The last one ends free: it just goes.
        wires = moved(2, mm(0, 2));
        QCOMPARE(wires.at(0).points, QList<Point>({mm(5, 0), mm(15, 0), mm(15, 12), mm(25, 12)}));
        // Along itself: nothing.
        QCOMPARE(moved(2, mm(4, 0)), sheet.wires);
    }

    // An upright segment with a level one after it, and a wire ending on
    // the upright one near the corner. The level one goes up past that end:
    // the end goes to the corner instead of being left behind.
    void aWireOnASegmentThatShrinksStaysJoined()
    {
        const Library library;
        Sheet sheet;
        sheet.wires = {wire({mm(0, 0), mm(0, 10), mm(10, 10)}), wire({mm(-5, 8), mm(0, 8)})};
        const QList<Wire> wires =
            withSegmentMoved(sheet, library, sheet.wires.first().id, 1, mm(0, -5)).wires;
        QCOMPARE(wires.at(0).points, QList<Point>({mm(0, 0), mm(0, 5), mm(10, 5)}));
        QCOMPARE(wires.at(1).points.first(), mm(-5, 8));
        QCOMPARE(wires.at(1).points.last(), mm(0, 5));
    }

    // A wire across from one rail to another, its ends on their middles.
    void aSegmentSlidesAlongTheWiresItsEndsLieOn()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.wires = {wire({mm(0, 0), mm(20, 0)}), wire({mm(0, 10), mm(20, 10)}),
                       wire({mm(10, 0), mm(10, 10)})};
        const ObjectId id = sheet.wires.at(2).id;
        // Along the rails: the ends, and the junctions, go along.
        QCOMPARE(withSegmentMoved(sheet, library, id, 0, mm(3, 0)).wires.at(2).points,
                 QList<Point>({mm(13, 0), mm(13, 10)}));
        // The rails stay as they are.
        QCOMPARE(withSegmentMoved(sheet, library, id, 0, mm(3, 0)).wires.at(0).points,
                 sheet.wires.at(0).points);
    }

    // A level wire from the right pin of a part at the origin, with an
    // upright wire going down from the same pin: moved down, the level one
    // slides down the upright one instead of leaving a bend over it.
    void aSegmentSlidesOffAPinAlongAWireThere()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(5, 0), mm(5, 20)}), wire({mm(5, 0), mm(20, 0)})};
        const ObjectId id = sheet.wires.at(1).id;
        QCOMPARE(withSegmentMoved(sheet, library, id, 0, mm(0, 4)).wires.at(1).points,
                 QList<Point>({mm(5, 4), mm(20, 4)}));
        // The upright one stays as it is.
        QCOMPARE(withSegmentMoved(sheet, library, id, 0, mm(0, 4)).wires.at(0).points,
                 sheet.wires.at(0).points);
    }

    void segmentsAreFoundCutAndCopied()
    {
        Sheet sheet;
        sheet.wires = {wire({mm(0, 0), mm(10, 0), mm(10, 10), mm(20, 10)})};
        const Wire &line = sheet.wires.first();
        QCOMPARE(segmentAt(line, QPointF(5, 1)), 0);
        QCOMPARE(segmentAt(line, QPointF(11, 6)), 1);
        QCOMPARE(segmentAt(line, QPointF(30, 10)), 2);
        QCOMPARE(segmentAt(wire({mm(1, 1)}), QPointF(1, 1)), -1);

        // The middle one cut away: two wires, the first keeps the id.
        Sheet cut = withoutSegment(sheet, line.id, 1);
        QCOMPARE(cut.wires.size(), 2);
        QVERIFY(cut.wires.at(0).id == line.id);
        QCOMPARE(cut.wires.at(0).points, QList<Point>({mm(0, 0), mm(10, 0)}));
        QCOMPARE(cut.wires.at(1).points, QList<Point>({mm(10, 10), mm(20, 10)}));
        QVERIFY(cut.wires.at(1).id != line.id);
        // An end one: the rest stays one wire.
        cut = withoutSegment(sheet, line.id, 0);
        QCOMPARE(cut.wires.size(), 1);
        QCOMPARE(cut.wires.at(0).points, QList<Point>({mm(10, 0), mm(10, 10), mm(20, 10)}));
        // The only one: the wire goes.
        Sheet single;
        single.wires = {wire({mm(0, 0), mm(5, 0)})};
        QVERIFY(withoutSegment(single, single.wires.first().id, 0).wires.isEmpty());

        const Sheet piece = segmentOf(sheet, line.id, 2);
        QCOMPARE(piece.wires.size(), 1);
        QCOMPARE(piece.wires.first().points, QList<Point>({mm(10, 10), mm(20, 10)}));
        QVERIFY(segmentOf(sheet, line.id, 3).wires.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestWiring)
#include "test_wiring.moc"
