#pragma once

#include "project.h"

namespace ppcb {

// Updating the board from the schematic, one way only: every component of
// the schematic that is on the board and has a footprint the library holds
// gets a placement, tied to it by its id; the placements of the others go.
// A placement kept keeps where it is; its designator, value, footprint and
// the nets of its pads follow the component.

// What the update does to one placement.
enum class BoardChangeKind {
    // A new placement for a component; 'after' is its footprint.
    Add,
    // A placement whose component is gone, is off the board or has no
    // footprint any more.
    Remove,
    // 'before' and 'after' are the old and the new text.
    Designator,
    Value,
    Footprint,
    // The nets of its pads changed.
    Nets,
};

struct BoardChange
{
    BoardChangeKind kind = BoardChangeKind::Add;
    // The designator the placement has after the update; the one it had,
    // for a placement removed.
    QString designator;
    QString before;
    QString after;
};

bool operator==(const BoardChange &a, const BoardChange &b);

struct BoardUpdate
{
    Board board;
    // In the order of the designators (designatorLess()); for one
    // placement, its kinds in the order of BoardChangeKind.
    QList<BoardChange> changes;
};

// The gap between new placements, and between them and the board, in
// nanometres.
constexpr Coord NEW_PLACEMENT_GAP = 2540000;

// The board of 'design' brought in line with its schematic. New placements
// stand in columns to the right of the outline and of the placements there
// are, from the top of the outline down, each column no taller than the
// outline (or 50 mm without one), on the board's grid.
BoardUpdate boardUpdatedFromSchematic(const Design &design);

} // namespace ppcb
