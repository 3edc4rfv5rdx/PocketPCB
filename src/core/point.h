#pragma once

#include "units.h"

#include <cstdlib>

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

// True for an offset that runs horizontally, vertically or at 45 degrees:
// the directions of a wire and a bus. The zero offset counts as one too.
inline bool isStraightOrDiagonal(const Point &step)
{
    return step.x == 0 || step.y == 0 || std::llabs(step.x) == std::llabs(step.y);
}

} // namespace ppcb
