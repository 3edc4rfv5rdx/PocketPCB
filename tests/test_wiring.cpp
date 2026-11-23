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

// The grid wires are laid on: half the pin grid.
const Coord H = PIN_GRID / 2;

Point h(Coord x, Coord y)
{
    return Point{x * H, y * H};
}

// A symbol on that grid: pins at (-4, 0) and (4, 0) steps, each two steps
// long, wired from the left and from the right, and a body 4 by 2 steps.
Library gridLibrary()
{
    Symbol symbol;
    Pin left;
    left.number = QStringLiteral("1");
    left.at = h(-4, 0);
    left.length = 2 * H;
    left.side = PinSide::Left;
    Pin right = left;
    right.number = QStringLiteral("2");
    right.side = PinSide::Right;
    right.at = h(4, 0);
    symbol.pins = {left, right};
    Graphic body;
    body.kind = GraphicKind::Rect;
    body.a = h(-2, -1);
    body.b = h(2, 1);
    symbol.graphics = {body};
    Library library;
    library.symbols.insert(QStringLiteral("grid"), symbol);
    Part part;
    part.symbol = QStringLiteral("grid");
    library.parts.insert(QStringLiteral("grid"), part);
    return library;
}

Component gridPart(const Point &at)
{
    Component component = twoPin(at);
    component.part = QStringLiteral("grid");
    return component;
}

// True when a point of the grid inside the rectangle from 'a' to 'b', edges
// left out, lies on the wire 'points'.
bool passesInside(const QList<Point> &points, const Point &a, const Point &b)
{
    for (Coord x = a.x + H; x < b.x; x += H) {
        for (Coord y = a.y + H; y < b.y; y += H) {
            if (isOnWire(points, Point{x, y}))
                return true;
        }
    }
    return false;
}

} // namespace

class TestWiring : public QObject
{
    Q_OBJECT

private slots:
    void aClickChoosesTheWireUpToPinsAndJoints()
    {
        // A wire from (0, 0) up to (0, -40) and left to (-10, -40); the left
        // pin of a part at (5, -20) lies on it at (0, -20), and another wire
        // ends on it at (0, -30).
        Sheet sheet;
        sheet.components = {twoPin(mm(5, -20))};
        const Wire long_ = wire({mm(0, 0), mm(0, -40), mm(-10, -40)});
        sheet.wires = {long_, wire({mm(0, -30), mm(-10, -30)})};
        const Library library = twoPinLibrary();

        // Below the pin: from the end up to the pin.
        WirePiece piece = wirePieceAt(sheet, library, long_.id, 0, QPointF(0, -5));
        QCOMPARE(piece.index, 0);
        QCOMPARE(linePoints(piece.sheet, long_.id),
                 (QList<Point>{mm(0, 0), mm(0, -20), mm(0, -40), mm(-10, -40)}));
        // Between the pin and the joint.
        piece = wirePieceAt(sheet, library, long_.id, 0, QPointF(0, -25));
        QCOMPARE(piece.index, 1);
        QCOMPARE(linePoints(piece.sheet, long_.id).mid(1, 2),
                 (QList<Point>{mm(0, -20), mm(0, -30)}));
        // Deleting it leaves the end on the pin and the wire on from the joint.
        const Sheet cut = withoutSegment(piece.sheet, long_.id, piece.index);
        QCOMPARE(linePoints(cut, long_.id), (QList<Point>{mm(0, 0), mm(0, -20)}));
        // A segment with nothing inside stays as it is.
        piece = wirePieceAt(sheet, library, long_.id, 1, QPointF(-5, -40));
        QCOMPARE(piece.index, 1);
        QCOMPARE(linePoints(piece.sheet, long_.id), long_.points);
    }

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

    // A power symbol is a pin of its own: wires end on its point, keep off
    // its pin's line and follow it when it goes.
    void powerSymbolsArePins()
    {
        Library library = twoPinLibrary();
        // A bar on a stem: the pin is wired from below, its line goes up.
        Symbol bar;
        Pin stem;
        stem.number = QStringLiteral("1");
        stem.side = PinSide::Down;
        stem.length = 2540000;
        bar.pins = {stem};
        bar.power = QStringLiteral("+5V");
        library.symbols.insert(QStringLiteral("bar"), bar);

        PowerSymbol power;
        power.id = newObjectId();
        power.at = mm(20, 0);
        power.name = QStringLiteral("+5V");
        power.symbol = QStringLiteral("bar");
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.powerSymbols = {power};
        sheet.wires = {wire({mm(5, 0), mm(20, 0)})};

        QList<SheetPin> pins = sheetPins(sheet, library);
        QCOMPARE(pins.size(), 3);
        QCOMPARE(pins.last().owner, power.id);
        QCOMPARE(pins.last().at, mm(20, 0));
        QCOMPARE(pins.last().bodyEnd, mm(20, 0) - Point({0, stem.length}));
        // Turned, the line turns with it.
        Sheet turned = sheet;
        turned.powerSymbols[0].rotation = 90;
        QCOMPARE(sheetPins(turned, library).last().bodyEnd, mm(20, 0) + Point({stem.length, 0}));
        // Without its symbol it still holds wires, with no line to keep off.
        Sheet lacking = sheet;
        lacking.powerSymbols[0].symbol = QStringLiteral("absent");
        pins = sheetPins(lacking, library);
        QCOMPARE(pins.last().at, mm(20, 0));
        QCOMPARE(pins.last().bodyEnd, mm(20, 0));

        QVERIFY(isConnectionPoint(sheet, library, mm(20, 0)));
        // A second wire on the symbol's point makes a dot with the first.
        Sheet joined = sheet;
        joined.wires.append(wire({mm(20, 0), mm(20, 10)}));
        QCOMPARE(junctions(joined, library), QList<Point>({mm(20, 0)}));

        // Moved, the symbol takes the wire end along.
        const QSet<ObjectId> symbol = {sheet.powerSymbols.first().id};
        Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, symbol, mm(3, 0)), symbol,
                                         library);
        QCOMPARE(moved.wires.first().points, QList<Point>({mm(5, 0), mm(23, 0)}));

        // The wire moved alone keeps its end on the symbol left in place.
        const QSet<ObjectId> line = {sheet.wires.first().id};
        moved = withWiresFollowing(sheet, movedObjects(sheet, line, mm(0, 5)), line, library);
        QCOMPARE(moved.wires.first().points.first(), mm(5, 0));
        QCOMPARE(moved.wires.first().points.last(), mm(20, 0));
    }

    // Labels left in place go with the pin or the wire they lie on.
    void labelsGoWithWhatTheyLieOn()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        // A part at the origin with a wire from its left pin, a free wire,
        // and labels on the pin, on each wire and on nothing.
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(-20, 0), mm(-5, 0)}), wire({mm(0, 30), mm(10, 30)})};
        const auto label = [](const Point &at) {
            Label result;
            result.id = newObjectId();
            result.at = at;
            result.text = QStringLiteral("N");
            return result;
        };
        sheet.labels = {label(mm(5, 0)), label(mm(-10, 0)), label(mm(5, 30)), label(mm(50, 50))};
        // A no-connect mark on the free pin too, and one on nothing.
        NoConnect mark;
        mark.id = newObjectId();
        mark.at = mm(5, 0);
        NoConnect stray = mark;
        stray.id = newObjectId();
        stray.at = mm(40, 40);
        sheet.noConnects = {mark, stray};
        const QSet<ObjectId> part = {sheet.components.first().id};

        // The part goes along its wire: the label on its pin goes too, the
        // one on the wire stays as far from the wire's far end.
        Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, mm(3, 0)), part, library);
        QCOMPARE(moved.labels.at(0).at, mm(8, 0));
        QCOMPARE(moved.labels.at(1).at, mm(-10, 0));
        QCOMPARE(moved.labels.at(3).at, mm(50, 50));
        QCOMPARE(moved.noConnects.at(0).at, mm(8, 0));
        QCOMPARE(moved.noConnects.at(1).at, mm(40, 40));
        QCOMPARE(partPinPoints(sheet, library), QList<Point>({mm(-5, 0), mm(5, 0)}));
        // Across: the wire gets a bend, and the label stays where it is on it.
        moved = withWiresFollowing(sheet, movedObjects(sheet, part, mm(0, 5)), part, library);
        QCOMPARE(moved.labels.at(0).at, mm(5, 5));
        QCOMPARE(moved.labels.at(1).at, mm(-10, 0));

        // The free wire moved: its label comes along.
        const QSet<ObjectId> free = {sheet.wires.at(1).id};
        moved = withWiresFollowing(sheet, movedObjects(sheet, free, mm(2, 3)), free, library);
        QCOMPARE(moved.labels.at(2).at, mm(7, 33));
        // A label moved with it is not moved twice.
        const QSet<ObjectId> both = {sheet.wires.at(1).id, sheet.labels.at(2).id};
        moved = withWiresFollowing(sheet, movedObjects(sheet, both, mm(2, 3)), both, library);
        QCOMPARE(moved.labels.at(2).at, mm(7, 33));

        // Its segment dragged down, and its end dragged further.
        moved = withSegmentMoved(sheet, library, sheet.wires.at(1).id, 0, mm(0, 4));
        QCOMPARE(moved.labels.at(2).at, mm(5, 34));
        moved = withWireEndAt(sheet, library, sheet.wires.at(1).id, true, mm(20, 30));
        QCOMPARE(moved.labels.at(2).at, mm(5, 30));
    }

    void wiresEndOnBuses()
    {
        Sheet sheet;
        Bus bus;
        bus.points = {mm(0, 0), mm(20, 0), mm(20, 20)};
        sheet.buses = {bus};
        QVERIFY(isOnBus(sheet, mm(10, 0)));
        QVERIFY(isOnBus(sheet, mm(20, 15)));
        QVERIFY(!isOnBus(sheet, mm(10, 1)));
        // A bus is no connection point: only its labels join anything.
        QVERIFY(!isConnectionPoint(sheet, Library(), mm(10, 0)));

        // Entering it: the last step turns into a slant to the right of an
        // upright wire, down from a level one, the other way at its end.
        const Coord step = MM;
        QCOMPARE(withBusEntry({mm(10, 10), mm(10, 0)}, sheet, step),
                 QList<Point>({mm(10, 10), mm(10, 1), mm(11, 0)}));
        QCOMPARE(withBusEntry({mm(10, -10), mm(10, 0)}, sheet, step),
                 QList<Point>({mm(10, -10), mm(10, -1), mm(11, 0)}));
        QCOMPARE(withBusEntry({mm(30, 5), mm(20, 5)}, sheet, step),
                 QList<Point>({mm(30, 5), mm(21, 5), mm(20, 6)}));
        QCOMPARE(withBusEntry({mm(30, 20), mm(20, 20)}, sheet, step),
                 QList<Point>({mm(30, 20), mm(21, 20), mm(20, 19)}));
        QCOMPARE(withBusEntry({mm(5, 5), mm(5, 2), mm(8, 2), mm(8, 0)}, sheet, step),
                 QList<Point>({mm(5, 5), mm(5, 2), mm(8, 2), mm(8, 1), mm(9, 0)}));
        // On the line of the bus into its end: a step aside, then a slant.
        QCOMPARE(withBusEntry({mm(-10, 0), mm(0, 0)}, sheet, step),
                 QList<Point>({mm(-10, 0), mm(-10, 1), mm(-1, 1), mm(0, 0)}));
        QCOMPARE(withBusEntry({mm(-10, 5), mm(-10, 0), mm(0, 0)}, sheet, step),
                 QList<Point>({mm(-10, 5), mm(-10, 1), mm(-1, 1), mm(0, 0)}));
        QCOMPARE(withBusEntry({mm(20, 30), mm(20, 20)}, sheet, step),
                 QList<Point>({mm(20, 30), mm(21, 30), mm(21, 21), mm(20, 20)}));
        // Into its corner on the line of one segment: across the other one.
        QCOMPARE(withBusEntry({mm(30, 0), mm(20, 0)}, sheet, step),
                 QList<Point>({mm(30, 0), mm(21, 0), mm(20, 1)}));
        // Left as they are: along the bus, slanted already, too short, off it.
        for (const QList<Point> &points :
             {QList<Point>({mm(-5, 0), mm(5, 0)}), QList<Point>({mm(5, 5), mm(10, 0)}),
              QList<Point>({Point{10 * MM, MM / 2}, mm(10, 0)}),
              QList<Point>({mm(10, 10), mm(10, 1)})}) {
            QCOMPARE(withBusEntry(points, sheet, step), points);
        }
    }

    // A wire begun on a bus leaves it at 45 degrees too; an end that joins a
    // pin or a wire there is left as it is.
    void wiresBegunOnBusesLeaveThemSlanted()
    {
        Sheet sheet;
        Bus bus;
        bus.points = {mm(0, 0), mm(20, 0)};
        sheet.buses = {bus};
        const Library library;
        QCOMPARE(withBusEntries({mm(10, 0), mm(10, 10)}, sheet, library, MM),
                 QList<Point>({mm(11, 0), mm(10, 1), mm(10, 10)}));
        QCOMPARE(withBusEntries({mm(5, 0), mm(5, 10), mm(15, 10), mm(15, 0)}, sheet, library, MM),
                 QList<Point>({mm(6, 0), mm(5, 1), mm(5, 10), mm(15, 10), mm(15, 1), mm(16, 0)}));
        sheet.wires = {wire({mm(10, 0), mm(10, -10)})};
        QCOMPARE(withBusEntries({mm(10, 0), mm(10, 10)}, sheet, library, MM),
                 QList<Point>({mm(10, 0), mm(10, 10)}));
    }

    // A bus is dragged by its segments, its ends or whole, as a wire is,
    // and the wires that enter it go along, entering it at 45 degrees.
    void busesAreDraggedWithTheirEntries()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        Bus bus;
        bus.id = newObjectId();
        bus.points = {mm(0, 0), mm(20, 0)};
        bus.name = QStringLiteral("BUS1");
        sheet.buses = {bus};
        sheet.wires = {wire({mm(10, 10), mm(10, 1), mm(11, 0)})};
        const auto entersBusAt = [](const Sheet &s, Coord y) {
            const Wire &entry = s.wires.first();
            const Point way = entry.points.last() - entry.points.at(entry.points.size() - 2);
            return entry.points.first() == mm(10, 10) && entry.points.last().y == y
                   && isOnBus(s, entry.points.last()) && way.x != 0
                   && std::llabs(way.x) == std::llabs(way.y);
        };

        // Its segment up: the bus goes, the wire's end with it.
        Sheet moved = withBusSegmentMoved(sheet, library, bus.id, 0, mm(0, -5));
        QCOMPARE(moved.buses.first().points, QList<Point>({mm(0, -5), mm(20, -5)}));
        QCOMPARE(moved.buses.first().name, bus.name);
        QVERIFY(moved.wires.size() == 1 && moved.buses.size() == 1);
        QVERIFY(entersBusAt(moved, -5 * MM));

        // Its end further: the wire stays as it is.
        moved = withBusEndAt(sheet, library, bus.id, true, mm(30, 0));
        QCOMPARE(moved.buses.first().points, QList<Point>({mm(0, 0), mm(30, 0)}));
        QCOMPARE(moved.wires.first().points, sheet.wires.first().points);

        // The whole bus up: the same as its segment.
        const QSet<ObjectId> whole = {bus.id};
        moved = withWiresFollowing(sheet, movedObjects(sheet, whole, mm(0, -5)), whole, library);
        QCOMPARE(moved.buses.first().points, QList<Point>({mm(0, -5), mm(20, -5)}));
        QVERIFY(entersBusAt(moved, -5 * MM));
    }

    // A bus drawn on to another is its branch: they are joined, a dot of the
    // bus colour marks the T, and the branch goes with the bus it branches
    // off.
    void busesBranchOffBuses()
    {
        const Library library;
        Sheet sheet;
        const auto bus = [](const QList<Point> &points) {
            Bus result;
            result.id = newObjectId();
            result.points = points;
            result.name = QStringLiteral("D[0..7]");
            return result;
        };
        sheet.buses = {bus({mm(0, 0), mm(40, 0)}), bus({mm(20, 0), mm(20, 20)}),
                       bus({mm(0, 30), mm(40, 30)})};
        const ObjectId main = sheet.buses.at(0).id;
        const ObjectId branch = sheet.buses.at(1).id;
        QCOMPARE(joinedBuses(sheet, main), QSet<ObjectId>({main, branch}));
        QCOMPARE(busJunctions(sheet), QList<Point>({mm(20, 0)}));

        // The bus goes up: the branch's end goes with it.
        Sheet moved = withBusSegmentMoved(sheet, library, main, 0, mm(0, -5));
        QCOMPARE(moved.buses.size(), 3);
        QCOMPARE(moved.buses.at(0).points, QList<Point>({mm(0, -5), mm(40, -5)}));
        QCOMPARE(moved.buses.at(1).points.first(), mm(20, -5));
        QCOMPARE(moved.buses.at(1).points.last(), mm(20, 20));
        QCOMPARE(moved.buses.at(1).name, sheet.buses.at(1).name);
        const QSet<ObjectId> whole = {main};
        moved = withWiresFollowing(sheet, movedObjects(sheet, whole, mm(0, -5)), whole, library);
        QCOMPARE(moved.buses.at(0).points, QList<Point>({mm(0, -5), mm(40, -5)}));
        QCOMPARE(moved.buses.at(1).points.first(), mm(20, -5));
    }

    // A bus holds the end of a wire that enters it, and the entry stays at
    // 45 degrees however the wire is laid again.
    void busesHoldTheirEntries()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        Bus bus;
        bus.id = newObjectId();
        bus.points = {mm(-20, -10), mm(20, -10)};
        bus.name = QStringLiteral("BUS1");
        sheet.buses = {bus};
        // From the right pin up, into the bus over the last millimetre.
        sheet.wires = {wire({mm(5, 0), mm(5, -9), mm(6, -10)})};
        const auto entersSlanted = [](const Wire &w) {
            const Point way = w.points.last() - w.points.at(w.points.size() - 2);
            return way.x != 0 && std::llabs(way.x) == std::llabs(way.y);
        };

        // The part goes: the wire follows its pin and keeps its entry.
        const QSet<ObjectId> part = {sheet.components.first().id};
        Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, mm(3, 0)), part, library);
        QCOMPARE(moved.wires.first().points.first(), mm(8, 0));
        QCOMPARE(moved.wires.first().points.last(), mm(6, -10));
        QVERIFY(entersSlanted(moved.wires.first()));

        // Its upright segment goes sideways: the pin end stays, and the wire
        // enters the bus where it now comes to it, not along it.
        moved = withSegmentMoved(sheet, library, sheet.wires.first().id, 0, mm(2, 0));
        QCOMPARE(moved.wires.first().points,
                 QList<Point>({mm(5, 0), mm(7, 0), mm(7, -9), mm(8, -10)}));

        // The wire itself goes along the bus: its end on the pin is led back,
        // and its entry slides along the bus with it.
        const QSet<ObjectId> line = {sheet.wires.first().id};
        moved = withWiresFollowing(sheet, movedObjects(sheet, line, mm(2, 0)), line, library);
        QCOMPARE(moved.wires.first().points,
                 QList<Point>({mm(5, 0), mm(7, 0), mm(7, -9), mm(8, -10)}));
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

    // A wire drawn back over itself is laid as wires covering each stretch
    // once: one going back short of where it came from goes on from there;
    // one going back past it takes the stretch over from the corner.
    void wiresTurningBackAreCut()
    {
        using Pieces = QList<QList<Point>>;
        QCOMPARE(wiresNotTurningBack({mm(0, 0), mm(10, 0), mm(10, 5)}),
                 Pieces({{mm(0, 0), mm(10, 0), mm(10, 5)}}));
        QCOMPARE(wiresNotTurningBack({mm(0, 0), mm(10, 0), mm(5, 0)}),
                 Pieces({{mm(0, 0), mm(10, 0)}}));
        QCOMPARE(wiresNotTurningBack({mm(0, 5), mm(0, 0), mm(10, 0), mm(5, 0), mm(5, 5)}),
                 Pieces({{mm(0, 5), mm(0, 0), mm(10, 0)}, {mm(5, 0), mm(5, 5)}}));
        QCOMPARE(wiresNotTurningBack({mm(0, 5), mm(0, 0), mm(10, 0), mm(-5, 0)}),
                 Pieces({{mm(0, 5), mm(0, 0)}, {mm(10, 0), mm(-5, 0)}}));
        QCOMPARE(wiresNotTurningBack({mm(0, 0), mm(10, 0), mm(-5, 0), mm(-5, 5)}),
                 Pieces({{mm(10, 0), mm(-5, 0), mm(-5, 5)}}));
        // Wires that would go back along each other are not merged.
        Sheet sheet;
        sheet.wires = {wire({mm(0, 0), mm(10, 0)}), wire({mm(10, 0), mm(5, 0), mm(5, 5)})};
        QCOMPARE(withWiresMerged(sheet).wires.size(), 2);
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

    // One wire from the right pin of a part at the origin through the left
    // pin of a part at (15, 10), at (10, 10), to the right pin of a part at
    // (0, 20). The two outer parts go left together: the wire is not shifted
    // whole off the pin left in place but stays joined to it.
    void aPinLeftInPlaceHoldsAWireWhoseEndsGoTogether()
    {
        const Library library = twoPinLibrary();
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            Sheet sheet;
            sheet.components = {twoPin(mm(0, 0)), twoPin(mm(0, 20)), twoPin(mm(15, 10))};
            sheet.wires = {wire({mm(5, 0), mm(10, 0), mm(10, 20), mm(5, 20)})};
            const QSet<ObjectId> outer = {sheet.components.at(0).id, sheet.components.at(1).id};
            const Sheet moved = withWiresFollowing(
                sheet, movedObjects(sheet, outer, mm(-4, 0)), outer, library, routing);
            // The pin left in place and the two moved pins are on one net.
            ObjectId held;
            for (const Wire &w : moved.wires) {
                if (isOnWire(w, mm(10, 10)))
                    held = w.id;
            }
            QVERIFY(!held.isNull());
            const QSet<ObjectId> net = joinedWires(moved, held);
            for (const Point &pin : {mm(1, 0), mm(1, 20)}) {
                QVERIFY(std::any_of(moved.wires.cbegin(), moved.wires.cend(), [&](const Wire &w) {
                    return net.contains(w.id) && isOnWire(w, pin);
                }));
            }
        }
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

    // Two wires end on the right pin of a part at the origin, one going up
    // and one going down. The part goes down, along the second: the first
    // grows after the pin and the second grows short, instead of staying
    // whole under the first.
    void wiresInOneLineThroughAPinDoNotOverlap()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(5, 0), mm(5, -10)}), wire({mm(5, 0), mm(5, 10)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, part, mm(0, 3)), part, library);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(5, 3), mm(5, -10)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, 3), mm(5, 10)}));
    }

    // A part between two wires that go round it to its pins, one over it
    // and one under it. Turned a quarter, each pin lands on the wire of the
    // other one: the wires are laid again clear of the pins they do not
    // end on, instead of passing through them.
    void wiresKeepOffThePinsTheyDoNotEndOn()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        // Turned half round: the left pin is at (15, 0), the right at (5, 0).
        sheet.components = {twoPin(mm(10, 0), 180)};
        sheet.wires = {wire({mm(0, 10), mm(0, -5), mm(15, -5), mm(15, 0)}),
                       wire({mm(15, 5), mm(5, 5), mm(5, 0)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const Sheet moved =
            withWiresFollowing(sheet, rotatedObjects(sheet, part, mm(10, 0)), part, library);
        // The left pin has gone to (10, 5), the right one to (10, -5).
        QCOMPARE(moved.wires.at(0).points, QList<Point>({mm(0, 10), mm(0, 5), mm(10, 5)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(15, 5), mm(15, -5), mm(10, -5)}));
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

    // Two wires where just their ends meet become one, the first keeping
    // its id, also on a pin; at a fork or a bus, and where a wire passes,
    // they stay.
    void wiresGoingOnFromEachOtherAreMerged()
    {
        Sheet sheet;
        // A and B meet end to end, B drawn backwards and going on straight;
        // C, D, E meet at one point; F and G meet where H passes; K and L
        // meet on a bus; I and J meet on a pin.
        sheet.wires = {wire({mm(0, 0), mm(10, 0)}), wire({mm(10, 10), mm(10, 5), mm(10, 0)}),
                       wire({mm(0, 20), mm(10, 20)}), wire({mm(10, 20), mm(20, 20)}),
                       wire({mm(10, 20), mm(10, 30)}), wire({mm(0, 40), mm(10, 40)}),
                       wire({mm(10, 40), mm(20, 40)}), wire({mm(10, 35), mm(10, 45)}),
                       wire({mm(70, 0), mm(75, 0)}), wire({mm(75, 0), mm(80, 0)}),
                       wire({mm(50, 0), mm(55, 0)}), wire({mm(55, 0), mm(55, 10)})};
        sheet.components = {twoPin(mm(60, 0))};
        Bus bus;
        bus.id = newObjectId();
        bus.points = {mm(75, -10), mm(75, 10)};
        sheet.buses = {bus};
        const ObjectId a = sheet.wires.at(0).id;

        const Sheet merged = withWiresMerged(sheet);
        QCOMPARE(int(merged.wires.size()), int(sheet.wires.size()) - 2);
        QVERIFY(merged.wires.at(0).id == a);
        QCOMPARE(merged.wires.at(0).points, QList<Point>({mm(0, 0), mm(10, 0), mm(10, 10)}));
        for (int i = 2; i < 10; ++i)
            QVERIFY(merged.wires.at(i - 1) == sheet.wires.at(i));
        // (55, 0) is the left pin of the part at (60, 0).
        QVERIFY(merged.wires.last().id == sheet.wires.at(10).id);
        QCOMPARE(merged.wires.last().points, QList<Point>({mm(50, 0), mm(55, 0), mm(55, 10)}));
        // A chain of three is one wire, the straight joints gone.
        Sheet line;
        line.wires = {wire({mm(0, 0), mm(5, 0)}), wire({mm(10, 0), mm(15, 0)}),
                      wire({mm(5, 0), mm(10, 0)})};
        const Sheet one = withWiresMerged(line);
        QCOMPARE(int(one.wires.size()), 1);
        QCOMPARE(one.wires.at(0).points, QList<Point>({mm(0, 0), mm(15, 0)}));
    }

    // A wire that passes (5, 0) and later turns there, crossed there by
    // another: joining them cuts the first at its pass, and its part after
    // the cut keeps the corner it turns at there.
    void aWireCutWhereItTurnsLaterKeepsThatCorner()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.wires = {
            wire({mm(0, 0), mm(10, 0), mm(10, 5), mm(5, 5), mm(5, 0), mm(10, -5)}),
            wire({mm(2, -3), mm(8, 3)})};
        const Sheet joined = withWiresJoinedAt(sheet, library, mm(5, 0));
        QCOMPARE(joined.wires.size(), 3);
        QCOMPARE(joined.wires.at(0).points, QList<Point>({mm(0, 0), mm(5, 0)}));
        QCOMPARE(joined.wires.at(1).points,
                 QList<Point>({mm(5, 0), mm(10, 0), mm(10, 5), mm(5, 5), mm(5, 0), mm(10, -5)}));
    }

    void crossingWiresAreJoinedAndSeparated()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        // A level and an upright wire crossing at (10, 0), not joined.
        sheet.wires = {wire({mm(0, 0), mm(20, 0)}), wire({mm(10, -10), mm(10, 10)})};
        const Point at = mm(10, 0);
        QVERIFY(canJoinWiresAt(sheet, library, at));
        QVERIFY(!canSeparateWiresAt(sheet, library, at));
        QVERIFY(!canJoinWiresAt(sheet, library, mm(5, 0)));

        // Joined: one of them cut there, so the point has its dot.
        const Sheet joined = withWiresJoinedAt(sheet, library, at);
        QCOMPARE(int(joined.wires.size()), 3);
        QCOMPARE(junctions(joined, library), QList<Point>({at}));
        QVERIFY(!canJoinWiresAt(joined, library, at));
        QVERIFY(canSeparateWiresAt(joined, library, at));
        // The document merges wires after every step; the cut stays.
        QCOMPARE(int(withWiresMerged(joined).wires.size()), 3);

        // Separated: the two halves one wire again, crossing with no dot.
        const Sheet separated = withWiresSeparatedAt(joined, library, at);
        QCOMPARE(int(separated.wires.size()), 2);
        QVERIFY(junctions(separated, library).isEmpty());
        QVERIFY(canJoinWiresAt(separated, library, at));

        // Four wires ending at one point are two crossing lines.
        Sheet cross;
        cross.wires = {wire({mm(0, 0), mm(10, 0)}), wire({mm(10, 0), mm(10, 10)}),
                       wire({mm(20, 0), mm(10, 0)}), wire({mm(10, -10), mm(10, 0)})};
        const Sheet two = withWiresSeparatedAt(cross, library, at);
        QCOMPARE(int(two.wires.size()), 2);
        QVERIFY(junctions(two, library).isEmpty());

        // A T has nothing to cross: it stays joined.
        Sheet tee;
        tee.wires = {wire({mm(0, 0), mm(20, 0)}), wire({mm(10, 0), mm(10, 10)})};
        QVERIFY(!canSeparateWiresAt(tee, library, at));
        QVERIFY(withWiresSeparatedAt(tee, library, at) == tee);
        // Nor is a crossing on a pin taken apart or joined again.
        Sheet onPin = joined;
        onPin.components = {twoPin(mm(15, 0))};
        QVERIFY(!canSeparateWiresAt(onPin, library, at));
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

    // A wire from the right pin of a part at the origin, (4, 0), through the
    // lower pin of an upright part at (12, 0), to (20, 0) and down to
    // (20, 20). Its end dragged back to (8, 20) leaves the pin behind the
    // corner, which keeps it joined by a wire of its own. A wire carried by
    // a dragged segment keeps the pin on its middle the same way.
    void aDraggedWireKeepsThePinsItPassedThrough()
    {
        const Library library = gridLibrary();
        const auto joined = [](const Sheet &sheet, const Point &a, const Point &b) {
            for (const Wire &w : sheet.wires) {
                if (!isOnWire(w, a))
                    continue;
                const QSet<ObjectId> net = joinedWires(sheet, w.id);
                for (const Wire &other : sheet.wires) {
                    if (net.contains(other.id) && isOnWire(other, b))
                        return true;
                }
            }
            return false;
        };
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            Sheet sheet;
            Component upright = gridPart(h(12, -4));
            upright.rotation = 90;
            sheet.components = {gridPart(h(0, 0)), upright};
            sheet.wires = {wire({h(4, 0), h(20, 0), h(20, 20)})};
            const Sheet ended =
                withWireEndAt(sheet, library, sheet.wires.first().id, true, h(8, 20), routing);
            QVERIFY(joined(ended, h(4, 0), h(12, 0)));
            QVERIFY(joined(ended, h(4, 0), h(8, 20)));

            // A wire from the right pin of a part at (0, 24) through the
            // lower pin of an upright part at (20, 24) onto an upright wire
            // at x = 60, which is dragged left by 50.
            Sheet carried;
            Component lower = gridPart(h(20, 20));
            lower.rotation = 90;
            carried.components = {gridPart(h(0, 24)), lower};
            carried.wires = {wire({h(4, 24), h(60, 24)}), wire({h(60, 8), h(60, 40)})};
            const Sheet moved = withSegmentMoved(carried, library, carried.wires.at(1).id, 0,
                                                 h(-50, 0), routing);
            QVERIFY(joined(moved, h(4, 24), h(20, 24)));
        }
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

    // An upright wire with its top end on the middle of a level rail,
    // moved sideways past the end of the rail: its end slides to the end
    // of the rail and a bend leads on, instead of a bend back along the rail.
    void aSegmentSlidesAsFarAsTheWireReaches()
    {
        const Library library;
        Sheet sheet;
        sheet.wires = {wire({mm(0, 0), mm(20, 0)}), wire({mm(10, 0), mm(10, 10)})};
        const ObjectId id = sheet.wires.at(1).id;
        const QList<Wire> wires = withSegmentMoved(sheet, library, id, 0, mm(15, 0)).wires;
        QCOMPARE(wires.at(1).points, QList<Point>({mm(20, 0), mm(25, 0), mm(25, 10)}));
        QCOMPARE(wires.at(0).points, sheet.wires.at(0).points);
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

    // An upright part whose lower pin joins a level line of two wires, with
    // a third wire hanging from the right one: moved along the line past
    // it, the pin slides along the line, which stays as it is, instead of
    // one wire going round the other.
    void aPinSlidesAlongTheLineOfItsTwoWires()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0), 90)};
        sheet.wires = {wire({mm(-20, 5), mm(0, 5)}), wire({mm(0, 5), mm(20, 5)}),
                       wire({mm(3, 5), mm(3, 15)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            const QList<Wire> wires =
                withWiresFollowing(sheet, movedObjects(sheet, part, mm(6, 0)), part, library,
                                   routing)
                    .wires;
            QCOMPARE(wires.size(), 3);
            QCOMPARE(wires.at(0).points, QList<Point>({mm(-20, 5), mm(6, 5)}));
            QCOMPARE(wires.at(1).points, QList<Point>({mm(6, 5), mm(20, 5)}));
            QCOMPARE(wires.at(2).points, sheet.wires.at(2).points);
        }
        // Moved off the line as well: the line stays as it is, and a new
        // wire joins the pin to it straight across.
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            const QList<Wire> wires =
                withWiresFollowing(sheet, movedObjects(sheet, part, mm(6, -3)), part, library,
                                   routing)
                    .wires;
            QCOMPARE(wires.size(), 4);
            QCOMPARE(wires.at(0).points, sheet.wires.at(0).points);
            QCOMPARE(wires.at(1).points, sheet.wires.at(1).points);
            QCOMPARE(wires.at(2).points, sheet.wires.at(2).points);
            QCOMPARE(wires.at(3).points, QList<Point>({mm(6, 5), mm(6, 2)}));
        }
        // Moved along past the end of the right wire onto a wire going on
        // from it: it lands on its net, and that wire is cut at it, with no
        // new wire round to it.
        Sheet longer = sheet;
        longer.wires[2] = wire({mm(20, 5), mm(40, 5)});
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            const QList<Wire> wires =
                withWiresFollowing(longer, movedObjects(longer, part, mm(26, 0)), part, library,
                                   routing)
                    .wires;
            QCOMPARE(wires.size(), 4);
            QCOMPARE(wires.at(0).points, longer.wires.at(0).points);
            QCOMPARE(wires.at(1).points, longer.wires.at(1).points);
            QCOMPARE(wires.at(2).points, QList<Point>({mm(20, 5), mm(26, 5)}));
            QCOMPARE(wires.at(3).points, QList<Point>({mm(26, 5), mm(40, 5)}));
        }
    }

    // An upright wire between two level rails, moved whole: its ends slide
    // along the rails, as those of its segment do, instead of staying with
    // bends back to them.
    void aWholeWireSlidesAlongTheWiresItsEndsLieOn()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.wires = {wire({mm(0, 0), mm(20, 0)}), wire({mm(0, 10), mm(20, 10)}),
                       wire({mm(10, 0), mm(10, 10)})};
        const QSet<ObjectId> line = {sheet.wires.at(2).id};
        Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, line, mm(3, 0)), line, library);
        QCOMPARE(moved.wires.at(2).points, QList<Point>({mm(13, 0), mm(13, 10)}));
        QCOMPARE(moved.wires.at(0).points, sheet.wires.at(0).points);
        QCOMPARE(moved.wires.at(1).points, sheet.wires.at(1).points);
        // Moved aslant: its ends slide as far as straight across from where
        // they went.
        moved = withWiresFollowing(sheet, movedObjects(sheet, line, mm(3, 2)), line, library);
        QCOMPARE(moved.wires.at(2).points.first(), mm(13, 0));
        QCOMPARE(moved.wires.at(2).points.last(), mm(13, 10));
    }

    // The same wire with its top end on a rail it is moved past the end of:
    // its end slides to the end of the rail and a bend leads on.
    void aWholeWireSlidesAsFarAsTheWireReaches()
    {
        const Library library;
        Sheet sheet;
        sheet.wires = {wire({mm(0, 0), mm(20, 0)}), wire({mm(10, 0), mm(10, 10)})};
        const QSet<ObjectId> line = {sheet.wires.at(1).id};
        const QList<Wire> wires =
            withWiresFollowing(sheet, movedObjects(sheet, line, mm(15, 0)), line, library).wires;
        QCOMPARE(wires.at(1).points, QList<Point>({mm(20, 0), mm(25, 0), mm(25, 10)}));
        QCOMPARE(wires.at(0).points, sheet.wires.at(0).points);
    }

    // A level wire from the right pin of a part at the origin, with an
    // upright wire going down from the same pin, moved down whole: it
    // slides down the upright one, as its segment does.
    void aWholeWireSlidesOffAPinAlongAWireThere()
    {
        const Library library = twoPinLibrary();
        Sheet sheet;
        sheet.components = {twoPin(mm(0, 0))};
        sheet.wires = {wire({mm(5, 0), mm(5, 20)}), wire({mm(5, 0), mm(20, 0)})};
        const QSet<ObjectId> line = {sheet.wires.at(1).id};
        const Sheet moved =
            withWiresFollowing(sheet, movedObjects(sheet, line, mm(0, 4)), line, library);
        QCOMPARE(moved.wires.at(1).points, QList<Point>({mm(5, 4), mm(20, 4)}));
        QCOMPARE(moved.wires.at(0).points, sheet.wires.at(0).points);
    }

    // On the grid a wire end dragged past a part goes round it, through
    // neither its body nor its pins. Laid again from its free end it takes
    // two bends, cheaper than the four of a detour off its run.
    void gridWiresGoRoundParts()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        sheet.components = {gridPart(h(10, 0))};
        sheet.wires = {wire({h(0, 0), h(2, 0)})};
        const QList<Point> points =
            withWireEndAt(sheet, library, sheet.wires.first().id, true, h(20, 0),
                          WireRouting::Grid)
                .wires.first()
                .points;
        QCOMPARE(points.first(), h(0, 0));
        QCOMPARE(points.last(), h(20, 0));
        QCOMPARE(points.size(), 4);
        // The body with its edges, and the lines of both pins.
        QVERIFY(!passesInside(points, h(7, -2), h(13, 2)));
        QVERIFY(!isOnWire(points, h(6, 0)));
        QVERIFY(!isOnWire(points, h(14, 0)));
    }

    // A pin dragged aside: the last segment slides with it, as it does by
    // hand, rather than a step being added at the end.
    void gridWiresSlideTheLastSegment()
    {
        Sheet sheet;
        sheet.wires = {wire({h(0, 0), h(10, 0), h(10, 10)})};
        const QList<Point> points =
            withWireEndAt(sheet, Library(), sheet.wires.first().id, true, h(12, 10),
                          WireRouting::Grid)
                .wires.first()
                .points;
        QCOMPARE(points, QList<Point>({h(0, 0), h(12, 0), h(12, 10)}));
    }

    // A level segment moved up with two wires ending on it at one point, one
    // from a pin above, one from a pin below: both ends go with it, the one
    // below too, though the wire above still lay there when it was laid.
    void gridWiresFromBothSidesFollowAMovedSegment()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        sheet.components = {gridPart(h(0, -8)), gridPart(h(0, 8))};
        sheet.components[0].rotation = 90;
        sheet.components[1].rotation = 90;
        sheet.wires = {wire({h(-10, 0), h(10, 0)}), wire({h(0, -4), h(0, 0)}),
                       wire({h(0, 0), h(0, 4)})};
        const QList<Wire> wires = withSegmentMoved(sheet, library, sheet.wires.first().id, 0,
                                                   h(0, -2), WireRouting::Grid)
                                      .wires;
        QCOMPARE(wires.size(), 3);
        QCOMPARE(wires.at(0).points, QList<Point>({h(-10, -2), h(10, -2)}));
        QCOMPARE(wires.at(1).points, QList<Point>({h(0, -4), h(0, -2)}));
        QCOMPARE(wires.at(2).points, QList<Point>({h(0, -2), h(0, 4)}));
    }

    // A block moved up with a wire of it whose end lay on the middle of a
    // wire laid again, its other end going with a part: the end slides
    // along that wire and the wire still passes through it, no loop back to
    // where the end was.
    void aMovedWireEndSlidesAlongAWireLaidAgain()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        sheet.components = {gridPart(h(12, 0)), gridPart(h(16, 10))};
        sheet.wires = {wire({h(0, -20), h(0, 10), h(12, 10)}), wire({h(0, 0), h(8, 0)})};
        const QSet<ObjectId> block = {sheet.components.at(0).id, sheet.components.at(1).id,
                                      sheet.wires.at(1).id};
        const QList<Wire> wires = withWiresFollowing(sheet, movedObjects(sheet, block, h(0, -4)),
                                                     block, library, WireRouting::Grid)
                                      .wires;
        const auto byId = [&wires](const ObjectId &id) {
            return *std::find_if(wires.cbegin(), wires.cend(),
                                 [&id](const Wire &w) { return w.id == id; });
        };
        QCOMPARE(byId(sheet.wires.at(1).id).points, QList<Point>({h(0, -4), h(8, -4)}));
        QVERIFY(isOnWire(byId(sheet.wires.at(0).id), h(0, -4)));
    }

    // A segment dragged away from a pin the wire passed through, at its
    // corner or along it: the pin keeps a wire of its own to the moved one.
    void aPinOnAMovedSegmentStaysJoined()
    {
        const Library library = gridLibrary();
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            // At the corner: the left pin of a part at (14, 0).
            Sheet corner;
            corner.components = {gridPart(h(14, 0))};
            corner.wires = {wire({h(0, 0), h(10, 0), h(10, 10)})};
            QList<Wire> wires =
                withSegmentMoved(corner, library, corner.wires.first().id, 1, h(-4, 0), routing)
                    .wires;
            QCOMPARE(wires.size(), 2);
            QCOMPARE(wires.at(0).points, QList<Point>({h(0, 0), h(6, 0), h(6, 10)}));
            QCOMPARE(wires.at(1).points, QList<Point>({h(10, 0), h(6, 0)}));
            // Along it: the upper pin of an upright part under it.
            Sheet along;
            along.components = {gridPart(h(10, 4))};
            along.components[0].rotation = 90;
            along.wires = {wire({h(0, 0), h(20, 0)})};
            wires = withSegmentMoved(along, library, along.wires.first().id, 0, h(0, -2), routing)
                        .wires;
            QCOMPARE(wires.size(), 2);
            QCOMPARE(wires.at(0).points, QList<Point>({h(0, -2), h(20, -2)}));
            QCOMPARE(wires.at(1).points, QList<Point>({h(10, 0), h(10, -2)}));
            // Still on it after the move: nothing is added.
            wires = withSegmentMoved(corner, library, corner.wires.first().id, 1, h(2, 0), routing)
                        .wires;
            QCOMPARE(wires.size(), 1);
        }
    }

    // A joint slides along the wire through it, round its corner too; the
    // wires ending there follow from their far ends, which stay.
    void aJointSlidesAlongItsWire()
    {
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            Sheet sheet;
            sheet.wires = {wire({h(0, 0), h(20, 0), h(20, 10)}), wire({h(10, 10), h(10, 0)}),
                           wire({h(10, -10), h(10, 0)})};
            QCOMPARE(jointWire(sheet, Library(), h(10, 0)), sheet.wires.at(0).id);
            for (const auto &[to, at] : {qMakePair(h(14, 1), h(14, 0)),
                                         qMakePair(h(25, 5), h(20, 5))}) {
                const QList<Wire> wires = withJointMoved(sheet, Library(), h(10, 0), to,
                                                         routing).wires;
                QCOMPARE(wires.size(), 3);
                QCOMPARE(wires.at(0).points, sheet.wires.at(0).points);
                QCOMPARE(wires.at(1).points.first(), h(10, 10));
                QCOMPARE(wires.at(1).points.last(), at);
                QCOMPARE(wires.at(2).points.first(), h(10, -10));
                QCOMPARE(wires.at(2).points.last(), at);
            }
            // Onto where it is: nothing changes.
            QCOMPARE(withJointMoved(sheet, Library(), h(10, 0), h(10, 3), routing).wires,
                     sheet.wires);
        }
    }

    // No joint where no wire passes through, nothing ends, or a pin is.
    void aJointNeedsAWireThroughAndAnEnd()
    {
        Sheet sheet;
        sheet.wires = {wire({h(0, 0), h(20, 0)}), wire({h(10, 10), h(10, 0)})};
        QVERIFY(jointWire(sheet, Library(), h(5, 0)).isNull());
        QVERIFY(jointWire(sheet, Library(), h(0, 0)).isNull());
        const Library library = gridLibrary();
        sheet.components = {gridPart(h(14, 0))};
        sheet.wires = {wire({h(0, 0), h(20, 0)}), wire({h(10, 10), h(10, 0)})};
        QVERIFY(jointWire(sheet, library, h(10, 0)).isNull());
        QCOMPARE(withJointMoved(sheet, library, h(10, 0), h(4, 0)).wires, sheet.wires);
    }

    // A part dragged down: its wire, of the ways as cheap, keeps its run and
    // comes down into the pin.
    void gridWiresFollowAMovedPart()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        sheet.components = {gridPart(h(0, 0))};
        sheet.wires = {wire({h(-20, 0), h(-4, 0)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, h(0, 10)), part,
                                               library, WireRouting::Grid);
        QCOMPARE(moved.wires.first().points, QList<Point>({h(-20, 0), h(-4, 0), h(-4, 10)}));
    }

    // A wire from the right pin of a part at (8, 20) to a part at (60, 20),
    // with a wire going down from (14, 20). The part goes right by 8, along
    // its wire past that joint: the wire keeps reaching it, the pin landing
    // on it, whatever way wires are laid.
    void aPinMovedAlongItsWirePastAJointKeepsTheJoint()
    {
        const Library library = gridLibrary();
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            Sheet sheet;
            sheet.components = {gridPart(h(8, 20)), gridPart(h(60, 20))};
            sheet.wires = {wire({h(12, 20), h(56, 20)}), wire({h(14, 20), h(14, 36)})};
            const QSet<ObjectId> part = {sheet.components.first().id};
            const Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, h(8, 0)),
                                                   part, library, routing);
            const QSet<ObjectId> net = joinedWires(moved, moved.wires.last().id);
            for (const Point &point : {h(20, 20), h(56, 20)}) {
                QVERIFY(std::any_of(moved.wires.cbegin(), moved.wires.cend(), [&](const Wire &w) {
                    return net.contains(w.id) && isOnWire(w, point);
                }));
            }
        }
    }

    // A part whose wire crosses the body of another part, and a part on the
    // middle of a wire across its own pins, moved by nothing: no wire is
    // laid again, whatever way wires are laid.
    void aMoveByNothingLeavesTheWires()
    {
        const Library library = gridLibrary();
        for (const WireRouting routing : {WireRouting::Hand, WireRouting::Grid}) {
            Sheet sheet;
            sheet.components = {gridPart(h(0, 0)), gridPart(h(20, 0)), gridPart(h(34, 10))};
            sheet.wires = {wire({h(4, 0), h(4, -10), h(20, -10), h(20, 10), h(30, 10)}),
                           wire({h(-12, 20), h(-4, 20)})};
            Component across = gridPart(h(-8, 20));
            sheet.components.append(across);
            for (const ObjectId &id : {sheet.components.first().id, across.id}) {
                const QSet<ObjectId> part = {id};
                QVERIFY(withWiresFollowing(sheet, movedObjects(sheet, part, Point()), part, library,
                                           routing)
                        == sheet);
            }
        }
    }

    // Another wire is crossed where that is cheapest, but neither run along
    // nor passed through at its end, which would join them.
    void gridWiresCrossOtherWiresOnly()
    {
        Sheet sheet;
        sheet.wires = {wire({h(0, 0), h(2, 0)}), wire({h(5, -5), h(5, 5)})};
        const auto dragged = [&sheet] {
            return withWireEndAt(sheet, Library(), sheet.wires.first().id, true, h(10, 0),
                                 WireRouting::Grid)
                .wires.first()
                .points;
        };
        QCOMPARE(dragged(), QList<Point>({h(0, 0), h(10, 0)}));
        sheet.wires[1].points = {h(5, 0), h(5, 5)};
        const QList<Point> round = dragged();
        QCOMPARE(round.last(), h(10, 0));
        QVERIFY(!isOnWire(round, h(5, 0)));
        for (Coord y = 1; y <= 5; ++y)
            QVERIFY(!isOnWire(round, h(5, y)));
    }

    // A wire dragged onto a bus from below its end comes to it square,
    // not up along it.
    void gridWiresKeepOffTheLineOfABus()
    {
        Sheet sheet;
        sheet.wires = {wire({h(5, 8), h(5, 7)})};
        Bus bus;
        bus.id = newObjectId();
        bus.points = {h(5, -5), h(5, 5)};
        sheet.buses = {bus};
        const QList<Point> points =
            withWireEndAt(sheet, Library(), sheet.wires.first().id, true, h(5, -3),
                          WireRouting::Grid)
                .wires.first()
                .points;
        QCOMPARE(points.first(), h(5, 8));
        QCOMPARE(points.last(), h(5, -3));
        for (Coord y = -2; y <= 5; ++y)
            QVERIFY(!isOnWire(points, h(5, y)));
    }

    // A junction at the origin: one wire from the left, one going up, and
    // a third down and right to a part's left pin. The part dragged up: the
    // third wire leaves the junction for the nearest point of the wire going
    // up, straight across; dragged onto that wire, the wire is cut where the
    // pin lands on it and the third is gone.
    void gridWiresRejoinTheirNet()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        sheet.components = {gridPart(h(14, 5))};
        sheet.wires = {wire({h(-10, 0), h(0, 0)}), wire({h(0, 0), h(0, -10)}),
                       wire({h(0, 0), h(0, 5), h(10, 5)})};
        const ObjectId third = sheet.wires.at(2).id;
        const QSet<ObjectId> part = {sheet.components.first().id};
        const auto dragged = [&](const Point &delta) {
            return withWiresFollowing(sheet, movedObjects(sheet, part, delta), part, library,
                                      WireRouting::Grid);
        };
        Sheet moved = dragged(h(0, -10));
        QCOMPARE(moved.wires.size(), 3);
        QCOMPARE(moved.wires.at(0).points, sheet.wires.at(0).points);
        QCOMPARE(moved.wires.at(1).points, sheet.wires.at(1).points);
        QCOMPARE(moved.wires.at(2).points, QList<Point>({h(0, -5), h(10, -5)}));

        moved = dragged(h(-10, -10));
        QVERIFY(std::none_of(moved.wires.cbegin(), moved.wires.cend(),
                             [&third](const Wire &w) { return w.id == third; }));
        QCOMPARE(moved.wires.size(), 3);
        QVERIFY(std::any_of(moved.wires.cbegin(), moved.wires.cend(),
                            [](const Wire &w) { return w.points.last() == h(0, -5); }));
        // By hand the third wire stays on the junction.
        const Sheet byHand = withWiresFollowing(sheet, movedObjects(sheet, part, h(0, -10)), part,
                                                library, WireRouting::Hand);
        QCOMPARE(byHand.wires.at(2).points.first(), h(0, 0));
    }

    // Where the old wire ran is free for the new one: a pin moved onto its
    // line is reached straight from the corner of the net beside it, not
    // round the old wire's steps.
    void gridWiresTakeTheLineOfTheOldOne()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        // The pin on the left holds the junction once the wire leaves it.
        sheet.components = {gridPart(h(14, 0)), gridPart(h(-4, 0))};
        sheet.wires = {wire({h(0, 0), h(2, 0), h(2, -10)}),
                       wire({h(0, 0), h(0, 2), h(6, 2), h(6, 0), h(10, 0)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, h(2, 0)), part,
                                               library, WireRouting::Grid);
        QCOMPARE(moved.wires.size(), 2);
        QCOMPARE(moved.wires.at(0).points, sheet.wires.at(0).points);
        QCOMPARE(moved.wires.at(1).points, QList<Point>({h(2, 0), h(12, 0)}));
    }

    // A wire held only by the end of another one keeps that end: leaving it
    // for a nearer point of the net would leave the other wire hanging.
    void gridWiresLeaveNoWireHanging()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        sheet.components = {gridPart(h(14, 0))};
        sheet.wires = {wire({h(-10, 10), h(20, 10)}), wire({h(0, 10), h(0, 2)}),
                       wire({h(0, 2), h(0, 0), h(10, 0)})};
        const QSet<ObjectId> part = {sheet.components.first().id};
        const Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, h(0, 4)), part,
                                               library, WireRouting::Grid);
        QCOMPARE(moved.wires.size(), 3);
        QCOMPARE(moved.wires.at(1).points, sheet.wires.at(1).points);
        QCOMPARE(moved.wires.at(2).points.first(), h(0, 2));
        QCOMPARE(moved.wires.at(2).points.last(), h(10, 4));
    }

    // A pin moved back past a wire joined to its wire lands on that wire:
    // the wire keeps going as far as the joint, cut at the pin, with no
    // hook back to it.
    void gridWiresPassThroughAPinComeOntoThem()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        // Upright: a pin's end at (10, 0), its line going down.
        Component upright = gridPart(h(10, 4));
        upright.rotation = 90;
        sheet.components = {upright};
        sheet.wires = {wire({h(-10, 0), h(10, 0)}), wire({h(6, 0), h(6, -6), h(16, -6)})};
        const QSet<ObjectId> part = {upright.id};
        const Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, h(-6, 0)), part,
                                               library, WireRouting::Grid);
        // Cut at the pin, as a pin on the middle of a wire always is.
        QCOMPARE(moved.wires.size(), 3);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({h(-10, 0), h(4, 0)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({h(4, 0), h(6, 0)}));
        QCOMPARE(moved.wires.at(2).points, sheet.wires.at(1).points);
    }

    // The same when the pin lands on another wire joined at the joint: the
    // wire to the pin ends at the joint and the pin cuts the other one,
    // instead of the wire going round to the pin.
    void gridWiresStopWhereAPinLandsOnTheirNet()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        Component upright = gridPart(h(10, 4));
        upright.rotation = 90;
        sheet.components = {upright};
        sheet.wires = {wire({h(-10, 0), h(6, 0)}),
                       wire({h(16, -6), h(6, -6), h(6, 0), h(10, 0)})};
        const QSet<ObjectId> part = {upright.id};
        const Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, h(-6, 0)), part,
                                               library, WireRouting::Grid);
        QCOMPARE(moved.wires.size(), 3);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({h(-10, 0), h(4, 0)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({h(4, 0), h(6, 0)}));
        QCOMPARE(moved.wires.at(2).points, QList<Point>({h(16, -6), h(6, -6), h(6, 0)}));
    }

    // A pin left in place on the middle of a wire is joined to it: the wire
    // keeps it when its end goes back past it.
    void gridWiresKeepThePinsOnTheirMiddle()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        Component moving = gridPart(h(10, 4));
        moving.rotation = 90;
        // Its upper pin's end on the middle of the wire, at (6, 8).
        Component still = gridPart(h(6, 12));
        still.rotation = 90;
        sheet.components = {moving, still};
        sheet.wires = {wire({h(-10, 8), h(10, 8)})};
        const QSet<ObjectId> part = {moving.id};
        const Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, h(-6, 0)), part,
                                               library, WireRouting::Grid);
        QVERIFY(std::any_of(moved.wires.cbegin(), moved.wires.cend(),
                            [](const Wire &w) { return isOnWire(w.points, h(6, 8)); }));
        QVERIFY(std::any_of(moved.wires.cbegin(), moved.wires.cend(),
                            [](const Wire &w) { return isOnWire(w.points, h(4, 8)); }));
    }

    // An upright part whose top pin a wire comes to down an upright, with
    // another wire branching off that upright. The part goes up and right:
    // the wire keeps the upright down to the branch and ends there, and a
    // new wire goes to the pin from the nearest point of the upright,
    // instead of the wire going on round the part to come to the pin from
    // above.
    void gridWiresBranchOffTheNet()
    {
        const Library library = gridLibrary();
        Sheet sheet;
        Component upright = gridPart(h(17, 21));
        upright.rotation = 90;
        sheet.components = {upright};
        sheet.wires = {wire({h(20, 12), h(20, 17), h(17, 17)}), wire({h(20, 16), h(10, 16)})};
        const QSet<ObjectId> part = {upright.id};
        const Sheet moved = withWiresFollowing(sheet, movedObjects(sheet, part, h(4, -2)), part,
                                               library, WireRouting::Grid);
        QCOMPARE(moved.wires.size(), 3);
        QCOMPARE(moved.wires.at(0).points, QList<Point>({h(20, 12), h(20, 16)}));
        QCOMPARE(moved.wires.at(1).points, QList<Point>({h(20, 15), h(21, 15)}));
        QCOMPARE(moved.wires.at(2).points, sheet.wires.at(1).points);
    }

    // Off the grid the wire is laid by hand.
    void gridWiresOffTheGridAreLaidByHand()
    {
        Sheet sheet;
        sheet.wires = {wire({h(0, 0), h(10, 0)})};
        const Point to = h(12, 3) + Point{H / 3, 0};
        const ObjectId id = sheet.wires.first().id;
        QCOMPARE(withWireEndAt(sheet, Library(), id, true, to, WireRouting::Grid).wires,
                 withWireEndAt(sheet, Library(), id, true, to, WireRouting::Hand).wires);
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
