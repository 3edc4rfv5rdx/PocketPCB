#pragma once

#include "graphic.h"

#include <QList>

namespace ppcb {

// Pins of every symbol sit on this grid (2.54 mm), so wires can reach them.
constexpr Coord PIN_GRID = 2540000;
// Pin length used when a file does not give one.
constexpr Coord DEFAULT_PIN_LENGTH = PIN_GRID;

// The side of the symbol a wire comes from.
enum class PinSide { Left, Right, Up, Down };

struct Pin
{
    // Connects the pin to the pad with the same number; unique in a symbol.
    QString number;
    // May be empty.
    QString name;
    // The point a wire connects to.
    Point at;
    PinSide side = PinSide::Left;
    // Length of the pin's line, drawn from 'at' towards the symbol body.
    Coord length = DEFAULT_PIN_LENGTH;
};

bool operator==(const Pin &a, const Pin &b);
inline bool operator!=(const Pin &a, const Pin &b) { return !(a == b); }

// A schematic symbol. Coordinates are relative to the symbol's origin.
struct Symbol
{
    QList<Graphic> graphics;
    QList<Pin> pins;
    // Where the reference designator and the value are first placed.
    Point refAt;
    Point valueAt;
    // Off for parts like a resistor, where pin numbers and names say nothing.
    bool showPinNumbers = true;
    bool showPinNames = true;

    // A power symbol (GND, +5V) when not empty: the net name it is placed
    // with. It has one pin, at its origin, and no part: it is placed by
    // itself and joins every power symbol of the same name into one net.
    // The net name is drawn at 'valueAt'; 'refAt' is not used.
    QString power;
    // The net name is not drawn while it is 'power': the usual ground.
    bool hidePowerName = false;
    // Name of a power symbol in the library panel, by interface language
    // code, as Part::name; may be empty.
    QMap<QString, QString> name;

    bool isPower() const { return !power.isEmpty(); }
    // As Part::displayName().
    QString displayName(const QStringList &languages, const QString &fallback) const
    {
        return localizedName(name, languages, fallback);
    }
};

bool operator==(const Symbol &a, const Symbol &b);
inline bool operator!=(const Symbol &a, const Symbol &b) { return !(a == b); }

QJsonObject symbolToJson(const Symbol &symbol);
// 'where' is the path of the object for error messages. After a failure
// (see reader.ok()) the result is not meaningful.
Symbol symbolFromJson(const QJsonObject &object, const QString &where, JsonReader &reader);

} // namespace ppcb
