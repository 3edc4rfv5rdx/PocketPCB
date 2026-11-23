#pragma once

#include "boardedit.h"
#include "project.h"

#include <QHash>

namespace ppcb {

// Updating the board from the schematic, one way only: every component of
// the schematic that is on the board and has a footprint the library holds
// gets a placement, tied to it by its id; the placements of the others go.
// A placement kept keeps where it is; its designator, value, footprint and
// the nets of its pads follow the component, named as withNetsNamedByPins()
// names them. A component on the board whose
// footprint the library lacks keeps its placement, if it has one, as it is.
// A placement removed takes its tracks with it (tracksOfPads()).
// The pads of mounting holes follow the nets they were on (holeNetsFollowed()).

// The net each net of the holes of 'before' is on after the board became
// 'after' (the same placements by id, their pads' nets updated): the one
// most of the pads of placements that were on it are on now, the first by
// name among equals; else itself, while some pad of 'after' is still on it;
// else none (empty).
QHash<QString, QString> holeNetsFollowed(const Board &before, const Board &after);

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
    // A component on the board whose footprint 'after' the library lacks:
    // its placement is kept as it is.
    MissingFootprint,
    // The same for a component with no placement yet: none is added.
    MissingFootprintNotAdded,
    // The pads of mounting holes on net 'before' go to net 'after', or to
    // none when it is empty; 'designator' is empty.
    HoleNet,
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
    // The project with a lasting name for every net with no name of its own
    // (withNetsNamedByPins()): the board's pads are on nets by these names.
    Project project;
    // In the order of the designators (designatorLess()); for one
    // placement, its kinds in the order of BoardChangeKind.
    QList<BoardChange> changes;
};

// The board of 'design' brought in line with its schematic. New placements
// stand packed as withPlacementsPacked() packs them.
BoardUpdate boardUpdatedFromSchematic(const Design &design);

} // namespace ppcb
