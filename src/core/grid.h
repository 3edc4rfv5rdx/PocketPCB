#pragma once

#include "units.h"

#include <vector>

namespace ppcb {

// Imperial grid: 0.635, 1.27, 2.54, 5.08, 10.16 mm.
constexpr Coord DEFAULT_GRID_STEP = 2540000;

// Available grid steps in nanometres, ascending.
const std::vector<Coord> &gridSteps();

// Next larger / smaller step from gridSteps(). 'current' need not be in the
// list. Stays at the largest / smallest step at the end of the list.
Coord nextGridStep(Coord current);
Coord prevGridStep(Coord current);

// Rounds 'value' to the nearest multiple of 'step' (step > 0).
// A value exactly halfway goes towards +infinity, the same on both sides of 0.
Coord snapToGrid(Coord value, Coord step);

} // namespace ppcb
