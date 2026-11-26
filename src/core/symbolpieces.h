#pragma once

#include "symbol.h"

#include <QList>

namespace ppcb {

// Ready-made symbols drawn to the sizes of the ESKD standards, to start a
// new symbol from or to put into one being drawn. Their bodies keep the
// sizes of the standard; their pins end on PIN_GRID.
//   Resistor: a rectangle 10 x 4 mm (GOST 2.728).
//   Capacitor: plates 8 mm long, 1.5 mm apart; an electrolytic one's
//   plates frames 1.5 mm thick (GOST 2.728).
//   Diode: a triangle 4 x 4 mm crossed by the line, its bar as long
//   (GOST 2.730).
//   Transistor: a circle 12 mm across, its base 5 mm (GOST 2.730).
//   Inductor: half circles of 2.54 mm radius (GOST 2.723).
//   Chip: a box with pins 5.08 mm apart (GOST 2.743, its 5 mm on the pin
//   grid), see chipSymbol().
enum class SymbolPiece {
    Resistor,
    Capacitor,
    ElectrolyticCapacitor,
    Diode,
    NpnTransistor,
    PnpTransistor,
    Inductor,
};

// Every piece, in the order the menus offer them.
QList<SymbolPiece> symbolPieces();
// The piece as a whole symbol: drawings, pins, the places of the
// designator and the value, its origin in its middle.
Symbol pieceSymbol(SymbolPiece piece);

// A chip after GOST 2.743: a box 'width' wide, 'left' pins down its left
// side and 'right' down its right one, CHIP_PIN_PITCH apart, numbered from
// 1 down the left side and on down the right one, each CHIP_PIN_LENGTH long;
// 'function' (as "&" or "DD") written at the top of the box when not
// empty. The width is at least CHIP_MIN_WIDTH, rounded up to whole steps
// of the pin grid so that the pins of both sides end on it; its origin is
// the end of pin 1.
constexpr Coord CHIP_PIN_PITCH = 2 * PIN_GRID;
constexpr Coord CHIP_PIN_LENGTH = 2 * PIN_GRID;
constexpr Coord CHIP_MIN_WIDTH = 10000000;
Symbol chipSymbol(int left, int right, Coord width, const QString &function);

} // namespace ppcb
