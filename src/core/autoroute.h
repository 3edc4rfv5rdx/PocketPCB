#pragma once

#include "project.h"

#include <QSet>
#include <QString>

#include <functional>

namespace ppcb {

// Laying the tracks of the board by the program (tz-add.md, Autorouter).
// Fewer vias come before shorter tracks. Each connection still to be made
// (ratsnest()), the shortest first, is laid by the grid search (gridRoute())
// on a grid of half the board's step and no coarser than 0.635 mm, on both sides of a board of two, in
// eight directions: from the pads of one group of copper to those of the
// other, keeping the clearance of the net classes from copper of other
// nets, from holes and from the edge of the board. On the top straight
// pieces go level more cheaply, on the bottom upright; on a board with copper
// on one side a wire link leaps what a track cannot pass, dearer than any way
// round. A step nearer to other copper than it need be costs more. The
// tracks laid are then spread (withTracksSpread()); the tracks already there
// are kept as they are.

struct AutorouteResult
{
    Board board;
    // The connections there were to lay at first, those laid, those still
    // unrouted at the end, and the vias and the wire links added.
    int total = 0;
    int routed = 0;
    int unrouted = 0;
    int vias = 0;
    int links = 0;
    // Stopped by 'progress' before all were tried.
    bool stopped = false;
};

// The board of 'design' with the connections of the nets 'nets' laid, of
// every net when it is empty. Connections with a pad off the outline are
// left. 'progress', when set, is asked before each connection with how many
// are done and how many there were at first; false stops the routing,
// keeping what is laid. Without 'links' no wire link is laid.
AutorouteResult autorouted(const Design &design, const QSet<QString> &nets = {},
                           const std::function<bool(int done, int total)> &progress = {},
                           bool links = true);

} // namespace ppcb
