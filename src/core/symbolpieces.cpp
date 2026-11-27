#include "symbolpieces.h"

#include "graphicedit.h"
#include "symboledit.h"
#include "units.h"

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

// A point given in millimetres.
Point mm(double x, double y)
{
    return Point{mmToNm(x), mmToNm(y)};
}

Pin pinAt(const QString &number, const Point &at, PinSide side, Coord length)
{
    Pin pin;
    pin.number = number;
    pin.name = number;
    pin.at = at;
    pin.side = side;
    pin.length = length;
    return pin;
}

// A symbol with two pins across, '1' on the left and '2' on the right, at
// 'reach' from the middle, each 'length' long.
Symbol twoPins(const QString &first, const QString &second, Coord reach, Coord length)
{
    Symbol symbol;
    symbol.pins = {pinAt(first, Point{-reach, 0}, PinSide::Left, length),
                   pinAt(second, Point{reach, 0}, PinSide::Right, length)};
    symbol.showPinNumbers = false;
    symbol.showPinNames = false;
    return symbol;
}

// GOST 2.728: a rectangle 10 x 4 mm.
Symbol resistor()
{
    Symbol symbol = twoPins(QStringLiteral("1"), QStringLiteral("2"), 3 * PIN_GRID,
                            3 * PIN_GRID - mmToNm(5));
    symbol.graphics = {rectBetween(mm(-5, -2), mm(5, 2))};
    symbol.refAt = mm(0, -3.81);
    symbol.valueAt = mm(0, 3.81);
    return symbol;
}

// GOST 2.728: plates 8 mm long, 1.5 mm apart.
Symbol capacitor()
{
    Symbol symbol = twoPins(QStringLiteral("1"), QStringLiteral("2"), 2 * PIN_GRID,
                            2 * PIN_GRID - mmToNm(0.75));
    symbol.graphics = {lineBetween(mm(-0.75, -4), mm(-0.75, 4)),
                       lineBetween(mm(0.75, -4), mm(0.75, 4))};
    symbol.refAt = mm(0, -6.35);
    symbol.valueAt = mm(0, 6.35);
    return symbol;
}

// GOST 2.728: plates frames 8 mm long and 1.5 mm thick, 1.5 mm apart, a
// plus by plate 1.
Symbol electrolyticCapacitor()
{
    Symbol symbol = twoPins(QStringLiteral("1"), QStringLiteral("2"), 2 * PIN_GRID,
                            2 * PIN_GRID - mmToNm(2.25));
    symbol.graphics = {rectBetween(mm(-2.25, -4), mm(-0.75, 4)),
                       rectBetween(mm(0.75, -4), mm(2.25, 4)),
                       lineBetween(mm(-4.25, -3), mm(-2.75, -3)),
                       lineBetween(mm(-3.5, -3.75), mm(-3.5, -2.25))};
    symbol.refAt = mm(0, -6.35);
    symbol.valueAt = mm(0, 6.35);
    return symbol;
}

// The arrow on the line from 'from' to 'to': its point 'at' of the way
// along, pointing towards 'to' or, with 'back', towards 'from'.
Graphic arrowOn(const QPointF &from, const QPointF &to, double at, bool back)
{
    const double length = 1.5;
    const double halfWidth = 0.5;
    const QLineF line(from, to);
    const QPointF unit = (to - from) / line.length();
    const QPointF normal(-unit.y(), unit.x());
    const QPointF tip = from + (to - from) * at;
    const QPointF base = back ? tip + unit * length : tip - unit * length;
    Graphic arrow = polygonOf({mm(tip.x(), tip.y()),
                               mm(base.x() + normal.x() * halfWidth, base.y() + normal.y() * halfWidth),
                               mm(base.x() - normal.x() * halfWidth, base.y() - normal.y() * halfWidth)});
    arrow.filled = true;
    return arrow;
}

// GOST 2.730: a triangle 4 x 4 mm, the bar as long, the line of the pins
// through the triangle.
Symbol diode()
{
    Symbol symbol = twoPins(QStringLiteral("A"), QStringLiteral("K"), 2 * PIN_GRID,
                            2 * PIN_GRID - mmToNm(2));
    symbol.graphics = {polygonOf({mm(-2, -2), mm(-2, 2), mm(2, 0)}),
                       lineBetween(mm(2, -2), mm(2, 2)), lineBetween(mm(-2, 0), mm(2, 0))};
    symbol.refAt = Point{0, -2 * PIN_GRID};
    symbol.valueAt = Point{0, 2 * PIN_GRID};
    return symbol;
}

// GOST 2.730: the diode, its bar with a stroke 1 mm long at the top,
// towards the anode.
Symbol zenerDiode()
{
    Symbol symbol = diode();
    symbol.graphics.append(lineBetween(mm(2, -2), mm(1, -2)));
    return symbol;
}

// GOST 2.730: the diode, its bar's ends bent into an S, 0.75 mm each way.
Symbol schottkyDiode()
{
    Symbol symbol = diode();
    symbol.graphics.append(lineBetween(mm(2, -2), mm(2.75, -2)));
    symbol.graphics.append(lineBetween(mm(2.75, -2), mm(2.75, -1.25)));
    symbol.graphics.append(lineBetween(mm(2, 2), mm(1.25, 2)));
    symbol.graphics.append(lineBetween(mm(1.25, 2), mm(1.25, 1.25)));
    return symbol;
}

// GOST 2.730: two arrows 3 mm long at 45 degrees over the diode, out of
// it for an LED, into it for a photodiode.
Symbol diodeWithArrows(bool out)
{
    Symbol symbol = diode();
    for (const double x : {-0.5, 1.0}) {
        const QPointF near(x, -2.5);
        const QPointF far(x + 2.1, -4.6);
        const QPointF from = out ? near : far;
        const QPointF to = out ? far : near;
        symbol.graphics.append(lineBetween(mm(from.x(), from.y()), mm(to.x(), to.y())));
        symbol.graphics.append(arrowOn(from, to, 1.0, false));
    }
    symbol.refAt = Point{0, -2 * PIN_GRID - PIN_GRID};
    return symbol;
}

// GOST 2.728: the resistor with the wiper, pin 3 from below, its arrow
// on the body.
Symbol potentiometer()
{
    Symbol symbol = resistor();
    symbol.pins.append(pinAt(QStringLiteral("3"), Point{0, 2 * PIN_GRID}, PinSide::Down,
                             2 * PIN_GRID - mmToNm(2)));
    symbol.graphics.append(arrowOn(QPointF(0, 4), QPointF(0, 2), 1.0, false));
    symbol.valueAt = mm(3.81, 3.81);
    return symbol;
}

// GOST 2.730: a circle 12 mm across, the base 5 mm; the arrow of the
// emitter out of it for NPN, into it for PNP.
Symbol transistor(bool npn)
{
    const double bar = -1.27;
    Symbol symbol;
    symbol.pins = {pinAt(QStringLiteral("B"), Point{-3 * PIN_GRID, 0}, PinSide::Left,
                         3 * PIN_GRID - mmToNm(-bar)),
                   pinAt(QStringLiteral("C"), Point{PIN_GRID, -3 * PIN_GRID}, PinSide::Up,
                         3 * PIN_GRID / 2),
                   pinAt(QStringLiteral("E"), Point{PIN_GRID, 3 * PIN_GRID}, PinSide::Down,
                         3 * PIN_GRID / 2)};
    symbol.showPinNumbers = false;
    symbol.showPinNames = false;
    const QPointF emitterFrom(bar, 1.27);
    const QPointF emitterTo(2.54, 3.81);
    symbol.graphics = {
        circleThrough(Point{}, mm(6, 0)),
        lineBetween(mm(bar, -2.5), mm(bar, 2.5)),
        lineBetween(mm(bar, -1.27), mm(2.54, -3.81)),
        lineBetween(mm(emitterFrom.x(), emitterFrom.y()), mm(emitterTo.x(), emitterTo.y())),
        npn ? arrowOn(emitterFrom, emitterTo, 0.85, false)
            : arrowOn(emitterFrom, emitterTo, 0.4, true),
    };
    symbol.refAt = mm(10.16, -1.27);
    symbol.valueAt = mm(10.16, 1.27);
    return symbol;
}

} // namespace

QList<SymbolPiece> symbolPieces()
{
    return {SymbolPiece::Resistor,      SymbolPiece::Potentiometer, SymbolPiece::Capacitor,
            SymbolPiece::ElectrolyticCapacitor, SymbolPiece::Diode,  SymbolPiece::ZenerDiode,
            SymbolPiece::SchottkyDiode, SymbolPiece::Led,           SymbolPiece::Photodiode,
            SymbolPiece::NpnTransistor, SymbolPiece::PnpTransistor, SymbolPiece::Inductor};
}

Symbol pieceSymbol(SymbolPiece piece)
{
    switch (piece) {
    case SymbolPiece::Resistor:
        return resistor();
    case SymbolPiece::Capacitor:
        return capacitor();
    case SymbolPiece::ElectrolyticCapacitor:
        return electrolyticCapacitor();
    case SymbolPiece::Diode:
        return diode();
    case SymbolPiece::ZenerDiode:
        return zenerDiode();
    case SymbolPiece::SchottkyDiode:
        return schottkyDiode();
    case SymbolPiece::Led:
        return diodeWithArrows(true);
    case SymbolPiece::Photodiode:
        return diodeWithArrows(false);
    case SymbolPiece::NpnTransistor:
        return transistor(true);
    case SymbolPiece::PnpTransistor:
        return transistor(false);
    case SymbolPiece::Inductor:
        return inductorSymbol(4);
    case SymbolPiece::Potentiometer:
        return potentiometer();
    }
    return Symbol();
}

QList<int> inductorTurnChoices()
{
    return {2, 3, 4};
}

Symbol inductorSymbol(int turns)
{
    turns = std::max(1, turns);
    Symbol symbol =
        twoPins(QStringLiteral("1"), QStringLiteral("2"), (turns + 1) * PIN_GRID, PIN_GRID);
    for (int i = 0; i < turns; ++i) {
        Graphic arc;
        arc.kind = GraphicKind::Arc;
        arc.a = Point{(2 * i - (turns - 1)) * PIN_GRID, 0};
        arc.radius = PIN_GRID;
        // Over the line: from the left through the top to the right.
        arc.startAngle = 180;
        arc.spanAngle = 180;
        symbol.graphics.append(arc);
    }
    symbol.refAt = Point{0, -2 * PIN_GRID};
    symbol.valueAt = Point{0, PIN_GRID};
    return symbol;
}

Symbol chipSymbol(int left, int right, Coord width, const QString &function)
{
    left = std::max(0, left);
    right = std::max(0, right);
    const int rows = std::max({left, right, 1});
    const Coord wide = std::max(width, CHIP_MIN_WIDTH);
    const Coord boxWidth = (wide + PIN_GRID - 1) / PIN_GRID * PIN_GRID;
    // The box from the end of the pins on the left, a step above the first
    // row and a step below the last.
    const Coord boxLeft = CHIP_PIN_LENGTH;
    const Coord boxRight = boxLeft + boxWidth;
    const Coord top = -CHIP_PIN_PITCH;
    const Coord bottom = rows * CHIP_PIN_PITCH;
    Symbol symbol;
    symbol.graphics.append(rectBetween(Point{boxLeft, top}, Point{boxRight, bottom}));
    const Coord middle = (boxLeft + boxRight) / 2;
    if (!function.trimmed().isEmpty())
        symbol.graphics.append(textAt(Point{middle, top / 2}, function.trimmed()));
    int number = 1;
    for (int i = 0; i < left; ++i) {
        symbol.pins.append(Pin{QString::number(number++), QString(), Point{0, i * CHIP_PIN_PITCH},
                               PinSide::Left, CHIP_PIN_LENGTH});
    }
    for (int i = 0; i < right; ++i) {
        symbol.pins.append(Pin{QString::number(number++), QString(),
                               Point{boxRight + CHIP_PIN_LENGTH, i * CHIP_PIN_PITCH},
                               PinSide::Right, CHIP_PIN_LENGTH});
    }
    symbol.refAt = Point{middle, top - PIN_GRID};
    symbol.valueAt = Point{middle, bottom + PIN_GRID};
    return symbol;
}

} // namespace ppcb
