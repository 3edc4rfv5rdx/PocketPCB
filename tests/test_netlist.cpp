#include "netlist.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

// A part with pins "1" at (-5, 0) and "2" at (5, 0) mm.
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

Component part(const QString &designator, const Point &at)
{
    Component component;
    component.id = newObjectId();
    component.part = QStringLiteral("two");
    component.designator = designator;
    component.at = at;
    return component;
}

Wire wire(const QList<Point> &points)
{
    Wire result;
    result.id = newObjectId();
    result.points = points;
    return result;
}

Label label(const QString &text, const Point &at)
{
    Label result;
    result.id = newObjectId();
    result.text = text;
    result.at = at;
    return result;
}

PowerSymbol power(const QString &name, const Point &at)
{
    PowerSymbol result;
    result.id = newObjectId();
    result.name = name;
    result.at = at;
    return result;
}

// "R1.2, R2.1": the pins of a net.
QString pinsOf(const Net &net)
{
    QStringList pins;
    for (const NetPin &pin : net.pins)
        pins.append(pin.designator + QLatin1Char('.') + pin.number);
    return pins.join(QStringLiteral(", "));
}

const Net *netNamed(const QList<Net> &all, const QString &name)
{
    for (const Net &net : all) {
        if (net.name == name)
            return &net;
    }
    return nullptr;
}

} // namespace

class TestNetlist : public QObject
{
    Q_OBJECT

private slots:
    void numbersAreReadAsNumbers()
    {
        QVERIFY(naturalLess(QStringLiteral("R2"), QStringLiteral("R10")));
        QVERIFY(!naturalLess(QStringLiteral("R10"), QStringLiteral("R2")));
        QVERIFY(naturalLess(QStringLiteral("C1"), QStringLiteral("R1")));
        QVERIFY(naturalLess(QStringLiteral("R1"), QStringLiteral("R1A")));
        QVERIFY(naturalLess(QStringLiteral("D007"), QStringLiteral("D8")));
        QVERIFY(!naturalLess(QStringLiteral("R1"), QStringLiteral("R1")));
    }

    // R1 at (0, 0) and R2 at (20, 0): R1.2 at (5, 0), R2.1 at (15, 0).
    void wiresJoinPins()
    {
        Schematic schematic;
        Sheet sheet;
        sheet.components = {part(QStringLiteral("R1"), mm(0, 0)),
                            part(QStringLiteral("R2"), mm(20, 0))};
        sheet.wires = {wire({mm(5, 0), mm(15, 0)})};
        schematic.sheets = {sheet};
        const QList<Net> all = nets(schematic, twoPinLibrary());
        // The free pins make no nets.
        QCOMPARE(int(all.size()), 1);
        QCOMPARE(all.first().name, QStringLiteral("N1"));
        QVERIFY(!all.first().named);
        QCOMPARE(pinsOf(all.first()), QStringLiteral("R1.2, R2.1"));
        QCOMPARE(all.first().objects.value(0), QSet<ObjectId>({sheet.wires.first().id}));
    }

    void wiresJoinWhereAnEndLiesOnAnother()
    {
        Schematic schematic;
        Sheet sheet;
        sheet.components = {part(QStringLiteral("R1"), mm(0, 0)),
                            part(QStringLiteral("R2"), mm(20, 20))};
        // From R1.2 to the right; from the middle of that one down to R2.1,
        // joined to it by its end; and one that only crosses the first.
        sheet.wires = {wire({mm(5, 0), mm(15, 0)}), wire({mm(10, 0), mm(10, 20), mm(15, 20)}),
                       wire({mm(12, -10), mm(12, 10)})};
        schematic.sheets = {sheet};
        const QList<Net> all = nets(schematic, twoPinLibrary());
        QCOMPARE(int(all.size()), 2);
        QCOMPARE(pinsOf(all.at(0)), QStringLiteral("R1.2, R2.1"));
        QCOMPARE(int(all.at(0).objects.value(0).size()), 2);
        // The crossing wire is a net of its own, with no pins, numbered last.
        QCOMPARE(all.at(1).name, QStringLiteral("N2"));
        QVERIFY(all.at(1).pins.isEmpty());
    }

    void pinsAtOnePointAreJoined()
    {
        Schematic schematic;
        Sheet sheet;
        // R2.1 on R1.2, at (5, 0).
        sheet.components = {part(QStringLiteral("R1"), mm(0, 0)),
                            part(QStringLiteral("R2"), mm(10, 0))};
        schematic.sheets = {sheet};
        const QList<Net> all = nets(schematic, twoPinLibrary());
        QCOMPARE(int(all.size()), 1);
        QCOMPARE(pinsOf(all.first()), QStringLiteral("R1.2, R2.1"));
    }

    // Labels of one text join on a sheet and across sheets; a power symbol
    // names its net before a label does.
    void labelsAndPowerSymbolsJoinByName()
    {
        Schematic schematic;
        Sheet first;
        first.components = {part(QStringLiteral("R1"), mm(0, 0))};
        first.wires = {wire({mm(5, 0), mm(15, 0)})};
        first.labels = {label(QStringLiteral("DATA"), mm(10, 0))};
        // A label right on a pin, with no wire.
        first.labels.append(label(QStringLiteral("CLK"), mm(-5, 0)));
        Sheet second;
        second.components = {part(QStringLiteral("R2"), mm(0, 0))};
        second.wires = {wire({mm(-15, 0), mm(-5, 0)})};
        second.labels = {label(QStringLiteral("DATA"), mm(-15, 0))};
        // A power symbol right on R2.2, and another of that name elsewhere
        // on the first sheet, on a wire also labelled.
        second.powerSymbols = {power(QStringLiteral("GND"), mm(5, 0))};
        first.powerSymbols = {power(QStringLiteral("GND"), mm(30, 0))};
        first.wires.append(wire({mm(30, 0), mm(40, 0)}));
        first.labels.append(label(QStringLiteral("AGND"), mm(40, 0)));
        schematic.sheets = {first, second};

        const QList<Net> all = nets(schematic, twoPinLibrary());
        // Named nets come first, in the order of their names.
        QCOMPARE(int(all.size()), 3);
        QCOMPARE(all.at(0).name, QStringLiteral("CLK"));
        QCOMPARE(all.at(1).name, QStringLiteral("DATA"));
        QCOMPARE(all.at(2).name, QStringLiteral("GND"));

        const Net *data = netNamed(all, QStringLiteral("DATA"));
        QVERIFY(data->named);
        QCOMPARE(pinsOf(*data), QStringLiteral("R1.2, R2.1"));
        QCOMPARE(data->objects.keys(), QList<int>({0, 1}));
        QVERIFY(data->otherNames.isEmpty());

        const Net *ground = netNamed(all, QStringLiteral("GND"));
        QCOMPARE(pinsOf(*ground), QStringLiteral("R2.2"));
        QCOMPARE(ground->otherNames, QStringList({QStringLiteral("AGND")}));
        QCOMPARE(pinsOf(*netNamed(all, QStringLiteral("CLK"))), QStringLiteral("R1.1"));
    }

    void unnamedNetsAreNumberedByTheirFirstPin()
    {
        Schematic schematic;
        Sheet sheet;
        // R10 first on the sheet, R2 second: R2's net is N1.
        sheet.components = {part(QStringLiteral("R10"), mm(0, 0)),
                            part(QStringLiteral("R2"), mm(0, 30))};
        sheet.wires = {wire({mm(5, 0), mm(5, 10)}), wire({mm(5, 30), mm(5, 40)})};
        schematic.sheets = {sheet};
        const QList<Net> all = nets(schematic, twoPinLibrary());
        QCOMPARE(int(all.size()), 2);
        QCOMPARE(all.at(0).name, QStringLiteral("N1"));
        QCOMPARE(pinsOf(all.at(0)), QStringLiteral("R2.2"));
        QCOMPARE(all.at(1).name, QStringLiteral("N2"));
        QCOMPARE(pinsOf(all.at(1)), QStringLiteral("R10.2"));
        // Built again, the same.
        const QList<Net> again = nets(schematic, twoPinLibrary());
        QCOMPARE(again.at(0).name, all.at(0).name);
        QCOMPARE(again.at(0).pins, all.at(0).pins);
    }

    void busesAndMarksJoinNothing()
    {
        Schematic schematic;
        Sheet sheet;
        sheet.components = {part(QStringLiteral("R1"), mm(0, 0)),
                            part(QStringLiteral("R2"), mm(30, 0))};
        Bus bus;
        bus.id = newObjectId();
        bus.points = {mm(0, -20), mm(40, -20)};
        bus.name = QStringLiteral("BUS1");
        sheet.buses = {bus};
        // Both pins wired to the bus: still not joined.
        sheet.wires = {wire({mm(5, 0), mm(5, -20)}), wire({mm(25, 0), mm(25, -20)})};
        NoConnect mark;
        mark.id = newObjectId();
        mark.at = mm(-5, 0);
        sheet.noConnects = {mark};
        schematic.sheets = {sheet};
        const QList<Net> all = nets(schematic, twoPinLibrary());
        QCOMPARE(int(all.size()), 2);
        QCOMPARE(pinsOf(all.at(0)), QStringLiteral("R1.2"));
        QCOMPARE(pinsOf(all.at(1)), QStringLiteral("R2.1"));
    }

    // The mouse finds a net by its wires, labels and power symbols on the
    // sheet looked at, and by the lines of its pins.
    void netsAreFoundUnderThePoint()
    {
        Schematic schematic;
        Sheet first;
        first.components = {part(QStringLiteral("R1"), mm(0, 0))};
        first.wires = {wire({mm(5, 0), mm(15, 0)})};
        first.labels = {label(QStringLiteral("DATA"), mm(15, 0))};
        Sheet second;
        second.wires = {wire({mm(0, 20), mm(10, 20)})};
        second.labels = {label(QStringLiteral("DATA"), mm(0, 20))};
        // Its symbol is not in the library: found by the square drawn instead.
        second.powerSymbols = {power(QStringLiteral("GND"), mm(30, 30))};
        schematic.sheets = {first, second};
        const Library library = twoPinLibrary();
        const QList<Net> all = nets(schematic, library);
        const int data = int(netNamed(all, QStringLiteral("DATA")) - all.constData());
        const int ground = int(netNamed(all, QStringLiteral("GND")) - all.constData());
        QCOMPARE(int(all.size()), 2);
        const double tolerance = 0.5;

        QCOMPARE(netAt(all, schematic, library, 0, QPointF(10, 0.4), tolerance), data);
        QCOMPARE(netAt(all, schematic, library, 1, QPointF(5, 20), tolerance), data);
        QCOMPARE(netAt(all, schematic, library, 1, QPointF(30, 30), tolerance), ground);
        // R1.2's line, from (5, 0) towards the body, off the wire's end.
        QCOMPARE(netAt(all, schematic, library, 0, QPointF(3.5, 0.2), tolerance), data);
        // R1.1 is joined to nothing; R1's middle, empty paper, the power
        // symbol's place on the other sheet, a sheet that is not there.
        QCOMPARE(netAt(all, schematic, library, 0, QPointF(-3.5, 0), tolerance), -1);
        QCOMPARE(netAt(all, schematic, library, 0, QPointF(0, 0), tolerance), -1);
        QCOMPARE(netAt(all, schematic, library, 0, QPointF(10, 8), tolerance), -1);
        QCOMPARE(netAt(all, schematic, library, 0, QPointF(30, 30), tolerance), -1);
        QCOMPARE(netAt(all, schematic, library, 2, QPointF(10, 0), tolerance), -1);
    }

    // Pins joined at one point make a net drawn with nothing: their lines
    // still find it.
    void aNetOfPinsAloneIsFoundByThem()
    {
        Schematic schematic;
        Sheet sheet;
        sheet.components = {part(QStringLiteral("R1"), mm(0, 0)),
                            part(QStringLiteral("R2"), mm(10, 0))};
        schematic.sheets = {sheet};
        const Library library = twoPinLibrary();
        const QList<Net> all = nets(schematic, library);
        QCOMPARE(int(all.size()), 1);
        QVERIFY(netHasPin(all.first(), sheet.components.at(1).id, QStringLiteral("1")));
        QVERIFY(!netHasPin(all.first(), sheet.components.at(1).id, QStringLiteral("2")));
        QCOMPARE(netAt(all, schematic, library, 0, QPointF(4, 0), 0.5), 0);
        QCOMPARE(netAt(all, schematic, library, 0, QPointF(6, 0), 0.5), 0);
        QCOMPARE(netAt(all, schematic, library, 0, QPointF(14, 0), 0.5), -1);
    }
};

QTEST_GUILESS_MAIN(TestNetlist)
#include "test_netlist.moc"
