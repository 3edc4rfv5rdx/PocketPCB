#include "partedit.h"

#include <QtTest>

using namespace ppcb;

namespace {

Symbol symbolWithPins(const QStringList &numbers)
{
    Symbol symbol;
    for (const QString &number : numbers) {
        Pin pin;
        pin.number = number;
        symbol.pins.append(pin);
    }
    return symbol;
}

Footprint footprintWithPads(const QStringList &numbers)
{
    Footprint footprint;
    for (const QString &number : numbers) {
        Pad pad;
        pad.number = number;
        footprint.pads.append(pad);
    }
    return footprint;
}

Part diode()
{
    Part part;
    part.symbol = QStringLiteral("diode");
    part.footprints = {QStringLiteral("small"), QStringLiteral("large")};
    part.prefix = QStringLiteral("D");
    part.prefixGost = QStringLiteral("VD");
    return part;
}

} // namespace

class TestPartEdit : public QObject
{
    Q_OBJECT

private slots:
    void pinsWithoutPadAreListedInPinOrder()
    {
        const Symbol symbol = symbolWithPins({QStringLiteral("3"), QStringLiteral("1"),
                                              QStringLiteral("2")});
        QCOMPARE(pinsWithoutPad(symbol, footprintWithPads({QStringLiteral("1")})),
                 QStringList({QStringLiteral("3"), QStringLiteral("2")}));
        // A pad without a pin, as the screw of a tab, is no mismatch.
        QVERIFY(pinsWithoutPad(symbol, footprintWithPads({QStringLiteral("1"), QStringLiteral("2"),
                                                          QStringLiteral("3"), QString()}))
                    .isEmpty());
    }

    void matchingPartHasNoMismatches()
    {
        Library library;
        library.symbols.insert(QStringLiteral("diode"),
                               symbolWithPins({QStringLiteral("A"), QStringLiteral("K")}));
        library.footprints.insert(QStringLiteral("small"),
                                  footprintWithPads({QStringLiteral("A"), QStringLiteral("K")}));
        library.footprints.insert(QStringLiteral("large"),
                                  footprintWithPads({QStringLiteral("K"), QStringLiteral("A")}));
        QVERIFY(partMismatches(diode(), library).isEmpty());
    }

    void mismatchesNameEachFootprintAndWhatIsMissing()
    {
        Library library;
        library.symbols.insert(QStringLiteral("diode"),
                               symbolWithPins({QStringLiteral("A"), QStringLiteral("K")}));
        library.footprints.insert(QStringLiteral("small"),
                                  footprintWithPads({QStringLiteral("1"), QStringLiteral("2")}));
        const QStringList problems = partMismatches(diode(), library);
        QCOMPARE(problems.size(), 2);
        QVERIFY(problems.at(0).contains(QStringLiteral("small")));
        QVERIFY(problems.at(0).contains(QStringLiteral("A, K")));
        QVERIFY(problems.at(1).contains(QStringLiteral("large")));
    }

    void missingSymbolIsToldOnceAndPadsAreNotChecked()
    {
        Library library;
        library.footprints.insert(QStringLiteral("small"), footprintWithPads({}));
        library.footprints.insert(QStringLiteral("large"), footprintWithPads({}));
        const QStringList problems = partMismatches(diode(), library);
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().contains(QStringLiteral("diode")));
    }

    void partNeedsSymbolAndBothPrefixes()
    {
        QVERIFY(partProblem(diode()).isEmpty());
        Part part = diode();
        part.symbol.clear();
        QVERIFY(!partProblem(part).isEmpty());
        part = diode();
        part.prefixGost.clear();
        QVERIFY(!partProblem(part).isEmpty());
        // No footprints is a part never on the board.
        part = diode();
        part.footprints.clear();
        QVERIFY(partProblem(part).isEmpty());
    }

    void footprintMovesWithinTheList()
    {
        const Part moved = withFootprintMoved(diode(), 1, 0);
        QCOMPARE(moved.footprints, QStringList({QStringLiteral("large"), QStringLiteral("small")}));
        QCOMPARE(withFootprintMoved(diode(), 1, 2), diode());
        QCOMPARE(withFootprintMoved(diode(), -1, 0), diode());
    }

    void groupKeysLeaveOutWireLinks()
    {
        const QStringList keys = partGroupKeys();
        QVERIFY(keys.contains(QStringLiteral("resistors")));
        QVERIFY(!keys.contains(QLatin1String(LINK_GROUP)));
        QCOMPARE(keys.first(), QStringLiteral("resistors"));
    }
};

QTEST_GUILESS_MAIN(TestPartEdit)
#include "test_partedit.moc"
