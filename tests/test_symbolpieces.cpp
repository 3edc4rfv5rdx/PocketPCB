#include "paint.h"
#include "symboledit.h"
#include "symbolpieces.h"

#include <QtTest>

using namespace ppcb;

class TestSymbolPieces : public QObject
{
    Q_OBJECT

private slots:
    // Every piece could be a file of its own.
    void everyPieceIsASymbol()
    {
        for (const SymbolPiece piece : symbolPieces()) {
            const Symbol symbol = pieceSymbol(piece);
            QVERIFY2(symbolProblem(symbol).isEmpty(), qPrintable(symbolProblem(symbol)));
            QVERIFY(!symbol.graphics.isEmpty());
            QVERIFY(!symbol.pins.isEmpty());
        }
    }

    // The sizes of the standard, the pins meeting the body.
    void theBodiesHaveTheSizesOfTheStandard()
    {
        const Symbol resistor = pieceSymbol(SymbolPiece::Resistor);
        QCOMPARE(resistor.graphics.first().b.x - resistor.graphics.first().a.x, 10000000);
        QCOMPARE(resistor.graphics.first().b.y - resistor.graphics.first().a.y, 4000000);
        QCOMPARE(pinBodyEnd(resistor.pins.first()), (Point{-5000000, 0}));
        const Symbol capacitor = pieceSymbol(SymbolPiece::Capacitor);
        QCOMPARE(capacitor.graphics.at(1).a.x - capacitor.graphics.at(0).a.x, 1500000);
        QCOMPARE(capacitor.graphics.at(0).b.y - capacitor.graphics.at(0).a.y, 8000000);
        QCOMPARE(pinBodyEnd(capacitor.pins.first()), (Point{-750000, 0}));
        // An electrolytic one's plates are frames 1.5 mm thick, 1.5 mm apart.
        const Symbol electrolytic = pieceSymbol(SymbolPiece::ElectrolyticCapacitor);
        QCOMPARE(electrolytic.graphics.at(0).b.x - electrolytic.graphics.at(0).a.x, 1500000);
        QCOMPARE(electrolytic.graphics.at(1).a.x - electrolytic.graphics.at(0).b.x, 1500000);
        QCOMPARE(pinBodyEnd(electrolytic.pins.first()), (Point{-2250000, 0}));
        const Symbol diode = pieceSymbol(SymbolPiece::Diode);
        QCOMPARE(diode.graphics.at(1).b.y - diode.graphics.at(1).a.y, 4000000);
        QCOMPARE(pinBodyEnd(diode.pins.first()), (Point{-2000000, 0}));
        // The kin of the diode draw it and add their marks.
        for (const SymbolPiece kin : {SymbolPiece::ZenerDiode, SymbolPiece::SchottkyDiode,
                                      SymbolPiece::Led, SymbolPiece::Photodiode}) {
            const Symbol symbol = pieceSymbol(kin);
            QCOMPARE(symbol.pins, diode.pins);
            QVERIFY(symbol.graphics.size() > diode.graphics.size());
            QCOMPARE(symbol.graphics.mid(0, diode.graphics.size()), diode.graphics);
        }
        // An LED's arrows point away from it, a photodiode's at it.
        const Graphic ledArrow = pieceSymbol(SymbolPiece::Led).graphics.at(4);
        const Graphic photoArrow = pieceSymbol(SymbolPiece::Photodiode).graphics.at(4);
        QVERIFY(ledArrow.points.first().y < photoArrow.points.first().y);
        // The potentiometer's wiper comes from below onto the body.
        const Symbol potentiometer = pieceSymbol(SymbolPiece::Potentiometer);
        QCOMPARE(potentiometer.pins.size(), 3);
        QCOMPARE(pinBodyEnd(potentiometer.pins.at(2)), (Point{0, 2000000}));
        const Symbol npn = pieceSymbol(SymbolPiece::NpnTransistor);
        QCOMPARE(npn.graphics.first().radius, 6000000);
        QCOMPARE(pinBodyEnd(npn.pins.at(1)), (Point{2540000, -3810000}));
    }

    // An inductor of 2, 3 or 4 half circles of the pin grid's radius, its
    // pins at their ends: the four of the ready-made one.
    void anInductorHasAsManyHalfCirclesAsAsked()
    {
        QCOMPARE(inductorSymbol(4), pieceSymbol(SymbolPiece::Inductor));
        for (const int turns : inductorTurnChoices()) {
            const Symbol symbol = inductorSymbol(turns);
            QVERIFY(symbolProblem(symbol).isEmpty());
            QCOMPARE(symbol.graphics.size(), turns);
            QCOMPARE(symbol.graphics.first().radius, PIN_GRID);
            QCOMPARE(pinBodyEnd(symbol.pins.first()).x, -turns * PIN_GRID);
            QCOMPARE(pinBodyEnd(symbol.pins.last()).x, turns * PIN_GRID);
        }
    }

    void aChipHasItsPinsDownBothSides()
    {
        const Symbol chip = chipSymbol(3, 2, 12000000, QStringLiteral("&"));
        QVERIFY(symbolProblem(chip).isEmpty());
        QCOMPARE(chip.pins.size(), 5);
        QCOMPARE(chip.pins.at(0).at, Point{});
        QCOMPARE(chip.pins.at(2).at, (Point{0, 2 * CHIP_PIN_PITCH}));
        QCOMPARE(chip.pins.at(3).number, QStringLiteral("4"));
        QCOMPARE(chip.pins.at(3).side, PinSide::Right);
        // 12 mm rounded up to the grid: 12.7.
        QCOMPARE(pinBodyEnd(chip.pins.at(3)).x - pinBodyEnd(chip.pins.at(0)).x, 12700000);
        QCOMPARE(chip.graphics.size(), 2);
        // Never narrower than the standard's least.
        const Symbol narrow = chipSymbol(1, 1, 0, QString());
        QVERIFY(pinBodyEnd(narrow.pins.at(1)).x - pinBodyEnd(narrow.pins.at(0)).x
                >= CHIP_MIN_WIDTH);
        QCOMPARE(narrow.graphics.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestSymbolPieces)
#include "test_symbolpieces.moc"
