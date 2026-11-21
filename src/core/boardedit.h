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

} // namespace ppcb
