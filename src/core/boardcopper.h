#pragma once

#include "board.h"
#include "library.h"

#include <QList>
#include <QMap>

namespace ppcb {

struct Rules;

// The copper and the holes of the board as they are made: shared by the
// print for a board made at home and by the files for a factory.

// A pad of copper on one side, its sides along the board's axes: the pad
// of a part (turned with it), of a wire link, of a via or of a mounting
// hole with a ring.
struct CopperPad
{
    enum class Kind { Part, Via, Link, Hole };
    Kind kind = Kind::Part;
    Point at;
    PadShape shape = PadShape::Circle;
    // Width along X and height along Y, on the board.
    Coord sizeX = 0;
    Coord sizeY = 0;
    // The hole's diameter; 0 for a pad without one.
    Coord drill = 0;
};

// The pads with copper on 'side', in the order: parts (those whose
// footprint 'library' has), vias, wire links (where the board has copper),
// mounting holes with a ring on that side.
QList<CopperPad> copperPads(const Board &board, const Library &library, BoardSide side);

// A hole drilled through the board.
struct DrillHole
{
    Point at;
    Coord diameter = 0;
    // Its wall is plated: the holes of parts' pads, of vias and of wire
    // links, and mounting holes marked so.
    bool plated = false;
};

// Every hole of the board, once each, with the diameter it is drilled with
// (drilledDiameter()).
QList<DrillHole> drillHoles(const Board &board, const Library &library, const Rules &rules);

// How many of 'holes' each diameter has, the diameters ascending: the drill
// table.
QMap<Coord, int> drillCounts(const QList<DrillHole> &holes);

// The diameter 'diameter' is drilled with: when only the drills of the
// rules may be used, the smallest of them not smaller; else, or when none
// is large enough, 'diameter' itself.
Coord drilledDiameter(Coord diameter, const Rules &rules);

} // namespace ppcb
