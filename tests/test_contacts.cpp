#include "contacts.h"

#include "document.h"
#include "paint.h"
#include "wiring.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

Pin pinAt(const QString &number, const Point &at, PinSide side)
{
    Pin pin;
    pin.number = number;
    pin.at = at;
    pin.side = side;
    return pin;
}

Part partOf(const QString &symbol, const QStringList &footprints, bool connector)
{
    Part part;
    part.symbol = symbol;
    part.footprints = footprints;
    part.prefix = connector ? QStringLiteral("J") : QStringLiteral("R");
    part.prefixGost = connector ? QStringLiteral("X") : QStringLiteral("R");
    part.connector = connector;
    return part;
}

// The library a contact is taken from: the wire pad, its pin 2.54 mm left
// of its origin.
Library source()
{
    Symbol pad;
    pad.pins = {pinAt(QStringLiteral("1"), Point{-PIN_GRID, 0}, PinSide::Left)};
    pad.refAt = Point{0, -PIN_GRID};
    Footprint footprint;
    Library library;
    library.symbols.insert(QLatin1String(WIRE_PAD_PART), pad);
    library.footprints.insert(QLatin1String(WIRE_PAD_PART), footprint);
    library.parts.insert(QLatin1String(WIRE_PAD_PART),
                         partOf(QLatin1String(WIRE_PAD_PART), {QLatin1String(WIRE_PAD_PART)}, true));
    return library;
}

Component component(const QString &part, const QString &designator, const Point &at,
                    bool offBoard)
{
    Component result;
    result.id = newObjectId();
    result.part = part;
    result.designator = designator;
    result.at = at;
    result.offBoard = offBoard;
    result.footprint = offBoard ? QString() : QStringLiteral("small");
    return result;
}

// A jack off the board, its pin 1 at (-5, 0) going left to a resistor on
// the board, its pin 2 at (5, 0) on a switch also off it; a second jack,
// turned, whose pin 2 goes down to the resistor's other pin.
struct Amplifier
{
    Design design;
    ObjectId jack;
    ObjectId otherJack;
};

Amplifier amplifier()
{
    Amplifier result;
    Library &library = result.design.library;
    Symbol two;
    two.pins = {pinAt(QStringLiteral("1"), mm(-5, 0), PinSide::Left),
                pinAt(QStringLiteral("2"), mm(5, 0), PinSide::Right)};
    library.symbols.insert(QStringLiteral("two"), two);
    library.footprints.insert(QStringLiteral("small"), Footprint());
    library.parts.insert(QStringLiteral("jack"), partOf(QStringLiteral("two"), {}, false));
    library.parts.insert(QStringLiteral("resistor"),
                         partOf(QStringLiteral("two"), {QStringLiteral("small")}, false));
    Sheet sheet;
    const Component jack = component(QStringLiteral("jack"), QStringLiteral("XS1"), mm(0, 0), true);
    // Its pin 2 at (-10, 0), on the jack's pin 1; its pin 1 at (-20, 0).
    const Component resistor =
        component(QStringLiteral("resistor"), QStringLiteral("R1"), mm(-15, 0), false);
    // Its pin 1 at (5, 0), on the jack's pin 2.
    const Component toggle =
        component(QStringLiteral("jack"), QStringLiteral("SA1"), mm(10, 0), true);
    // Turned a quarter: its pin 2 at (-20, -5), above the resistor's pin 1,
    // pointing down to it.
    Component other = component(QStringLiteral("jack"), QStringLiteral("XS2"), mm(-20, -10), true);
    other.rotation = 90;
    sheet.components = {jack, resistor, toggle, other};
    Wire link;
    link.id = newObjectId();
    link.points = {mm(-5, 0), mm(-10, 0)};
    Wire down;
    down.id = newObjectId();
    down.points = {mm(-20, -5), mm(-20, 0)};
    sheet.wires = {link, down};
    result.design.schematic.sheets = {sheet};
    result.jack = jack.id;
    result.otherJack = other.id;
    return result;
}

const Component *placedContact(const Design &design, const PlacedContacts &placed,
                               const Point &pinPoint)
{
    for (const Component &each : design.schematic.sheets.first().components) {
        if (placed.placed.contains(each.id)
            && componentPoint(each, Point{-PIN_GRID, 0}) == pinPoint)
            return &each;
    }
    return nullptr;
}

} // namespace

class TestContacts : public QObject
{
    Q_OBJECT

private slots:
    // Every pin of an off-board part gets a wire pad, whatever it is joined
    // to: the two jacks and the switch, two pins each.
    void everyPinOfAnOffBoardPartGetsAPad()
    {
        const Amplifier amp = amplifier();
        const PlacedContacts placed = withContactsPlaced(amp.design, source());
        QCOMPARE(placed.placed.size(), 6);
        QVERIFY(placed.design.library.parts.contains(QLatin1String(WIRE_PAD_PART)));

        // On the jack's pin, its body further left, on the wire.
        const Component *left = placedContact(placed.design, placed, mm(-5, 0));
        QVERIFY(left != nullptr);
        QCOMPARE(left->rotation, 180);
        QCOMPARE(left->at, (mm(-5, 0) - Point{PIN_GRID, 0}));
        QVERIFY(!left->offBoard);
        QCOMPARE(left->footprint, QLatin1String(WIRE_PAD_PART));
        // Its designator turned with it: below, as the turn puts it.
        QCOMPARE(left->refAt, (Point{0, PIN_GRID}));

        // The turned jack's pin points down: so does its contact.
        const Component *down = placedContact(placed.design, placed, mm(-20, -5));
        QVERIFY(down != nullptr);
        QCOMPARE(down->rotation, 90);

        // Numbered on: J1 to J6.
        QSet<QString> designators;
        for (const Component &each : placed.design.schematic.sheets.first().components) {
            if (placed.placed.contains(each.id))
                designators.insert(each.designator);
        }
        QCOMPARE(designators, QSet<QString>({QStringLiteral("J1"), QStringLiteral("J2"),
                                             QStringLiteral("J3"), QStringLiteral("J4"),
                                             QStringLiteral("J5"), QStringLiteral("J6")}));
    }

    // A pin marked not connected gets none.
    void aPinMarkedNotConnectedGetsNone()
    {
        Amplifier amp = amplifier();
        NoConnect mark;
        mark.id = newObjectId();
        // The turned jack's pin 1, joined to nothing.
        mark.at = mm(-20, -15);
        amp.design.schematic.sheets[0].noConnects.append(mark);
        const PlacedContacts placed = withContactsPlaced(amp.design, source(), {amp.otherJack});
        QCOMPARE(placed.placed.size(), 1);
        QVERIFY(placedContact(placed.design, placed, mm(-20, -15)) == nullptr);
    }

    // A pin with a wire pad on its end gets no other.
    void aPinWithAPadGetsNoMore()
    {
        const Amplifier amp = amplifier();
        const PlacedContacts once = withContactsPlaced(amp.design, source());
        const PlacedContacts twice = withContactsPlaced(once.design, source());
        QVERIFY(twice.placed.isEmpty());
        QCOMPARE(twice.design, once.design);
    }

    // Only the parts asked for: the turned jack's two pins.
    void onlyTheChosenOffBoardPartsGetContacts()
    {
        const Amplifier amp = amplifier();
        const PlacedContacts placed = withContactsPlaced(amp.design, source(), {amp.otherJack});
        QCOMPARE(placed.placed.size(), 2);
        QVERIFY(placedContact(placed.design, placed, mm(-20, -5)) != nullptr);
        QVERIFY(placedContact(placed.design, placed, mm(-20, -15)) != nullptr);
        // Then the first jack: its own two, the other's kept.
        const PlacedContacts then = withContactsPlaced(placed.design, source(), {amp.jack});
        QCOMPARE(then.placed.size(), 2);
    }

    // A wire pad on the board is labelled with the off-board pins of its
    // net, or the net's name when it has none; other parts are not.
    void wirePadsAreLabelledWhereTheirWireGoes()
    {
        Amplifier amp = amplifier();
        const QList<Net> all = nets(amp.design.schematic, amp.design.library);
        QString jackNet;
        for (const Net &net : all) {
            const bool jack = std::any_of(net.pins.cbegin(), net.pins.cend(), [](const NetPin &p) {
                return p.designator == QStringLiteral("XS1") && p.number == QStringLiteral("1");
            });
            if (jack)
                jackNet = net.name;
        }
        QVERIFY(!jackNet.isEmpty());
        // A wire pad of the schematic on the board, and a potentiometer on
        // wire pads, whose wires are its own.
        const Library wirePads = source();
        amp.design.library.parts.insert(QLatin1String(WIRE_PAD_PART),
                                        wirePads.parts.value(QLatin1String(WIRE_PAD_PART)));
        amp.design.library.symbols.insert(QLatin1String(WIRE_PAD_PART),
                                          wirePads.symbols.value(QLatin1String(WIRE_PAD_PART)));
        amp.design.library.footprints.insert(QLatin1String(WIRE_PAD_PART), Footprint());
        amp.design.library.parts.insert(
            QStringLiteral("pot"),
            partOf(QStringLiteral("two"), {QLatin1String(WIRE_PAD_PART)}, false));
        Component padComponent =
            component(QLatin1String(WIRE_PAD_PART), QStringLiteral("X9"), mm(50, 50), false);
        padComponent.footprint = QLatin1String(WIRE_PAD_PART);
        Component potComponent =
            component(QStringLiteral("pot"), QStringLiteral("R9"), mm(60, 50), false);
        potComponent.footprint = QLatin1String(WIRE_PAD_PART);
        amp.design.schematic.sheets[0].components.append(padComponent);
        amp.design.schematic.sheets[0].components.append(potComponent);
        Placement pad;
        pad.id = newObjectId();
        pad.component = padComponent.id;
        pad.footprint = QLatin1String(WIRE_PAD_PART);
        pad.padNets.insert(QStringLiteral("1"), jackNet);
        QCOMPARE(wirePadLabel(pad, all, amp.design.schematic), QStringLiteral("XS1.1"));
        // A net with no off-board pin is told by its name.
        Placement other = pad;
        other.padNets.insert(QStringLiteral("1"), QStringLiteral("GND"));
        QCOMPARE(wirePadLabel(other, all, amp.design.schematic), QStringLiteral("GND"));
        Placement resistor = pad;
        resistor.id = newObjectId();
        resistor.footprint = QStringLiteral("small");
        Placement pot = pad;
        pot.id = newObjectId();
        pot.component = potComponent.id;
        QVERIFY(isWirePad(pad, amp.design));
        QVERIFY(!isWirePad(resistor, amp.design));
        QVERIFY(!isWirePad(pot, amp.design));

        // The document makes the labels; they are not part of what is
        // saved or compared.
        amp.design.board.placements = {pad, resistor, pot};
        Document document;
        document.startNew();
        document.change(QStringLiteral("Board"), amp.design);
        const QList<Placement> &placements = document.design().board.placements;
        QCOMPARE(placements.at(0).padLabel, QStringLiteral("XS1.1"));
        QVERIFY(placements.at(1).padLabel.isEmpty());
        QVERIFY(placements.at(2).padLabel.isEmpty());
        // A label alone is no change: no step is made for it.
        const int steps = document.undoStack()->count();
        Design unlabelled = document.design();
        unlabelled.board.placements[0].padLabel.clear();
        QVERIFY(unlabelled == document.design());
        document.change(QStringLiteral("Nothing"), unlabelled);
        QCOMPARE(document.undoStack()->count(), steps);

        // On the screen always, in place of the value; on the silkscreen
        // only while the value is shown.
        const Placement &labelled = placements.at(0);
        QCOMPARE(placementTextShown(labelled, ComponentText::Value), QStringLiteral("XS1.1"));
        QVERIFY(placementTextOnSilkscreen(labelled, ComponentText::Value).isEmpty());
        Placement onSilk = labelled;
        onSilk.showValue = true;
        QCOMPARE(placementTextOnSilkscreen(onSilk, ComponentText::Value), QStringLiteral("XS1.1"));
    }

    void withoutTheWirePadNothingIsPlaced()
    {
        const Amplifier amp = amplifier();
        const PlacedContacts placed = withContactsPlaced(amp.design, Library());
        QVERIFY(placed.placed.isEmpty());
        QCOMPARE(placed.design, amp.design);
    }
};

QTEST_GUILESS_MAIN(TestContacts)
#include "test_contacts.moc"
