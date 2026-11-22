#pragma once

#include "board.h"
#include "library.h"
#include "paint.h"

#include <QPointF>
#include <QRectF>
#include <QSet>

namespace ppcb {

// Choosing and changing the placements of the board. A locked placement
// (Placement::locked) is not moved or turned by any of these; its texts may
// still be moved, as they are the silkscreen's, not the part's place.

// The placement under 'at' (millimetres), the one drawn last of several;
// null when there is none.
ObjectId placementAt(const Board &board, const Library &library, const QPointF &at);
// The placements lying wholly inside 'rect' (millimetres).
QSet<ObjectId> placementsInside(const Board &board, const Library &library, const QRectF &rect);
// The rectangle holding the placements 'ids'; empty when there are none.
QRectF placementsBounds(const Board &board, const Library &library, const QSet<ObjectId> &ids);

// A text a placement shows on the silkscreen: its designator or its value.
struct PlacementText
{
    ObjectId owner;
    ComponentText text = ComponentText::Designator;

    bool isNull() const { return owner.isNull(); }
};
bool operator==(const PlacementText &a, const PlacementText &b);

// The text shown under 'at' (millimetres), the one drawn last of several;
// null when there is none.
PlacementText placementTextAt(const Board &board, const QPointF &at);
// Where that text stands; the origin when the board has no such placement.
Point placementTextPoint(const Board &board, const PlacementText &text);

// The placements 'ids' moved by 'delta', their texts with them.
Board withPlacementsMoved(const Board &board, const QSet<ObjectId> &ids, const Point &delta);
// The placements 'ids' turned by 90 degrees clockwise, as seen from the top,
// about 'center', their texts going round them. Turning four times gives
// the board back.
Board withPlacementsRotated(const Board &board, const QSet<ObjectId> &ids, const Point &center);
// The text moved by 'delta' away from where it stands; its placement stays.
Board withPlacementTextMoved(const Board &board, const PlacementText &text, const Point &delta);
// The placements 'ids' locked or unlocked.
Board withPlacementsLocked(const Board &board, const QSet<ObjectId> &ids, bool locked);
// True when there is a placement among 'ids' and every one of them is locked.
bool arePlacementsLocked(const Board &board, const QSet<ObjectId> &ids);

// Mounting holes (MountingHole).

// The diameter a new hole has until another is chosen: for an M3 screw.
constexpr Coord DEFAULT_HOLE_DRILL = 3200000;
// The width of the copper ring a hole without one gets when it is made
// plated.
constexpr Coord DEFAULT_HOLE_RING = 1000000;

// The hole under 'at' (millimetres), or within 'reach' millimetres of its
// middle when it is smaller than that; the one placed last of several;
// null when there is none.
ObjectId holeAt(const Board &board, const QPointF &at, double reach);
// The holes lying wholly inside 'rect' (millimetres).
QSet<ObjectId> holesInside(const Board &board, const QRectF &rect);
// A new hole of diameter 'drill' at 'at', without plating.
MountingHole newHole(const Point &at, Coord drill);
// A locked hole (MountingHole::locked) is not moved, changed or taken off
// by any of these.
// The holes 'ids' moved by 'delta'.
Board withHolesMoved(const Board &board, const QSet<ObjectId> &ids, const Point &delta);
// The holes 'ids' of diameter 'drill'.
Board withHolesDrill(const Board &board, const QSet<ObjectId> &ids, Coord drill);
// The holes 'ids' made plated, a hole without a ring getting
// DEFAULT_HOLE_RING, or not plated, keeping their rings.
Board withHolesPlated(const Board &board, const QSet<ObjectId> &ids, bool plated);
// The copper ring of 'hole' that makes a pad of 'diameter' around it; 0 for
// a diameter of 0 or one not larger than the hole.
Coord padRing(const MountingHole &hole, Coord diameter);
// A pad of 'diameter' fits 'hole': larger than the hole, or 0 (no pad)
// for a hole without plating, as a plated one needs its ring.
bool takesPad(const MountingHole &hole, Coord diameter);
// The holes 'ids' with copper pads of 'diameter' (the ring's width going by
// each hole's own); 0 takes the pad and its net off. A hole the pad does
// not fit (takesPad()) keeps its own.
Board withHolesPad(const Board &board, const QSet<ObjectId> &ids, Coord diameter);
// The board without the holes 'ids'.
Board withoutHoles(const Board &board, const QSet<ObjectId> &ids);
// True when there is a hole among 'ids' and every one of them is plated.
bool areHolesPlated(const Board &board, const QSet<ObjectId> &ids);
// The holes 'ids' locked or unlocked.
Board withHolesLocked(const Board &board, const QSet<ObjectId> &ids, bool locked);
// True when there is a hole among 'ids' and every one of them is locked.
bool areHolesLocked(const Board &board, const QSet<ObjectId> &ids);

} // namespace ppcb
