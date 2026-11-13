#pragma once

#include "graphic.h"

#include <QList>

namespace ppcb {

enum class PadShape { Circle, Rect, Oval };

// Copper layers of a pad, relative to the side the part is mounted on:
// Front is that side, Back the opposite one.
enum class PadLayers { Both, Front, Back };

struct Pad
{
    // Connects the pad to the pin with the same number. Several pads may
    // share a number; an empty number is a pad connected to no pin.
    QString number;
    // Centre of the pad and of its hole.
    Point at;
    PadShape shape = PadShape::Circle;
    // Width and height; equal for a Circle.
    Coord sizeX = 0;
    Coord sizeY = 0;
    // Drill diameter; 0 is a pad without a hole (surface mount).
    Coord hole = 0;
    // A pad without a hole is never on Both.
    PadLayers layers = PadLayers::Both;
};

bool operator==(const Pad &a, const Pad &b);
inline bool operator!=(const Pad &a, const Pad &b) { return !(a == b); }

// A footprint. Coordinates are relative to the footprint's origin, as seen
// from the side the part is mounted on.
struct Footprint
{
    QList<Pad> pads;
    // Body outline for the silkscreen.
    QList<Graphic> graphics;
    // Where the reference designator and the value are first placed.
    Point refAt;
    Point valueAt;
};

bool operator==(const Footprint &a, const Footprint &b);
inline bool operator!=(const Footprint &a, const Footprint &b) { return !(a == b); }

QJsonObject footprintToJson(const Footprint &footprint);
// 'where' is the path of the object for error messages. After a failure
// (see reader.ok()) the result is not meaningful.
Footprint footprintFromJson(const QJsonObject &object, const QString &where, JsonReader &reader);

} // namespace ppcb
