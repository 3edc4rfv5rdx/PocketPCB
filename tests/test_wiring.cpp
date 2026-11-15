#include "wiring.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

// A symbol with pins at (-5, 0) and (5, 0) mm.
Library twoPinLibrary()
{
    Symbol symbol;
    Pin left;
    left.number = QStringLiteral("1");
    left.at = mm(-5, 0);
    Pin right = left;
    right.number = QStringLiteral("2");
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
};

QTEST_GUILESS_MAIN(TestWiring)
#include "test_wiring.moc"
