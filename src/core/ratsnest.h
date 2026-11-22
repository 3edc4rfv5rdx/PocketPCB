#pragma once

#include "board.h"
#include "library.h"

#include <QList>

namespace ppcb {

// A line of the ratsnest: two pads of 'net' still to be joined.
struct RatsnestLine
{
    Point from;
    Point to;
    QString net;
};
bool operator==(const RatsnestLine &a, const RatsnestLine &b);

// The lines that show which pads of the board are still to be joined: for
// every net, the shortest tree over the centres of its pads, each pad tied
// to the nearest one already in the tree. A net with one pad has none.
// Tracks are not counted yet.
QList<RatsnestLine> ratsnest(const Board &board, const Library &library);

// A pad of the board on a net: its centre and its size, in nanometres.
struct NetPad
{
    Point at;
    Coord sizeX = 0;
    Coord sizeY = 0;
    QString net;
};
// The pads of the placements of 'board' that are on a net, then the pads
// of its mounting holes that are.
QList<NetPad> netPads(const Board &board, const Library &library);

// Where the pad at 'padAt' of a footprint stands on the board when the
// footprint is placed by 'placement': flipped for the bottom side, turned,
// then moved, as the board draws it.
Point placedPadPoint(const Placement &placement, const Point &padAt);

} // namespace ppcb
