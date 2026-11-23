#pragma once

#include "project.h"

#include <QPointF>
#include <QSet>

#include <optional>

namespace ppcb {

// Laying tracks on the board and finding the copper they join.

// The points of a track from 'from' to 'to' at the angles of the board: a
// straight piece, level or upright, and a piece at 45 degrees, the
// straight one first or, with 'diagonalFirst', the slanted one. Both ends
// are in it, and no point twice: two points when one piece will do.
QList<Point> trackCorners(const Point &from, const Point &to, bool diagonalFirst);

// The point of the straight piece 'from'-'to' nearest to 'at'.
QPointF nearestOnPiece(const QPointF &from, const QPointF &to, const QPointF &at);

// Copper a track may start from or end on: a pad of a part, the pad of a
// mounting hole, a via, or a point of a track.
struct CopperPoint
{
    // Where a track joins it: the middle of a pad or of a via, the end of
    // a track, or the point of a track nearest to the cursor.
    Point at;
    QString net;
    // The sides of the board it is on, as the board's copper allows.
    bool top = false;
    bool bottom = false;

    bool on(BoardSide side) const { return side == BoardSide::Top ? top : bottom; }
};

// The pad of a part under 'at' (millimetres), or within 'reach' of it, the
// last placed first; nothing when there is none.
std::optional<CopperPoint> padAt(const Board &board, const Library &library, const QPointF &at,
                                 double reach);
// The copper under 'at' (millimetres), or within 'reach' millimetres of it:
// a pad first, then a via, then the end of a track, then a track; on either
// side, the one laid last of several. Nothing when there is none.
std::optional<CopperPoint> copperAt(const Board &board, const Library &library, const QPointF &at,
                                    double reach);

// The track under 'at' (millimetres), within 'reach' millimetres of its
// edge: one on 'first' before one on the other side, the one laid last of
// several; null when there is none.
ObjectId trackAt(const Board &board, const QPointF &at, double reach, BoardSide first);
// The via under 'at', within 'reach' of its edge; null when there is none.
ObjectId viaAt(const Board &board, const QPointF &at, double reach);
// The tracks and the vias lying wholly inside 'rect' (millimetres).
QSet<ObjectId> tracksInside(const Board &board, const QRectF &rect);
// The track 'id' and every track joined to it end to end on its side, or
// through a via, up to the pads: the whole of a track as it was laid.
QSet<ObjectId> trackRun(const Board &board, const ObjectId &id);
// The board without the tracks and the vias 'ids'.
Board withoutTracks(const Board &board, const QSet<ObjectId> &ids);
// The board without any track or via, as it is shown while they are hidden.
Board withoutAnyTracks(const Board &board);
// The tracks 'ids' 'width' wide.
Board withTracksWidth(const Board &board, const QSet<ObjectId> &ids, Coord width);
// The tracks 'ids' moved to 'side'.
Board withTracksSide(const Board &board, const QSet<ObjectId> &ids, BoardSide side);

// The net class of the net 'net' of 'design' (netClassOf()); the default
// class for a net the schematic does not have, or for no net. Null only for
// a project without classes.
const NetClass *trackClass(const Design &design, const QString &net);

} // namespace ppcb
