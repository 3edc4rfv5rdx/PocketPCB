#pragma once

#include <cstdint>

namespace ppcb {

// All coordinates and sizes in the model are integer nanometres.
using Coord = std::int64_t;

constexpr double PI = 3.14159265358979323846;

constexpr Coord NM_PER_MM = 1000000;
constexpr Coord NM_PER_MIL = 25400;

// Largest absolute value accepted from millimetres (1 km = 1e12 nm), far
// inside int64. Note: a product of two such coordinates (squared distance)
// does NOT fit int64 — geometry code must compute those in double.
constexpr double MAX_ABS_MM = 1000000.0;

// Converts millimetres to nanometres, rounding to the nearest nanometre.
// Returns false (and leaves 'out' untouched) for NaN, infinity or a value
// beyond MAX_ABS_MM. Use this for any value read from a file or typed by the user.
bool tryMmToNm(double mm, Coord &out);

// Same conversion for values known to be valid (constants, internal math).
// Asserts in debug builds on invalid input; returns 0 for it in release builds.
Coord mmToNm(double mm);

double nmToMm(Coord nm);

} // namespace ppcb
