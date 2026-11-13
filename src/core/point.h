#pragma once

#include "units.h"

namespace ppcb {

// A position or an offset in nanometres. Y grows downwards.
struct Point
{
    Coord x = 0;
    Coord y = 0;
};

inline bool operator==(const Point &a, const Point &b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const Point &a, const Point &b) { return !(a == b); }
inline Point operator+(const Point &a, const Point &b) { return {a.x + b.x, a.y + b.y}; }
inline Point operator-(const Point &a, const Point &b) { return {a.x - b.x, a.y - b.y}; }

} // namespace ppcb
