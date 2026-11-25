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
// How far apart the pieces 'a'-'b' and 'c'-'d' are (millimetres): 0 when
// they cross.
double piecesApart(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d);

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

// The copper of a pad of a part or of a mounting hole, where track ends
// lie and what tracks of other nets keep off.
struct PadPoint
{
    Point at;
    bool top = false;
    bool bottom = false;
    // The placement or the hole it is of.
    ObjectId owner;
    // Its net, empty for none.
    QString net;
    // Its copper as a line with a round end of 'radius' on each: the long
    // axis of a pill or a rectangle, the middle alone of a round pad.
    Point axisFrom;
    Point axisTo;
    Coord radius = 0;

    bool on(BoardSide side) const { return side == BoardSide::Top ? top : bottom; }
};

// The pads of the placements and of the holes with a pad of 'board'.
QList<PadPoint> padPoints(const Board &board, const Library &library);

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
// The wire link with a pad under 'at', within 'reach' of its edge; null when
// there is none.
ObjectId linkAt(const Board &board, const QPointF &at, double reach);
// The tracks, the vias and the wire links lying wholly inside 'rect'
// (millimetres).
QSet<ObjectId> tracksInside(const Board &board, const QRectF &rect);
// The track 'id' and the tracks going on from it end to end, on its side or
// through a via, up to a pad of a part or a hole and up to a fork: a point
// where three or more ends meet, or a piece with another track ending on
// its middle, which stays out unless it is 'id' itself. The vias whose
// tracks are all in it go with it: the whole of a track from pad to pad,
// as it was laid.
QSet<ObjectId> trackRun(const Board &board, const Library &library, const ObjectId &id);
// The runs (trackRun()) of the tracks ending on the middle of a pad, on a
// side it has copper on, of the placements or the holes 'ids': the tracks
// that go with them when they are taken off the board.
QSet<ObjectId> tracksOfPads(const Board &board, const Library &library,
                            const QSet<ObjectId> &ids);
// The tracks on a side 'board' has no copper on and, on a board with copper
// on one side only, every via: copper that joins nothing there.
QSet<ObjectId> offSideCopper(const Board &board);
// The board without the tracks, the vias and the wire links 'ids'.
Board withoutTracks(const Board &board, const QSet<ObjectId> &ids);
// The board without any track, via or wire link, as it is shown while they
// are hidden.
Board withoutAnyTracks(const Board &board);
// 'after', a change of 'before' that moved or turned parts or holes, with
// the ends of its tracks that lay on the middle of a pad of their net and
// side in 'before' moved to where that pad is now: the last piece of a
// track stretches after its part. A pad left where it was holds the ends on
// it; a piece shrunk to nothing goes. The tracks 'carried', moved whole
// with the parts, stay as they are in 'after'.
Board withTracksFollowingPads(const Board &before, const Board &after, const Library &library,
                              const QSet<ObjectId> &carried = {});
// 'after', a change of 'before' that gave the placements 'ids' other
// footprints, with their tracks going along: an end on the middle of a pad
// goes to the pad of the same number in the new footprint, the last piece
// stretching (one shrunk to nothing goes), and the runs (trackRun()) from a
// pad the new footprint lacks are deleted.
Board withTracksOnNewFootprints(const Board &before, const Board &after, const Library &library,
                                const QSet<ObjectId> &ids);
// 'after', a change of 'before' that stretched tracks after pads moved
// (withTracksFollowingPads(), withTracksOnNewFootprints()), without the runs
// (trackRun()) of the tracks whose shape the change altered where they now
// cross copper of another net on their side, or come nearer to it than the
// clearance of their net's class: those connections go back to the
// ratsnest instead of shorting. Tracks moved whole, or not there before,
// are left alone. The classes and the footprints come from 'design'.
Board withoutShortingStretches(const Board &before, const Board &after, const Design &design);
// Of the tracks 'ids' of 'board', those that cross copper of another net on
// their side, or come nearer to it than the clearance of their net's class;
// the pads a track ends on are its own.
QSet<ObjectId> shortingTracks(const Board &board, const Design &design, const QSet<ObjectId> &ids);

// Dragging tracks. The other piece of track ending where the end 'atTo'
// (else the start) of the track 'id' is, on its side, when the two alone
// meet there with no pad and no via: a corner of one track. Null otherwise.
ObjectId cornerNeighbour(const Board &board, const Library &library, const ObjectId &id,
                         bool atTo);
// The corner at that end of the track 'id' (cornerNeighbour()) moved to
// 'to', the two pieces laid again to it from their far ends: straight and at
// 45 degrees (trackCorners()), or straight with 'anyAngle'. The board as it
// is when there is no such corner.
Board withTrackCornerMoved(const Board &board, const Library &library, const ObjectId &id,
                           bool atTo, const Point &to, bool anyAngle);
// The piece 'id' moved across itself by 'delta': a level piece up or down, an
// upright one sideways, one at 45 degrees by whole cells of 'gridStep'
// across it, another by 'delta' as it is. A piece going on from an end is
// laid again to it from its far end, straight and at 45 degrees; an end on a
// pad, a via or a fork gets a new piece to it; a free end just moves. A
// piece ending on it between its ends is made longer or shorter along its
// way to meet it again; when one cannot, the board stays as it is.
Board withTrackPieceMoved(const Board &board, const Library &library, const ObjectId &id,
                          const Point &delta, Coord gridStep);
// 'board' with its pieces of track 'ids' (every one when empty) moved across
// themselves (withTrackPieceMoved()), a cell of 'gridStep' at a time and a
// few cells at most, wherever that leaves them more room to the copper of
// other nets on their side and to the edge of the board, keeping the
// clearances: tracks put in the middle of the room they run in. Only a
// piece between two corners (cornerNeighbour()) moves, so none is added.
Board withTracksSpread(const Board &board, const Design &design, Coord gridStep,
                       const QSet<ObjectId> &ids = {});
// The tracks, the vias and the wire links 'ids' moved by 'delta'.
Board withTracksMoved(const Board &board, const QSet<ObjectId> &ids, const Point &delta);
// The tracks and the vias 'ids' as laid by hand: no longer the
// autorouter's (Track::autorouted).
Board withTracksByHand(const Board &board, const QSet<ObjectId> &ids);
// The designators the wire links of 'board' have, the empty ones left out.
QStringList linkDesignators(const Board &board);
// 'board' with each wire link lacking a designator given the first free
// one of linkPrefix('style'), free of the components of 'schematic' and of
// the other links, in the order the links are on the board.
Board withLinkDesignators(const Board &board, const Schematic &schematic, DesignatorStyle style);
// 'board' with the designators of its wire links switched from style
// 'from' to 'to', as withDesignatorStyle() switches those of 'schematic',
// which has them switched already: the prefix changes, the rest stays; one
// that would repeat a designator gets the first free number instead.
Board withLinkDesignatorStyle(const Board &board, const Schematic &schematic,
                              DesignatorStyle from, DesignatorStyle to);
// The tracks 'ids' 'width' wide.
Board withTracksWidth(const Board &board, const QSet<ObjectId> &ids, Coord width);
// The tracks 'ids' moved to 'side'.
Board withTracksSide(const Board &board, const QSet<ObjectId> &ids, BoardSide side);

// The net class of the net 'net' of 'design' (netClassOf()); the default
// class for a net the schematic does not have, or for no net. Null only for
// a project without classes.
const NetClass *trackClass(const Design &design, const QString &net);

} // namespace ppcb
