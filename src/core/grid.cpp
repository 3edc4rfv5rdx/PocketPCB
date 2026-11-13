#include "grid.h"

#include <QtGlobal>

namespace ppcb {

namespace {

// Integer division rounding towards -infinity (C++ '/' rounds towards zero).
Coord floorDiv(Coord a, Coord b)
{
    Coord q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

} // namespace

const std::vector<Coord> &gridSteps()
{
    static const std::vector<Coord> steps = {
        635000, 1270000, DEFAULT_GRID_STEP, 5080000, 10160000,
    };
    return steps;
}

Coord nextGridStep(Coord current)
{
    for (Coord step : gridSteps()) {
        if (step > current)
            return step;
    }
    return gridSteps().back();
}

Coord prevGridStep(Coord current)
{
    const std::vector<Coord> &steps = gridSteps();
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
        if (*it < current)
            return *it;
    }
    return steps.front();
}

Coord snapToGrid(Coord value, Coord step)
{
    Q_ASSERT_X(step > 0, "snapToGrid", "step must be positive");
    if (step <= 0)
        return value;
    // Doubled remainder compared with step: exact for odd steps too.
    Coord q = floorDiv(value, step);
    const Coord rest = value - q * step; // 0 <= rest < step
    if (2 * rest >= step)
        ++q;
    return q * step;
}

} // namespace ppcb
