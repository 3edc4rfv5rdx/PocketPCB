#include "paint.h"
#include "symboledit.h"

#include <QtTest>

using namespace ppcb;

namespace {

Point grid(Coord x, Coord y)
{
    return Point{x * PIN_GRID, y * PIN_GRID};
}

Pin pin(const QString &number, const Point &at, PinSide side)
{
    Pin result;
    result.number = number;
    result.at = at;
    result.side = side;
    return result;
}

} // namespace

class TestSymbolEdit : public QObject
{
    Q_OBJECT

private slots:
    void theNextNumberFillsAGap()
    {
        Symbol symbol;
        QCOMPARE(nextPinNumber(symbol), QStringLiteral("1"));
        symbol.pins = {pin(QStringLiteral("1"), grid(0, 0), PinSide::Left),
                       pin(QStringLiteral("3"), grid(0, 1), PinSide::Left),
                       pin(QStringLiteral("VCC"), grid(0, 2), PinSide::Left)};
        QCOMPARE(nextPinNumber(symbol), QStringLiteral("2"));
    }

    // A pin is found along its line, not only at its point.
    void aPinIsFoundAlongItsLine()
    {
        Symbol symbol;
        symbol.pins = {pin(QStringLiteral("1"), grid(-2, 0), PinSide::Left)};
        // The line goes from -5.08 to -2.54 across.
        QCOMPARE(pinAtPoint(symbol, QPointF(-3.5, 0.1), 0.2), 0);
        QCOMPARE(pinAtPoint(symbol, QPointF(-3.5, 0.5), 0.2), -1);
        QCOMPARE(pinAtPoint(symbol, QPointF(-1.0, 0), 0.2), -1);
        QCOMPARE(pinsInside(symbol, QRectF(-6, -1, 4, 2)), QSet<int>({0}));
        QCOMPARE(pinsInside(symbol, QRectF(-4, -1, 4, 2)), QSet<int>());
    }

    // A pin of no length still has a place in the bounds.
    void boundsHoldAPinOfNoLength()
    {
        Symbol symbol;
        Pin dot = pin(QStringLiteral("1"), grid(1, 1), PinSide::Left);
        dot.length = 0;
        symbol.pins = {dot, pin(QStringLiteral("2"), grid(-2, 0), PinSide::Left)};
        const QRectF bounds = pinsBounds(symbol, {0, 1});
        QCOMPARE(bounds.topLeft(), QPointF(-5.08, 0));
        QCOMPARE(bounds.bottomRight(), QPointF(2.54, 2.54));
        QVERIFY(pinsBounds(symbol, {}).isNull());
        QCOMPARE(pinsBounds(symbol, {0}).center(), QPointF(2.54, 2.54));
    }

    // Four quarter turns bring a pin back; each turns its side with it.
    void aPinTurnsWithItsSide()
    {
        const Pin start = pin(QStringLiteral("1"), grid(-2, 0), PinSide::Left);
        Pin turned = pinTurned(start, Point{});
        QCOMPARE(turned.at, grid(0, -2));
        QCOMPARE(turned.side, PinSide::Up);
        // Its body end turns where the body is.
        QCOMPARE(pinBodyEnd(turned), grid(0, -1));
        for (int i = 0; i < 3; ++i)
            turned = pinTurned(turned, Point{});
        QCOMPARE(turned, start);
    }

    // The body end goes along the line of the pin only, never past its point.
    void theBodyEndSetsTheLength()
    {
        const Pin left = pin(QStringLiteral("1"), grid(-2, 0), PinSide::Left);
        const Pin longer = withBodyEndAt(left, Point{-635000, 900000});
        QCOMPARE(longer.length, 5080000 - 635000);
        QCOMPARE(longer.at, left.at);
        QCOMPARE(withBodyEndAt(left, grid(-3, 0)).length, 0);
        const Pin down = pin(QStringLiteral("2"), grid(0, 2), PinSide::Down);
        QCOMPARE(withBodyEndAt(down, Point{0, 3175000}).length, 5080000 - 3175000);
        QCOMPARE(pinBodyEnd(withBodyEndAt(down, Point{0, 3175000})), (Point{0, 3175000}));
    }

    // A piece put in twice: its pins numbered on the second time.
    void aPieceIsPutIn()
    {
        Symbol piece;
        piece.pins = {pin(QStringLiteral("A"), grid(-1, 0), PinSide::Left),
                      pin(QStringLiteral("K"), grid(1, 0), PinSide::Right)};
        Graphic line;
        line.b = grid(1, 0);
        piece.graphics = {line};
        piece.refAt = grid(0, -1);
        piece.showPinNumbers = false;
        const Symbol once = withPiece(Symbol(), piece, grid(2, 0));
        QCOMPARE(once.pins.at(0).at, grid(1, 0));
        QCOMPARE(once.refAt, grid(2, -1));
        QVERIFY(!once.showPinNumbers);
        const Symbol twice = withPiece(once, piece, grid(2, 4));
        QCOMPARE(twice.pins.size(), 4);
        QCOMPARE(twice.pins.at(2).number, QStringLiteral("1"));
        QCOMPARE(twice.pins.at(3).number, QStringLiteral("2"));
        QCOMPARE(twice.graphics.at(1).a, grid(2, 4));
        // The places of the texts stay where they were.
        QCOMPARE(twice.refAt, grid(2, -1));
        QVERIFY(symbolProblem(twice).isEmpty());
    }

    void theOriginMovesEverything()
    {
        Symbol symbol;
        symbol.pins = {pin(QStringLiteral("1"), grid(2, 1), PinSide::Left)};
        Graphic line;
        line.a = grid(0, 0);
        line.b = grid(1, 0);
        symbol.graphics = {line};
        symbol.refAt = grid(0, -1);
        const Symbol moved = withOriginAt(symbol, grid(1, 1));
        QCOMPARE(moved.pins.first().at, grid(1, 0));
        QCOMPARE(moved.graphics.first().a, grid(-1, -1));
        QCOMPARE(moved.refAt, grid(-1, -2));
    }

    void problemsAreThoseOfTheFile()
    {
        Symbol symbol;
        symbol.pins = {pin(QStringLiteral("1"), grid(0, 0), PinSide::Left)};
        QVERIFY(symbolProblem(symbol).isEmpty());
        symbol.pins.append(pin(QStringLiteral("1"), grid(0, 1), PinSide::Left));
        QVERIFY(!symbolProblem(symbol).isEmpty());
        symbol.pins.last().number = QStringLiteral("2");
        symbol.pins.last().at.x += 1;
        QVERIFY(!symbolProblem(symbol).isEmpty());
        symbol.pins.removeLast();
        // A power symbol: one pin, at its origin.
        symbol.power = QStringLiteral("GND");
        QVERIFY(symbolProblem(symbol).isEmpty());
        symbol.pins.first().at = grid(0, 1);
        QVERIFY(!symbolProblem(symbol).isEmpty());
        // The pins themselves are fine.
        QVERIFY(pinsProblem(symbol).isEmpty());
    }

    void partsUsingASymbolAreFound()
    {
        Library library;
        Part a;
        a.symbol = QStringLiteral("resistor");
        Part b;
        b.symbol = QStringLiteral("diode");
        library.parts.insert(QStringLiteral("r"), a);
        library.parts.insert(QStringLiteral("d"), b);
        QCOMPARE(partsUsingSymbol(library, QStringLiteral("resistor")), QStringList({QStringLiteral("r")}));
    }
};

QTEST_GUILESS_MAIN(TestSymbolEdit)
#include "test_symboledit.moc"
