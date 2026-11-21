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

// R1 to R2 by one wire, R3 to R4 by another: two nets with no name.
Schematic twoUnnamedNets()
{
    Schematic schematic;
    Sheet sheet;
    sheet.components = {part(QStringLiteral("R1"), mm(0, 0)),
                        part(QStringLiteral("R2"), mm(20, 0)),
                        part(QStringLiteral("R3"), mm(0, 20)),
                        part(QStringLiteral("R4"), mm(20, 20))};
    sheet.wires = {wire({mm(5, 0), mm(15, 0)}), wire({mm(5, 20), mm(15, 20)})};
    schematic.sheets = {sheet};
    return schematic;
}

// A new project with the table names 'byPins'.
Project projectWith(const QList<NetByPins> &byPins)
{
    Project project = newDesign(QStringLiteral("t")).project;
    project.netsByPins = byPins;
    return project;
}

PinRef pinOf(const Sheet &sheet, int component, const char *number)
{
    return PinRef{sheet.components.at(component).id, QString::fromLatin1(number)};
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
        QCOMPARE(all.first().name, QStringLiteral("ppcbautoN1"));
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
        QCOMPARE(all.at(1).name, QStringLiteral("ppcbautoN2"));
        QVERIFY(all.at(1).pins.isEmpty());
    }

    void anAutomaticNameSkipsANameALabelGives()
    {
        Schematic schematic = twoUnnamedNets();
        // A label on the first net named like the first automatic name.
        schematic.sheets[0].labels = {label(QStringLiteral("ppcbautoN1"), mm(10, 0))};
        const QList<Net> all = nets(schematic, twoPinLibrary());
        QCOMPARE(int(all.size()), 2);
        QCOMPARE(all.at(0).name, QStringLiteral("ppcbautoN1"));
        QVERIFY(all.at(0).named);
        QCOMPARE(all.at(1).name, QStringLiteral("ppcbautoN2"));
        QVERIFY(!all.at(1).named);
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
        QCOMPARE(netSheets(*data, schematic), QList<int>({0, 1}));
        // R2.2 on the second sheet, the other GND symbol on the first.
        QCOMPARE(netSheets(*netNamed(all, QStringLiteral("GND")), schematic), QList<int>({0, 1}));
        QCOMPARE(netSheets(*netNamed(all, QStringLiteral("CLK")), schematic), QList<int>({0}));
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
        QCOMPARE(all.at(0).name, QStringLiteral("ppcbautoN1"));
        QCOMPARE(pinsOf(all.at(0)), QStringLiteral("R2.2"));
        QCOMPARE(all.at(1).name, QStringLiteral("ppcbautoN2"));
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
        // Drawn with nothing, it is still on the sheet of its pins.
        QCOMPARE(netSheets(all.first(), schematic), QList<int>({0}));
    }

    void aTableNameGoesToTheNetHoldingMostOfItsPins()
    {
        Schematic schematic = twoUnnamedNets();
        const Sheet &sheet = schematic.sheets.first();
        // Its pins: R1.2 and R2.1 of the first net, R3.2 of the second.
        const NetByPins byPins{QStringLiteral("NN1"),
                               {pinOf(sheet, 0, "2"), pinOf(sheet, 1, "1"), pinOf(sheet, 2, "2")}};
        QList<Net> all = nets(schematic, twoPinLibrary(), projectWith({byPins}));
        QCOMPARE(int(all.size()), 2);
        QCOMPARE(all.at(0).name, QStringLiteral("NN1"));
        QVERIFY(all.at(0).named);
        QCOMPARE(all.at(0).tableName, QStringLiteral("NN1"));
        QCOMPARE(pinsOf(all.at(0)), QStringLiteral("R1.2, R2.1"));
        // The other is numbered as if alone.
        QCOMPARE(all.at(1).name, QStringLiteral("ppcbautoN1"));
        QVERIFY(all.at(1).tableName.isEmpty());

        // Renumbered parts keep it: the ids are the same.
        schematic.sheets[0].components[0].designator = QStringLiteral("R9");
        all = nets(schematic, twoPinLibrary(), projectWith({byPins}));
        QCOMPARE(netNamed(all, QStringLiteral("NN1"))->pins.first().designator,
                 QStringLiteral("R2"));

        // Equal shares: the net whose first pin comes first, R1.2 before R3.2.
        schematic = twoUnnamedNets();
        const Sheet &fresh = schematic.sheets.first();
        const NetByPins even{QStringLiteral("NN1"),
                             {pinOf(fresh, 2, "2"), pinOf(fresh, 0, "2")}};
        all = nets(schematic, twoPinLibrary(), projectWith({even}));
        QCOMPARE(pinsOf(*netNamed(all, QStringLiteral("NN1"))), QStringLiteral("R1.2, R2.1"));

        // Two on one net, with no class: the first by name names it.
        const NetByPins second{QStringLiteral("NN2"), {pinOf(fresh, 0, "2")}};
        const NetByPins first{QStringLiteral("NN1"), {pinOf(fresh, 1, "1")}};
        all = nets(schematic, twoPinLibrary(), projectWith({second, first}));
        QCOMPARE(pinsOf(*netNamed(all, QStringLiteral("NN1"))), QStringLiteral("R1.2, R2.1"));
        QCOMPARE(netNamed(all, QStringLiteral("NN1"))->mergedTableNames,
                 QStringList({QStringLiteral("NN2")}));
        QVERIFY(netNamed(all, QStringLiteral("NN2")) == nullptr);

        // None of its pins on a net any more: no net takes it.
        const NetByPins gone{QStringLiteral("NN1"), {PinRef{newObjectId(), QStringLiteral("1")}}};
        all = nets(schematic, twoPinLibrary(), projectWith({gone}));
        QVERIFY(netNamed(all, QStringLiteral("NN1")) == nullptr);
    }

    // Two nets with classes joined by a wire: one net, of the stronger class
    // and named by it; choosing its class again forgets the other name.
    void joinedNetsTakeTheStrongerClass()
    {
        Schematic schematic = twoUnnamedNets();
        const Sheet &sheet = schematic.sheets.first();
        const Library library = twoPinLibrary();
        Project project = projectWith({NetByPins{QStringLiteral("NN1"), {pinOf(sheet, 0, "2"),
                                                                       pinOf(sheet, 1, "1")}},
                                       NetByPins{QStringLiteral("NN2"), {pinOf(sheet, 2, "2"),
                                                                       pinOf(sheet, 3, "1")}}});
        project.classOfNet.insert(QStringLiteral("NN1"), project.defaultClass);
        project.classOfNet.insert(QStringLiteral("NN2"), project.powerClass);
        QVERIFY(strongerClass(project, project.powerClass, project.defaultClass));
        QVERIFY(!strongerClass(project, QStringLiteral("Nowhere"), project.defaultClass));

        // R2.1 to R3.2.
        schematic.sheets[0].wires.append(wire({mm(15, 0), mm(15, 10), mm(5, 10), mm(5, 20)}));
        QList<Net> all = nets(schematic, library, project);
        QCOMPARE(int(all.size()), 1);
        const Net joined = all.first();
        QCOMPARE(joined.name, QStringLiteral("NN2"));
        QCOMPARE(joined.mergedTableNames, QStringList({QStringLiteral("NN1")}));
        QCOMPARE(netClassOf(joined, project, schematic), project.powerClass);

        project = withNetClass(project, all, joined, project.powerClass);
        QCOMPARE(int(project.netsByPins.size()), 1);
        QCOMPARE(project.netsByPins.first().name, QStringLiteral("NN2"));
        QCOMPARE(int(project.netsByPins.first().pins.size()), 4);
        QVERIFY(!project.classOfNet.contains(QStringLiteral("NN1")));
    }

    void aLabelNamesANetBeforeItsTableName()
    {
        Schematic schematic = twoUnnamedNets();
        schematic.sheets[0].labels = {label(QStringLiteral("DATA"), mm(10, 0))};
        const Sheet &sheet = schematic.sheets.first();
        const NetByPins byPins{QStringLiteral("NN1"), {pinOf(sheet, 0, "2")}};
        const QList<Net> all = nets(schematic, twoPinLibrary(), projectWith({byPins}));
        const Net *data = netNamed(all, QStringLiteral("DATA"));
        QVERIFY(data != nullptr);
        QCOMPARE(data->tableName, QStringLiteral("NN1"));
        QVERIFY(data->otherNames.isEmpty());
    }

    void theClassOfANet()
    {
        Project project = newDesign(QStringLiteral("t")).project;
        const QString signal = project.defaultClass;
        const QString powerClass = project.powerClass;
        QVERIFY(signal != powerClass);

        Schematic schematic = twoUnnamedNets();
        schematic.sheets[0].labels = {label(QStringLiteral("DATA"), mm(10, 0))};
        schematic.sheets[0].powerSymbols = {power(QStringLiteral("GND"), mm(10, 20))};
        const Library library = twoPinLibrary();
        QList<Net> all = nets(schematic, library);
        const Net data = *netNamed(all, QStringLiteral("DATA"));
        const Net ground = *netNamed(all, QStringLiteral("GND"));
        QCOMPARE(netClassOf(data, project, schematic), signal);
        QCOMPARE(netClassOf(ground, project, schematic), powerClass);

        // A label's class, unless it is not in the project.
        schematic.sheets[0].labels[0].netClass = powerClass;
        QCOMPARE(netClassOf(data, project, schematic), powerClass);
        schematic.sheets[0].labels[0].netClass = QStringLiteral("Nowhere");
        QCOMPARE(netClassOf(data, project, schematic), signal);
        schematic.sheets[0].labels[0].netClass = powerClass;

        // Chosen in the table, by name: before the label's.
        project = withNetClass(project, all, data, signal);
        QCOMPARE(project.classOfNet.value(QStringLiteral("DATA")), signal);
        QVERIFY(project.netsByPins.isEmpty());
        QCOMPARE(netClassOf(data, project, schematic), signal);
    }

    void aClassChosenForANetWithNoNameNamesItByItsPins()
    {
        const Schematic schematic = twoUnnamedNets();
        const Library library = twoPinLibrary();
        Project project = newDesign(QStringLiteral("t")).project;
        const QString powerClass = project.powerClass;
        // A name a net has already is skipped.
        project.classOfNet.insert(QStringLiteral("NN1"), powerClass);

        QList<Net> all = nets(schematic, library, project);
        QCOMPARE(all.at(1).name, QStringLiteral("ppcbautoN2"));
        // A class by an automatic name counts for nothing.
        project.classOfNet.insert(QStringLiteral("ppcbautoN2"), powerClass);
        QCOMPARE(netClassOf(all.at(1), project, schematic), project.defaultClass);

        project = withNetClass(project, all, all.at(1), powerClass);
        QCOMPARE(int(project.netsByPins.size()), 1);
        QCOMPARE(project.netsByPins.first().name, QStringLiteral("NN2"));
        QCOMPARE(int(project.netsByPins.first().pins.size()), 2);
        QCOMPARE(project.classOfNet.value(QStringLiteral("NN2")), powerClass);

        all = nets(schematic, library, project);
        const Net *named = netNamed(all, QStringLiteral("NN2"));
        QVERIFY(named != nullptr);
        QCOMPARE(pinsOf(*named), QStringLiteral("R3.2, R4.1"));
        QCOMPARE(netClassOf(*named, project, schematic), powerClass);

        // Chosen again: the same name, the class changed.
        project = withNetClass(project, all, *named, project.defaultClass);
        QCOMPARE(int(project.netsByPins.size()), 1);
        QCOMPARE(project.classOfNet.value(QStringLiteral("NN2")), project.defaultClass);

        // A net of wires alone is left as it is.
        Schematic bare;
        bare.sheets = {Sheet()};
        bare.sheets[0].wires = {wire({mm(0, 0), mm(10, 0)})};
        const QList<Net> wires = nets(bare, library);
        QVERIFY(withNetClass(project, wires, wires.first(), powerClass) == project);
    }

    // A class renamed is given by its new name wherever it was given; what
    // gave a class deleted gives the default one, or nothing for a label.
    void netClassesEditedFollowEverywhere()
    {
        Design design = newDesign(QStringLiteral("amp"));
        NetClass high;
        high.name = QStringLiteral("High");
        high.trackWidth = 3 * NM_PER_MM;
        design.project.netClasses.append(high);
        const QString signal = design.project.defaultClass;
        const QString power = design.project.powerClass;
        design.project.classOfNet = {{QStringLiteral("VCC"), power},
                                     {QStringLiteral("NN1"), QStringLiteral("High")}};
        Label powerLabel;
        powerLabel.netClass = power;
        Label highLabel;
        highLabel.netClass = QStringLiteral("High");
        Label plain;
        design.schematic.sheets[0].labels = {powerLabel, highLabel, plain};
        QVERIFY(isNetClassUsed(design, QStringLiteral("High")));
        QVERIFY(!isNetClassUsed(design, signal));

        // Power renamed to Main and made the default; High deleted; Thin added.
        NetClass main = design.project.netClasses.at(1);
        main.name = QStringLiteral("Main");
        NetClass thin;
        thin.name = QStringLiteral("Thin");
        const QList<NetClassEdit> edits = {{signal, design.project.netClasses.at(0)},
                                           {power, main},
                                           {QString(), thin}};
        const Design edited = withNetClasses(design, edits, QStringLiteral("Main"), signal);
        QCOMPARE(int(edited.project.netClasses.size()), 3);
        QCOMPARE(edited.project.netClasses.at(1).name, QStringLiteral("Main"));
        QCOMPARE(edited.project.netClasses.at(2).name, QStringLiteral("Thin"));
        QCOMPARE(edited.project.defaultClass, QStringLiteral("Main"));
        QCOMPARE(edited.project.powerClass, signal);
        QCOMPARE(edited.project.classOfNet.value(QStringLiteral("VCC")), QStringLiteral("Main"));
        QCOMPARE(edited.project.classOfNet.value(QStringLiteral("NN1")), QStringLiteral("Main"));
        const QList<Label> &labels = edited.schematic.sheets.at(0).labels;
        QCOMPARE(labels.at(0).netClass, QStringLiteral("Main"));
        QVERIFY(labels.at(1).netClass.isEmpty());
        QVERIFY(labels.at(2).netClass.isEmpty());
        QVERIFY(!isNetClassUsed(edited, QStringLiteral("High")));
    }
};

QTEST_GUILESS_MAIN(TestNetlist)
#include "test_netlist.moc"
