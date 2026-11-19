#pragma once

#include "units.h"

#include <QString>

namespace ppcb {

// Millimetres with exactly three decimals, in the current locale: "2.540".
// For values that change while the user watches (cursor position).
QString formatMmFixed(Coord nm);

// Millimetres with trailing zeros removed: "2.54", "10.16", "5".
// For values shown in lists (grid steps, widths).
QString formatMmShort(Coord nm);

// "mm", the unit after a length, in the current language.
QString mmUnit();

// A rated power in watts, in the current locale: "0.125 W"; empty for 0.
QString formatPower(int powerMw);

} // namespace ppcb
