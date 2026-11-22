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
// to the nearest one already in the tree, with no line between pads the
// copper joins already (copperGroups()). A net with one pad has none.
QList<RatsnestLine> ratsnest(const Board &board, const Library &library);

// How far the routing is: the connections still to be made (the lines of
// the ratsnest) and all there are, one fewer than the pads of each net.
struct RoutingProgress
{
    int unrouted = 0;
    int total = 0;
};
RoutingProgress routingProgress(const Board &board, const Library &library);

// A pad of the board on a net: its centre and its size, in nanometres.
struct NetPad
{
    Point at;
    Coord sizeX = 0;
    Coord sizeY = 0;
    QString net;
    // The sides of the board it has copper on.
    bool top = true;
    bool bottom = true;
};
// The pads of the placements of 'board' that are on a net, then the pads
// of its mounting holes that are.
QList<NetPad> netPads(const Board &board, const Library &library);

// The group of copper each of 'pads' is in: pads joined by the tracks and
// vias of 'board' have one number, pads not joined different ones. A track
// joins what its ends lie on, on its side: a pad, a via, the end or the
// middle of a track; a via joins both sides.
QList<int> copperGroups(const Board &board, const QList<NetPad> &pads);

// Where the pad at 'padAt' of a footprint stands on the board when the
// footprint is placed by 'placement': flipped for the bottom side, turned,
// then moved, as the board draws it.
Point placedPadPoint(const Placement &placement, const Point &padAt);

} // namespace ppcb
