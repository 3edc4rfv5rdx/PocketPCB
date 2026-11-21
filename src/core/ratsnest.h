#pragma once

#include "board.h"
#include "library.h"

#include <QList>
#include <QPair>

namespace ppcb {

// The lines that show which pads of the board are still to be joined: for
// every net, the shortest tree over the centres of its pads, each pad tied
// to the nearest one already in the tree. A net with one pad has none.
// Tracks are not counted yet.
QList<QPair<Point, Point>> ratsnest(const Board &board, const Library &library);

// Where the pad at 'padAt' of a footprint stands on the board when the
// footprint is placed by 'placement': flipped for the bottom side, turned,
// then moved, as the board draws it.
Point placedPadPoint(const Placement &placement, const Point &padAt);

} // namespace ppcb
