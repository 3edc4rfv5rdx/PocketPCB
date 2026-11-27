#include "contacts.h"

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
    void aContactGoesOnEveryOffBoardNetThatReachesTheBoard()
    {
        const Amplifier amp = amplifier();
        const PlacedContacts placed = withContactsPlaced(amp.design, source());
        // The two nets reaching the resistor, not the jack's to the switch.
        QCOMPARE(placed.placed.size(), 2);
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

        // Numbered on: J1 and J2.
        QStringList designators = {left->designator, down->designator};
        designators.sort();
        QCOMPARE(designators, QStringList({QStringLiteral("J1"), QStringLiteral("J2")}));
    }

    void aNetWithAContactGetsNoMore()
    {
        const Amplifier amp = amplifier();
        const PlacedContacts once = withContactsPlaced(amp.design, source());
        const PlacedContacts twice = withContactsPlaced(once.design, source());
        QVERIFY(twice.placed.isEmpty());
        QCOMPARE(twice.design, once.design);
    }

    void onlyTheChosenOffBoardPartsGetContacts()
    {
        const Amplifier amp = amplifier();
        const PlacedContacts placed = withContactsPlaced(amp.design, source(), {amp.otherJack});
        QCOMPARE(placed.placed.size(), 1);
        QVERIFY(placedContact(placed.design, placed, mm(-20, -5)) != nullptr);
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
