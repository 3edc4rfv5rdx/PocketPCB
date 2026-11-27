#include "symboledit.h"

#include "graphicedit.h"
#include "jsonio.h"
#include "paint.h"
#include "trackedit.h"

#include <QLineF>

#include <algorithm>

namespace ppcb {

namespace {

// The line of 'pin' in millimetres, from its point to the body.
QRectF pinRect(const Pin &pin)
{
    return QRectF(toMm(pin.at), toMm(pinBodyEnd(pin))).normalized();
}

} // namespace

QList<Coord> pinLengthChoices()
{
    return {PIN_GRID, PIN_GRID * 3 / 2, PIN_GRID * 2, PIN_GRID * 3, PIN_GRID * 4};
}

QString nextPinNumber(const Symbol &symbol)
{
    QSet<int> taken;
    for (const Pin &pin : symbol.pins) {
        bool ok = false;
        const int number = pin.number.toInt(&ok);
        if (ok && number > 0 && pin.number == QString::number(number))
            taken.insert(number);
    }
    int number = 1;
    while (taken.contains(number))
        ++number;
    return QString::number(number);
}

int pinAtPoint(const Symbol &symbol, const QPointF &at, double reach)
{
    for (int i = int(symbol.pins.size()) - 1; i >= 0; --i) {
        const Pin &pin = symbol.pins.at(i);
        const QPointF from = toMm(pin.at);
        const QPointF nearest = nearestOnPiece(from, toMm(pinBodyEnd(pin)), at);
        if (QLineF(nearest, at).length() <= reach)
            return i;
    }
    return -1;
}

QSet<int> pinsInside(const Symbol &symbol, const QRectF &rect)
{
    QSet<int> inside;
    for (int i = 0; i < symbol.pins.size(); ++i) {
        const Pin &pin = symbol.pins.at(i);
        if (rect.contains(toMm(pin.at)) && rect.contains(toMm(pinBodyEnd(pin))))
            inside.insert(i);
    }
    return inside;
}

QRectF pinsBounds(const Symbol &symbol, const QSet<int> &pins)
{
    // By hand: QRectF::united() drops a pin of no length, a point.
    bool any = false;
    QPointF low;
    QPointF high;
    for (int i = 0; i < symbol.pins.size(); ++i) {
        if (!pins.contains(i))
            continue;
        const QRectF line = pinRect(symbol.pins.at(i));
        low = any ? QPointF(std::min(low.x(), line.left()), std::min(low.y(), line.top()))
                  : line.topLeft();
        high = any ? QPointF(std::max(high.x(), line.right()), std::max(high.y(), line.bottom()))
                   : line.bottomRight();
        any = true;
    }
    return any ? QRectF(low, high) : QRectF();
}

PinSide sideTurned(PinSide side)
{
    // The way the wire comes from, turned as turnedQuarter() turns a point:
    // from the left it comes from above, and so on round.
    switch (side) {
    case PinSide::Left:
        return PinSide::Up;
    case PinSide::Up:
        return PinSide::Right;
    case PinSide::Right:
        return PinSide::Down;
    case PinSide::Down:
        return PinSide::Left;
    }
    return side;
}

Pin pinTurned(Pin pin, const Point &center)
{
    pin.at = turnedQuarter(pin.at, center);
    pin.side = sideTurned(pin.side);
    return pin;
}

Pin withBodyEndAt(Pin pin, const Point &to)
{
    // The way into the body: the other way from the side the wire is on.
    Coord length = 0;
    switch (pin.side) {
    case PinSide::Left:
        length = to.x - pin.at.x;
        break;
    case PinSide::Right:
        length = pin.at.x - to.x;
        break;
    case PinSide::Up:
        length = to.y - pin.at.y;
        break;
    case PinSide::Down:
        length = pin.at.y - to.y;
        break;
    }
    pin.length = std::max<Coord>(0, length);
    return pin;
}

Symbol withPinsAdded(const Symbol &symbol, const QList<Pin> &pins)
{
    Symbol result = symbol;
    result.pins.append(pins);
    return result;
}

Symbol withPinsMoved(const Symbol &symbol, const QSet<int> &pins, const Point &delta)
{
    Symbol result = symbol;
    for (int i = 0; i < result.pins.size(); ++i) {
        if (pins.contains(i))
            result.pins[i].at = result.pins.at(i).at + delta;
    }
    return result;
}

Symbol withPinsRotated(const Symbol &symbol, const QSet<int> &pins, const Point &center)
{
    Symbol result = symbol;
    for (int i = 0; i < result.pins.size(); ++i) {
        if (pins.contains(i))
            result.pins[i] = pinTurned(result.pins.at(i), center);
    }
    return result;
}

Symbol withoutPins(const Symbol &symbol, const QSet<int> &pins)
{
    Symbol result = symbol;
    result.pins.clear();
    for (int i = 0; i < symbol.pins.size(); ++i) {
        if (!pins.contains(i))
            result.pins.append(symbol.pins.at(i));
    }
    return result;
}

Symbol withPinsReplaced(const Symbol &symbol, const QList<int> &pins, const QList<Pin> &changed)
{
    Symbol result = symbol;
    for (int k = 0; k < pins.size() && k < changed.size(); ++k) {
        const int i = pins.at(k);
        if (i >= 0 && i < result.pins.size())
            result.pins[i] = changed.at(k);
    }
    return result;
}

Symbol withPiece(const Symbol &symbol, const Symbol &piece, const Point &at)
{
    Symbol result = symbol;
    const bool empty = symbol.graphics.isEmpty() && symbol.pins.isEmpty();
    for (const Graphic &graphic : piece.graphics)
        result.graphics.append(graphicMoved(graphic, at));
    for (Pin pin : piece.pins) {
        pin.at = pin.at + at;
        const bool taken = std::any_of(result.pins.cbegin(), result.pins.cend(),
                                       [&pin](const Pin &other) { return other.number == pin.number; });
        if (taken)
            pin.number = nextPinNumber(result);
        result.pins.append(pin);
    }
    if (empty) {
        result.refAt = piece.refAt + at;
        result.valueAt = piece.valueAt + at;
        result.showPinNumbers = piece.showPinNumbers;
        result.showPinNames = piece.showPinNames;
    }
    return result;
}

Symbol withOriginAt(const Symbol &symbol, const Point &at)
{
    const Point delta = Point{} - at;
    Symbol result = symbol;
    for (Pin &pin : result.pins)
        pin.at = pin.at + delta;
    for (Graphic &graphic : result.graphics)
        graphic = graphicMoved(graphic, delta);
    result.refAt = result.refAt + delta;
    result.valueAt = result.valueAt + delta;
    return result;
}

Point symbolMiddle(const Symbol &symbol)
{
    QRectF bounds = symbolBodyBounds(symbol);
    if (bounds.isNull()) {
        QSet<int> all;
        for (int i = 0; i < symbol.pins.size(); ++i)
            all.insert(i);
        bounds = pinsBounds(symbol, all);
    }
    if (symbol.pins.isEmpty() && symbol.graphics.isEmpty())
        return Point{};
    return Point{mmToNm(bounds.center().x()), mmToNm(bounds.center().y())};
}

QString pinsProblem(const Symbol &symbol)
{
    // The messages of the file reader, so that the two say it alike.
    QSet<QString> numbers;
    for (const Pin &pin : symbol.pins) {
        if (pin.number.isEmpty())
            return JsonReader::tr("the text must not be empty");
        if (numbers.contains(pin.number))
            return JsonReader::tr("the pin number is used twice");
        numbers.insert(pin.number);
        if (pin.at.x % PIN_GRID != 0 || pin.at.y % PIN_GRID != 0)
            return JsonReader::tr("the pin is not on the pin grid");
        if (pin.length < 0)
            return JsonReader::tr("a size that is not negative is expected");
    }
    return QString();
}

QString symbolProblem(const Symbol &symbol)
{
    const QString problem = pinsProblem(symbol);
    if (!problem.isEmpty())
        return problem;
    if (symbol.isPower() && (symbol.pins.size() != 1 || symbol.pins.first().at != Point()))
        return JsonReader::tr("a power symbol has one pin, at its origin");
    return QString();
}

QStringList partsUsingSymbol(const Library &library, const QString &name)
{
    QStringList using_;
    for (auto it = library.parts.constBegin(); it != library.parts.constEnd(); ++it) {
        if (it.value().symbol == name || it.value().units.contains(name))
            using_.append(it.key());
    }
    return using_;
}

} // namespace ppcb
