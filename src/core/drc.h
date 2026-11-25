#pragma once

#include "project.h"

#include <QList>
#include <QString>

namespace ppcb {

// The design rules check of the board: what the copper, the holes and the
// tracks of the board break of the project's rules (Rules), each at the
// level the rules give it; a check set Off finds nothing.

enum class DrcKind {
    // Copper of two nets touching or overlapping, on a side both are on;
    // always an error. Copper of no net counts as a net of its own.
    Short,
    // Copper of two nets nearer than the clearance of their classes, the
    // larger of the two; or copper nearer than it to a hole without a pad.
    Clearance,
    // Copper nearer to the outline than the edge clearance, or out of it.
    EdgeClearance,
    // The copper round the hole of a pad, a via, a wire link's pad or a
    // mounting hole's ring narrower than the rules allow.
    AnnularRing,
    // A track narrower than its net's class asks for.
    ClassWidth,
    // A track narrower than the smallest of the rules' track widths.
    MinimumWidth,
    // Holes of a diameter not in the drill set, when only those may be
    // used (Rules::drillsOnly): one problem for each diameter; a warning.
    DrillNotInSet,
    // A placement whose pads all lie out of the outline: one problem for it,
    // and its pads are left out of the other checks; a warning.
    OffBoard,
    // A connection of the ratsnest still to be laid; a warning.
    Unrouted,
};

// Copper or a hole a problem is about.
struct DrcItem
{
    enum class Kind { Pad, Track, Via, LinkPad, HolePad, Hole };
    Kind kind = Kind::Track;
    // The track, via, wire link, mounting hole or, for a pad, placement.
    ObjectId id;
    // The pad's designator and number ("R1.2"), a wire link's designator,
    // else empty.
    QString name;
    QString net;
};

struct DrcProblem
{
    DrcKind kind = DrcKind::Short;
    // An error, else a warning.
    bool error = true;
    // Where to look on the board: between the two items, at the narrow
    // place, the hole, the middle of a connection.
    Point at;
    // What it is about: two items for Short and Clearance, one for most,
    // none for DrillNotInSet and Unrouted.
    QList<DrcItem> items;
    // What there is and what the rules ask for, in nanometres: the gap and
    // the clearance, the width, the ring; the diameter for DrillNotInSet.
    Coord actual = 0;
    Coord required = 0;
    // The holes of that diameter, for DrillNotInSet.
    int count = 0;
    // For OffBoard, the placement's designator; for Unrouted, the net.
    QString name;
};

// The problems of the board of 'design', errors first, then by kind in the
// order of DrcKind, then from the top left.
QList<DrcProblem> drcProblems(const Design &design);

} // namespace ppcb
