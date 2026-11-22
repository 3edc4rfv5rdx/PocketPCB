#include "boardedit.h"

#include <QLineF>

#include <algorithm>

namespace ppcb {

namespace {

constexpr int QUARTER_TURN = 90;
constexpr int FULL_TURN = 360;

// A quarter turn clockwise on the board, where Y grows downwards: what was
// to the right goes below.
Point turned(const Point &offset)
{
    return Point{-offset.y, offset.x};
}

// True when 'items' hold one of 'ids' and every one of them has 'property'.
template <typename Item, typename Property>
bool allHave(const QList<Item> &items, const QSet<ObjectId> &ids, Property property)
{
    bool any = false;
    for (const Item &item : items) {
        if (!ids.contains(item.id))
            continue;
        if (!property(item))
            return false;
        any = true;
    }
    return any;
}

} // namespace

ObjectId placementAt(const Board &board, const Library &library, const QPointF &at)
{
    for (auto it = board.placements.crbegin(); it != board.placements.crend(); ++it) {
        if (placementRect(*it, library).contains(at))
            return it->id;
    }
    return ObjectId();
}

QSet<ObjectId> placementsInside(const Board &board, const Library &library, const QRectF &rect)
{
    QSet<ObjectId> inside;
    for (const Placement &placement : board.placements) {
        if (rect.contains(placementRect(placement, library)))
            inside.insert(placement.id);
    }
    return inside;
}

QRectF placementsBounds(const Board &board, const Library &library, const QSet<ObjectId> &ids)
{
    QRectF bounds;
    for (const Placement &placement : board.placements) {
        if (ids.contains(placement.id))
            bounds = bounds.isNull() ? placementRect(placement, library)
                                     : bounds.united(placementRect(placement, library));
    }
    return bounds;
}

bool operator==(const PlacementText &a, const PlacementText &b)
{
    return a.owner == b.owner && a.text == b.text;
}

PlacementText placementTextAt(const Board &board, const QPointF &at)
{
    for (auto it = board.placements.crbegin(); it != board.placements.crend(); ++it) {
        for (const ComponentText text : {ComponentText::Value, ComponentText::Designator}) {
            // A text not shown has a rectangle of no size, which still holds
            // its own point.
            if (!placementTextShown(*it, text).isEmpty()
                && placementTextRect(*it, text).contains(at))
                return PlacementText{it->id, text};
        }
    }
    return PlacementText();
}

Point placementTextPoint(const Board &board, const PlacementText &text)
{
    const Placement *placement = findById(board.placements, text.owner);
    return placement == nullptr ? Point() : placementTextPoint(*placement, text.text);
}

Board withPlacementsMoved(const Board &board, const QSet<ObjectId> &ids, const Point &delta)
{
    Board result = board;
    for (Placement &placement : result.placements) {
        if (ids.contains(placement.id) && !placement.locked)
            placement.at = placement.at + delta;
    }
    return result;
}

Board withPlacementsRotated(const Board &board, const QSet<ObjectId> &ids, const Point &center)
{
    Board result = board;
    for (Placement &placement : result.placements) {
        if (!ids.contains(placement.id) || placement.locked)
            continue;
        placement.at = center + turned(placement.at - center);
        placement.rotation = (placement.rotation + QUARTER_TURN) % FULL_TURN;
        placement.refAt = turned(placement.refAt);
        placement.valueAt = turned(placement.valueAt);
    }
    return result;
}

Board withPlacementTextMoved(const Board &board, const PlacementText &text, const Point &delta)
{
    Board result = board;
    for (Placement &placement : result.placements) {
        if (placement.id != text.owner)
            continue;
        Point &offset =
            text.text == ComponentText::Designator ? placement.refAt : placement.valueAt;
        offset = offset + delta;
    }
    return result;
}

Board withPlacementsLocked(const Board &board, const QSet<ObjectId> &ids, bool locked)
{
    Board result = board;
    for (Placement &placement : result.placements) {
        if (ids.contains(placement.id))
            placement.locked = locked;
    }
    return result;
}

bool arePlacementsLocked(const Board &board, const QSet<ObjectId> &ids)
{
    return allHave(board.placements, ids, [](const Placement &item) { return item.locked; });
}

ObjectId holeAt(const Board &board, const QPointF &at, double reach)
{
    for (auto it = board.holes.crbegin(); it != board.holes.crend(); ++it) {
        const double radius = std::max(nmToMm(holeOuterDiameter(*it)) / 2, reach);
        if (QLineF(toMm(it->at), at).length() <= radius)
            return it->id;
    }
    return ObjectId();
}

QSet<ObjectId> holesInside(const Board &board, const QRectF &rect)
{
    QSet<ObjectId> inside;
    for (const MountingHole &hole : board.holes) {
        const double radius = nmToMm(holeOuterDiameter(hole)) / 2;
        const QPointF middle = toMm(hole.at);
        if (rect.contains(QRectF(middle - QPointF(radius, radius),
                                 middle + QPointF(radius, radius))))
            inside.insert(hole.id);
    }
    return inside;
}

MountingHole newHole(const Point &at, Coord drill)
{
    MountingHole hole;
    hole.id = newObjectId();
    hole.at = at;
    hole.drill = drill;
    return hole;
}

Board withHolesMoved(const Board &board, const QSet<ObjectId> &ids, const Point &delta)
{
    Board result = board;
    for (MountingHole &hole : result.holes) {
        if (ids.contains(hole.id) && !hole.locked)
            hole.at = hole.at + delta;
    }
    return result;
}

Board withHolesDrill(const Board &board, const QSet<ObjectId> &ids, Coord drill)
{
    Board result = board;
    for (MountingHole &hole : result.holes) {
        if (ids.contains(hole.id) && !hole.locked)
            hole.drill = drill;
    }
    return result;
}

Board withHolesPlated(const Board &board, const QSet<ObjectId> &ids, bool plated)
{
    Board result = board;
    for (MountingHole &hole : result.holes) {
        if (!ids.contains(hole.id) || hole.locked)
            continue;
        hole.plated = plated;
        if (!plated) {
            hole.ring = 0;
            hole.net.clear();
        } else if (hole.ring == 0) {
            hole.ring = DEFAULT_HOLE_RING;
        }
    }
    return result;
}

Board withHolesRing(const Board &board, const QSet<ObjectId> &ids, Coord ring)
{
    Board result = board;
    for (MountingHole &hole : result.holes) {
        if (ids.contains(hole.id) && hole.plated && !hole.locked)
            hole.ring = ring;
    }
    return result;
}

Board withoutHoles(const Board &board, const QSet<ObjectId> &ids)
{
    Board result = board;
    result.holes.removeIf(
        [&ids](const MountingHole &hole) { return ids.contains(hole.id) && !hole.locked; });
    return result;
}

bool areHolesPlated(const Board &board, const QSet<ObjectId> &ids)
{
    return allHave(board.holes, ids, [](const MountingHole &item) { return item.plated; });
}

Board withHolesLocked(const Board &board, const QSet<ObjectId> &ids, bool locked)
{
    Board result = board;
    for (MountingHole &hole : result.holes) {
        if (ids.contains(hole.id))
            hole.locked = locked;
    }
    return result;
}

bool areHolesLocked(const Board &board, const QSet<ObjectId> &ids)
{
    return allHave(board.holes, ids, [](const MountingHole &item) { return item.locked; });
}

} // namespace ppcb
