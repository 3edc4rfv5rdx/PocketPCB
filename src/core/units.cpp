#include "units.h"

#include <QtGlobal>

#include <cmath>

namespace ppcb {

bool tryMmToNm(double mm, Coord &out)
{
    if (!std::isfinite(mm) || std::fabs(mm) > MAX_ABS_MM)
        return false;
    // llround rounds halfway cases away from zero.
    out = std::llround(mm * static_cast<double>(NM_PER_MM));
    return true;
}

Coord mmToNm(double mm)
{
    Coord nm = 0;
    const bool ok = tryMmToNm(mm, nm);
    Q_ASSERT_X(ok, "mmToNm", "value is not finite or out of range");
    Q_UNUSED(ok);
    return nm;
}

double nmToMm(Coord nm)
{
    return static_cast<double>(nm) / static_cast<double>(NM_PER_MM);
}

} // namespace ppcb
