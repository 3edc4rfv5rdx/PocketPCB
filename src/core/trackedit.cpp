#include "trackedit.h"

#include "boardedit.h"
#include "bom.h"
#include "grid.h"
#include "netlist.h"
#include "paint.h"
#include "ratsnest.h"
#include "sheetedit.h"

#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>
#include <QRectF>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <utility>

namespace ppcb {

namespace {

Coord sign(Coord value)
{
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

// The sides 'sides' has, as the copper of 'board' allows.
void setSides(CopperPoint &point, const Board &board, CopperSides sides)
{
    point.top = copperOn(sides, BoardSide::Top) && copperOn(board.copper, BoardSide::Top);
    point.bottom =
        copperOn(sides, BoardSide::Bottom) && copperOn(board.copper, BoardSide::Bottom);
}

} // namespace

QPointF nearestOnPiece(const QPointF &from, const QPointF &to, const QPointF &at)
{
    const QPointF along = to - from;
    const double length = QPointF::dotProduct(along, along);
    if (length == 0)
        return from;
    const double t = std::clamp(QPointF::dotProduct(at - from, along) / length, 0.0, 1.0);
    return from + along * t;
}

double piecesApart(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d)
{
    const auto cross = [](const QPointF &o, const QPointF &p, const QPointF &q) {
        return (p.x() - o.x()) * (q.y() - o.y()) - (p.y() - o.y()) * (q.x() - o.x());
    };
    const double abc = cross(a, b, c);
    const double abd = cross(a, b, d);
    const double cda = cross(c, d, a);
    const double cdb = cross(c, d, b);
    if (((abc > 0 && abd < 0) || (abc < 0 && abd > 0))
        && ((cda > 0 && cdb < 0) || (cda < 0 && cdb > 0)))
        return 0;
    const auto apart = [](const QPointF &from, const QPointF &to, const QPointF &at) {
        return QLineF(nearestOnPiece(from, to, at), at).length();
    };
    return std::min({apart(a, b, c), apart(a, b, d), apart(c, d, a), apart(c, d, b)});
}

QList<Point> trackCorners(const Point &from, const Point &to, bool diagonalFirst)
{
    const Coord dx = to.x - from.x;
    const Coord dy = to.y - from.y;
    const Coord slant = std::min(std::abs(dx), std::abs(dy));
    const Point corner =
        diagonalFirst ? Point{from.x + sign(dx) * slant, from.y + sign(dy) * slant}
                      : Point{from.x + sign(dx) * (std::abs(dx) - slant),
                              from.y + sign(dy) * (std::abs(dy) - slant)};
    QList<Point> points = {from};
    for (const Point &point : {corner, to}) {
        if (point != points.last())
            points.append(point);
    }
    return points;
}

std::optional<CopperPoint> padAt(const Board &board, const Library &library, const QPointF &at,
                                 double reach)
{
    // Within the square of their larger size.
    for (auto it = board.placements.crbegin(); it != board.placements.crend(); ++it) {
        const Footprint *footprint = library.findFootprint(it->footprint);
        if (footprint == nullptr)
            continue;
        for (auto pad = footprint->pads.crbegin(); pad != footprint->pads.crend(); ++pad) {
            const QPointF middle = toMm(placedPadPoint(*it, pad->at));
            const double half = nmToMm(std::max(pad->sizeX, pad->sizeY)) / 2 + reach;
            if (std::abs(at.x() - middle.x()) > half || std::abs(at.y() - middle.y()) > half)
                continue;
            CopperPoint point;
            point.at = placedPadPoint(*it, pad->at);
            point.net = it->padNets.value(pad->number);
            setSides(point, board, padCopperSides(pad->layers, it->side));
            if (point.top || point.bottom)
                return point;
        }
    }
    return std::nullopt;
}

std::optional<CopperPoint> copperAt(const Board &board, const Library &library, const QPointF &at,
                                    double reach)
{
    if (const std::optional<CopperPoint> pad = padAt(board, library, at, reach))
        return pad;
    // Pads of mounting holes.
    for (auto it = board.holes.crbegin(); it != board.holes.crend(); ++it) {
        const double radius = nmToMm(holeOuterDiameter(*it)) / 2 + reach;
        if (it->ring == 0 || QLineF(toMm(it->at), at).length() > radius)
            continue;
        CopperPoint point;
        point.at = it->at;
        point.net = it->net;
        point.top = holePadOn(board, *it, BoardSide::Top);
        point.bottom = holePadOn(board, *it, BoardSide::Bottom);
        if (point.top || point.bottom)
            return point;
    }
    for (auto it = board.vias.crbegin(); it != board.vias.crend(); ++it) {
        if (QLineF(toMm(it->at), at).length() > nmToMm(it->size) / 2 + reach)
            continue;
        CopperPoint point;
        point.at = it->at;
        point.net = it->net;
        setSides(point, board, CopperSides::Both);
        return point;
    }
    // The pads of wire links.
    for (auto it = board.links.crbegin(); it != board.links.crend(); ++it) {
        for (const Point &end : {it->to, it->from}) {
            if (QLineF(toMm(end), at).length() > nmToMm(it->size) / 2 + reach)
                continue;
            CopperPoint point;
            point.at = end;
            point.net = it->net;
            setSides(point, board, CopperSides::Both);
            return point;
        }
    }
    // The ends of tracks, then any point of them.
    const auto trackPoint = [&board](const Track &track, const Point &where) {
        CopperPoint point;
        point.at = where;
        point.net = track.net;
        setSides(point, board,
                 track.side == BoardSide::Top ? CopperSides::Top : CopperSides::Bottom);
        return point;
    };
    for (auto it = board.tracks.crbegin(); it != board.tracks.crend(); ++it) {
        const double radius = nmToMm(it->width) / 2 + reach;
        for (const Point &end : {it->to, it->from}) {
            if (QLineF(toMm(end), at).length() <= radius)
                return trackPoint(*it, end);
        }
    }
    for (auto it = board.tracks.crbegin(); it != board.tracks.crend(); ++it) {
        const QPointF nearest = nearestOnPiece(toMm(it->from), toMm(it->to), at);
        if (QLineF(nearest, at).length() <= nmToMm(it->width) / 2 + reach)
            return trackPoint(*it, Point{mmToNm(nearest.x()), mmToNm(nearest.y())});
    }
    return std::nullopt;
}

ObjectId trackAt(const Board &board, const QPointF &at, double reach, BoardSide first)
{
    for (const BoardSide side : {first, first == BoardSide::Top ? BoardSide::Bottom
                                                                 : BoardSide::Top}) {
        for (auto it = board.tracks.crbegin(); it != board.tracks.crend(); ++it) {
            if (it->side != side)
                continue;
            const QPointF nearest = nearestOnPiece(toMm(it->from), toMm(it->to), at);
            if (QLineF(nearest, at).length() <= nmToMm(it->width) / 2 + reach)
                return it->id;
        }
    }
    return ObjectId();
}

ObjectId viaAt(const Board &board, const QPointF &at, double reach)
{
    for (auto it = board.vias.crbegin(); it != board.vias.crend(); ++it) {
        if (QLineF(toMm(it->at), at).length() <= nmToMm(it->size) / 2 + reach)
            return it->id;
    }
    return ObjectId();
}

ObjectId linkAt(const Board &board, const QPointF &at, double reach)
{
    for (auto it = board.links.crbegin(); it != board.links.crend(); ++it) {
        for (const Point &end : {it->to, it->from}) {
            if (QLineF(toMm(end), at).length() <= nmToMm(it->size) / 2 + reach)
                return it->id;
        }
    }
    return ObjectId();
}

QSet<ObjectId> tracksInside(const Board &board, const QRectF &rect)
{
    QSet<ObjectId> inside;
    for (const Track &track : board.tracks) {
        const double half = nmToMm(track.width) / 2;
        const QRectF covered = QRectF(toMm(track.from), toMm(track.to))
                                   .normalized()
                                   .adjusted(-half, -half, half, half);
        if (rect.contains(covered))
            inside.insert(track.id);
    }
    for (const Via &via : board.vias) {
        const double half = nmToMm(via.size) / 2;
        const QPointF middle = toMm(via.at);
        if (rect.contains(QRectF(middle - QPointF(half, half), middle + QPointF(half, half))))
            inside.insert(via.id);
    }
    for (const WireLink &link : board.links) {
        const double half = nmToMm(link.size) / 2;
        const QRectF covered = QRectF(toMm(link.from), toMm(link.to))
                                   .normalized()
                                   .adjusted(-half, -half, half, half);
        if (rect.contains(covered))
            inside.insert(link.id);
    }
    return inside;
}

namespace {



// 'at' lies on the piece of 'track' between its ends, not on an end.
bool insidePiece(const Track &track, const Point &at)
{
    if (at == track.from || at == track.to)
        return false;
    const Point along = track.to - track.from;
    const Point to = at - track.from;
    if (along.x * to.y - along.y * to.x != 0)
        return false;
    return std::min(track.from.x, track.to.x) <= at.x && at.x <= std::max(track.from.x, track.to.x)
           && std::min(track.from.y, track.to.y) <= at.y
           && at.y <= std::max(track.from.y, track.to.y);
}

QSet<ObjectId> runOf(const Board &board, const QList<PadPoint> &pads, const ObjectId &id)
{
    QSet<ObjectId> run;
    if (findById(board.tracks, id) == nullptr)
        return run;
    const auto viaAtPoint = [&board](const Point &at) {
        return std::any_of(board.vias.cbegin(), board.vias.cend(),
                           [&at](const Via &via) { return via.at == at; });
    };
    // A piece another track ends on the middle of is a fork.
    const auto forked = [&board, &viaAtPoint](const Track &piece) {
        return std::any_of(board.tracks.cbegin(), board.tracks.cend(), [&](const Track &other) {
            if (other.id == piece.id)
                return false;
            for (const Point &end : {other.from, other.to}) {
                if ((other.side == piece.side || viaAtPoint(end)) && insidePiece(piece, end))
                    return true;
            }
            return false;
        });
    };
    QList<ObjectId> open = {id};
    run.insert(id);
    while (!open.isEmpty()) {
        const Track &track = *findById(board.tracks, open.takeLast());
        for (const Point &end : {track.from, track.to}) {
            // A pad ends the run.
            if (std::any_of(pads.cbegin(), pads.cend(), [&](const PadPoint &pad) {
                    return pad.at == end && pad.on(track.side);
                }))
                continue;
            // What goes on from here: on its side, or through a via on both.
            const bool via = viaAtPoint(end);
            QList<const Track *> next;
            for (const Track &other : board.tracks) {
                if (other.id != track.id && (other.side == track.side || via)
                    && (other.from == end || other.to == end))
                    next.append(&other);
            }
            // At a fork, or into a piece with a fork on it, the run stops.
            if (next.size() != 1 || forked(*next.first()))
                continue;
            if (!run.contains(next.first()->id)) {
                run.insert(next.first()->id);
                open.append(next.first()->id);
            }
        }
    }
    // The vias all of whose tracks are in it.
    for (const Via &via : board.vias) {
        bool touched = false;
        bool whole = true;
        for (const Track &track : board.tracks) {
            if (track.from != via.at && track.to != via.at)
                continue;
            touched = touched || run.contains(track.id);
            whole = whole && run.contains(track.id);
        }
        if (touched && whole)
            run.insert(via.id);
    }
    return run;
}

} // namespace

QList<PadPoint> padPoints(const Board &board, const Library &library)
{
    QList<PadPoint> points;
    for (const Placement &placement : board.placements) {
        const Footprint *footprint = library.findFootprint(placement.footprint);
        if (footprint == nullptr)
            continue;
        for (const Pad &pad : footprint->pads) {
            const CopperSides sides = padCopperSides(pad.layers, placement.side);
            const Coord narrow = std::min(pad.sizeX, pad.sizeY);
            const Point half = pad.sizeX > pad.sizeY ? Point{(pad.sizeX - narrow) / 2, 0}
                                                     : Point{0, (pad.sizeY - narrow) / 2};
            points.append(PadPoint{placedPadPoint(placement, pad.at),
                                   copperOn(sides, BoardSide::Top),
                                   copperOn(sides, BoardSide::Bottom), placement.id,
                                   placement.padNets.value(pad.number),
                                   placedPadPoint(placement, pad.at - half),
                                   placedPadPoint(placement, pad.at + half), narrow / 2});
        }
    }
    for (const MountingHole &hole : board.holes) {
        if (hole.ring > 0)
            points.append(PadPoint{hole.at, holePadOn(board, hole, BoardSide::Top),
                                   holePadOn(board, hole, BoardSide::Bottom), hole.id, hole.net,
                                   hole.at, hole.at, holeOuterDiameter(hole) / 2});
    }
    // The pads at the ends of wire links, through the board.
    for (const WireLink &link : board.links) {
        for (const Point &end : {link.from, link.to})
            points.append(PadPoint{end, copperOn(board.copper, BoardSide::Top),
                                   copperOn(board.copper, BoardSide::Bottom), link.id, link.net,
                                   end, end, link.size / 2});
    }
    return points;
}

QSet<ObjectId> trackRun(const Board &board, const Library &library, const ObjectId &id)
{
    return runOf(board, padPoints(board, library), id);
}

QSet<ObjectId> tracksOfPads(const Board &board, const Library &library,
                            const QSet<ObjectId> &ids)
{
    const QList<PadPoint> pads = padPoints(board, library);
    QSet<ObjectId> tracks;
    for (const Track &track : board.tracks) {
        if (tracks.contains(track.id))
            continue;
        const bool onPad = std::any_of(pads.cbegin(), pads.cend(), [&](const PadPoint &pad) {
            return ids.contains(pad.owner) && pad.on(track.side)
                   && (pad.at == track.from || pad.at == track.to);
        });
        if (onPad)
            tracks.unite(runOf(board, pads, track.id));
    }
    return tracks;
}

QSet<ObjectId> offSideCopper(const Board &board)
{
    QSet<ObjectId> off;
    for (const Track &track : board.tracks) {
        if (!copperOn(board.copper, track.side))
            off.insert(track.id);
    }
    if (board.copper != CopperSides::Both) {
        for (const Via &via : board.vias)
            off.insert(via.id);
    }
    return off;
}

Board withoutTracks(const Board &board, const QSet<ObjectId> &ids)
{
    Board result = board;
    result.tracks.removeIf([&ids](const Track &track) { return ids.contains(track.id); });
    result.vias.removeIf([&ids](const Via &via) { return ids.contains(via.id); });
    result.links.removeIf([&ids](const WireLink &link) { return ids.contains(link.id); });
    return result;
}

Board withoutAnyTracks(const Board &board)
{
    Board result = board;
    result.tracks.clear();
    result.vias.clear();
    result.links.clear();
    return result;
}

Board withTracksFollowingPads(const Board &before, const Board &after, const Library &library,
                              const QSet<ObjectId> &carried)
{
    // The same pads in the same order, as only their places changed.
    const QList<NetPad> was = netPads(before, library);
    const QList<NetPad> now = netPads(after, library);
    if (was.size() != now.size())
        return after;
    const auto followed = [&was, &now](const Track &track, const Point &end) {
        std::optional<Point> moved;
        for (int i = 0; i < was.size(); ++i) {
            const NetPad &pad = was.at(i);
            const bool onSide = track.side == BoardSide::Top ? pad.top : pad.bottom;
            if (pad.at != end || pad.net != track.net || !onSide || now.at(i).net != pad.net)
                continue;
            if (now.at(i).at == pad.at)
                return end;
            if (!moved)
                moved = now.at(i).at;
        }
        return moved.value_or(end);
    };
    Board result = after;
    for (Track &track : result.tracks) {
        if (carried.contains(track.id))
            continue;
        track.from = followed(track, track.from);
        track.to = followed(track, track.to);
    }
    result.tracks.removeIf([](const Track &track) { return track.from == track.to; });
    return result;
}

Board withPartsFlipped(const Board &board, const Library &library, const QSet<ObjectId> &ids,
                       const Point &center)
{
    const Board flipped = withPlacementsFlipped(board, library, ids, center);
    // The same pads in the same order, as only the placements changed.
    const QList<PadPoint> was = padPoints(board, library);
    const QList<PadPoint> now = padPoints(flipped, library);
    QSet<ObjectId> left;
    if (was.size() == now.size()) {
        for (const Track &track : board.tracks) {
            for (int i = 0; i < was.size(); ++i) {
                const PadPoint &pad = was.at(i);
                if (pad.on(track.side) && !now.at(i).on(track.side)
                    && (pad.at == track.from || pad.at == track.to))
                    left.unite(runOf(board, was, track.id));
            }
        }
    }
    return withoutTracks(withTracksFollowingPads(board, flipped, library), left);
}

Board withTracksOnNewFootprints(const Board &before, const Board &after, const Library &library,
                                const QSet<ObjectId> &ids)
{
    const QList<PadPoint> pads = padPoints(before, library);
    // Where the ends on the old pads go, and the runs from pads gone.
    QList<std::pair<Point, Point>> moves;
    QSet<ObjectId> gone;
    for (const Placement &old : before.placements) {
        const Placement *now = findById(after.placements, old.id);
        if (!ids.contains(old.id) || now == nullptr)
            continue;
        const Footprint *was = library.findFootprint(old.footprint);
        const Footprint *is = library.findFootprint(now->footprint);
        if (was == nullptr || is == nullptr)
            continue;
        for (const Pad &pad : was->pads) {
            const Point from = placedPadPoint(old, pad.at);
            const auto same = std::find_if(is->pads.cbegin(), is->pads.cend(), [&pad](const Pad &p) {
                return !pad.number.isEmpty() && p.number == pad.number;
            });
            if (same == is->pads.cend()) {
                for (const Track &track : before.tracks) {
                    if (track.from == from || track.to == from)
                        gone.unite(runOf(before, pads, track.id));
                }
                continue;
            }
            const Point to = placedPadPoint(*now, same->at);
            if (to != from)
                moves.append({from, to});
        }
    }
    Board result = withoutTracks(after, gone);
    const auto moved = [&moves](const Point &end) {
        for (const auto &[from, to] : moves) {
            if (end == from)
                return to;
        }
        return end;
    };
    for (Track &track : result.tracks) {
        track.from = moved(track.from);
        track.to = moved(track.to);
    }
    result.tracks.removeIf([](const Track &track) { return track.from == track.to; });
    return result;
}

QSet<ObjectId> shortingTracks(const Board &board, const Design &design, const QSet<ObjectId> &ids)
{
    const QList<PadPoint> pads = padPoints(board, design.library);
    QSet<ObjectId> shorting;
    for (const Track &track : board.tracks) {
        if (!ids.contains(track.id))
            continue;
        const NetClass *netClass = trackClass(design, track.net);
        const double clearance =
            nmToMm(netClass != nullptr ? netClass->clearance : design.project.rules.clearance);
        const double half = nmToMm(track.width) / 2;
        const QPointF a = toMm(track.from);
        const QPointF b = toMm(track.to);
        // Too near the copper of another net on its side.
        bool shorts = false;
        for (const Track &other : board.tracks) {
            shorts = shorts
                     || (other.side == track.side && other.net != track.net
                         && piecesApart(a, b, toMm(other.from), toMm(other.to))
                                < half + nmToMm(other.width) / 2 + clearance);
        }
        for (const Via &via : board.vias) {
            shorts = shorts
                     || (via.net != track.net
                         && QLineF(nearestOnPiece(a, b, toMm(via.at)), toMm(via.at)).length()
                                < half + nmToMm(via.size) / 2 + clearance);
        }
        // The pads it ends on are its own, whatever their nets.
        for (const PadPoint &pad : pads) {
            shorts = shorts
                     || (pad.on(track.side) && pad.net != track.net && pad.at != track.from
                         && pad.at != track.to
                         && piecesApart(a, b, toMm(pad.axisFrom), toMm(pad.axisTo))
                                < half + nmToMm(pad.radius) + clearance);
        }
        if (shorts)
            shorting.insert(track.id);
    }
    return shorting;
}

Board withoutShortingStretches(const Board &before, const Board &after, const Design &design)
{
    // The tracks whose shape changed: not new, nor moved whole.
    QSet<ObjectId> stretched;
    for (const Track &track : after.tracks) {
        const Track *was = findById(before.tracks, track.id);
        if (was != nullptr && track.to - track.from != was->to - was->from)
            stretched.insert(track.id);
    }
    const QList<PadPoint> pads = padPoints(after, design.library);
    QSet<ObjectId> gone;
    for (const ObjectId &id : shortingTracks(after, design, stretched)) {
        if (!gone.contains(id))
            gone.unite(runOf(after, pads, id));
    }
    return withoutTracks(after, gone);
}

namespace {

// The way of a track from 'from' to 'to': straight, or straight and at 45
// degrees (trackCorners()).
QList<Point> trackWay(const Point &from, const Point &to, bool anyAngle)
{
    return anyAngle ? QList<Point>{from, to} : trackCorners(from, to, false);
}

// The pieces of the way 'points', each like 'like', the first with its id
// and the others with new ones.
QList<Track> piecesAlong(const QList<Point> &points, const Track &like)
{
    QList<Track> pieces;
    for (int i = 1; i < points.size(); ++i) {
        if (points.at(i - 1) == points.at(i))
            continue;
        Track piece = like;
        piece.id = pieces.isEmpty() ? like.id : newObjectId();
        piece.from = points.at(i - 1);
        piece.to = points.at(i);
        pieces.append(piece);
    }
    return pieces;
}

// 'board' with the track 'id' in place of 'pieces', those of 'gone' taken
// off, and 'added' put after them; no piece of no length.
Board withPiecesReplaced(const Board &board, const ObjectId &id, const QList<Track> &pieces,
                         const QSet<ObjectId> &gone, const QList<Track> &added)
{
    Board result = board;
    result.tracks.clear();
    for (const Track &track : board.tracks) {
        if (track.id == id)
            result.tracks.append(pieces);
        else if (!gone.contains(track.id))
            result.tracks.append(track);
    }
    result.tracks.append(added);
    result.tracks.removeIf([](const Track &track) { return track.from == track.to; });
    return result;
}

// The pieces of 'board' on 'side' ending at 'at', but for 'id'.
QList<const Track *> piecesAt(const Board &board, BoardSide side, const Point &at,
                              const ObjectId &id)
{
    QList<const Track *> pieces;
    for (const Track &track : board.tracks) {
        if (track.id != id && track.side == side && (track.from == at || track.to == at))
            pieces.append(&track);
    }
    return pieces;
}

bool padOrViaAt(const Board &board, const Library &library, BoardSide side, const Point &at)
{
    const QList<PadPoint> pads = padPoints(board, library);
    return std::any_of(board.vias.cbegin(), board.vias.cend(),
                       [&at](const Via &via) { return via.at == at; })
           || std::any_of(pads.cbegin(), pads.cend(), [&](const PadPoint &pad) {
                  return pad.at == at && pad.on(side);
              });
}

Coord dot(const Point &a, const Point &b)
{
    return a.x * b.x + a.y * b.y;
}

Coord cross(const Point &a, const Point &b)
{
    return a.x * b.y - a.y * b.x;
}

// Where the line through 'a' along 'alongA' meets the one through 'b' along
// 'alongB'; nothing when they run alike.
std::optional<Point> linesMeet(const Point &a, const Point &alongA, const Point &b,
                               const Point &alongB)
{
    const double turn = double(cross(alongA, alongB));
    if (turn == 0)
        return std::nullopt;
    const double part = double(cross(b - a, alongB)) / turn;
    return Point{a.x + Coord(std::llround(part * double(alongA.x))),
                 a.y + Coord(std::llround(part * double(alongA.y)))};
}

// 'board' with no piece twice, and with every two pieces of one track that
// go on straight from each other, alone at their point with no pad and no
// via there, made one.
Board withPiecesMerged(const Board &board, const Library &library)
{
    Board result = board;
    QList<Track> &tracks = result.tracks;
    for (int i = 0; i < tracks.size(); ++i) {
        for (int j = int(tracks.size()) - 1; j > i; --j) {
            const Track &a = tracks.at(i);
            const Track &b = tracks.at(j);
            if (a.side == b.side && a.net == b.net
                && ((a.from == b.from && a.to == b.to) || (a.from == b.to && a.to == b.from)))
                tracks.removeAt(j);
        }
    }
    for (bool merged = true; merged;) {
        merged = false;
        for (int i = 0; i < tracks.size() && !merged; ++i) {
            for (const bool atTo : {false, true}) {
                const Track &track = tracks.at(i);
                const Point end = atTo ? track.to : track.from;
                const QList<const Track *> others = piecesAt(result, track.side, end, track.id);
                if (others.size() != 1 || padOrViaAt(result, library, track.side, end))
                    continue;
                const Track &other = *others.first();
                const Point far = atTo ? track.from : track.to;
                const Point otherFar = other.from == end ? other.to : other.from;
                if (other.width != track.width || other.net != track.net
                    || cross(far - end, otherFar - end) != 0 || dot(far - end, otherFar - end) >= 0)
                    continue;
                const ObjectId otherId = other.id;
                tracks[i].from = far;
                tracks[i].to = otherFar;
                tracks.removeIf([&otherId](const Track &piece) { return piece.id == otherId; });
                merged = true;
                break;
            }
        }
    }
    return result;
}

} // namespace

ObjectId cornerNeighbour(const Board &board, const Library &library, const ObjectId &id,
                         bool atTo)
{
    const Track *track = findById(board.tracks, id);
    if (track == nullptr)
        return ObjectId();
    const Point at = atTo ? track->to : track->from;
    const QList<const Track *> others = piecesAt(board, track->side, at, id);
    if (others.size() != 1 || padOrViaAt(board, library, track->side, at))
        return ObjectId();
    return others.first()->id;
}

Board withTrackCornerMoved(const Board &board, const Library &library, const ObjectId &id,
                           bool atTo, const Point &to, bool anyAngle)
{
    const ObjectId otherId = cornerNeighbour(board, library, id, atTo);
    if (otherId.isNull())
        return board;
    const Track &track = *findById(board.tracks, id);
    const Track &other = *findById(board.tracks, otherId);
    const Point corner = atTo ? track.to : track.from;
    const Point far = atTo ? track.from : track.to;
    const Point otherFar = other.from == corner ? other.to : other.from;
    // From the far end of the one through the corner to that of the other.
    return withPiecesMerged(
        withPiecesReplaced(board, id, piecesAlong(trackWay(far, to, anyAngle), track), {otherId},
                           piecesAlong(trackWay(to, otherFar, anyAngle), other)),
        library);
}

Board withTrackPieceMoved(const Board &board, const Library &library, const ObjectId &id,
                          const Point &delta, Coord gridStep)
{
    const Track *found = findById(board.tracks, id);
    if (found == nullptr)
        return board;
    const Track track = *found;
    // Across the piece: a level or upright one by the other coordinate, a
    // slanted one by whole cells across it, another as it is.
    const Point way = track.to - track.from;
    Point across = delta;
    if (way.x == 0) {
        across = Point{delta.x, 0};
    } else if (way.y == 0) {
        across = Point{0, delta.y};
    } else if (std::llabs(way.x) == std::llabs(way.y)) {
        const bool falling = (way.x > 0) == (way.y > 0);
        const Coord step =
            snapToGrid(falling ? (delta.x - delta.y) / 2 : (delta.x + delta.y) / 2, gridStep);
        across = falling ? Point{step, -step} : Point{step, step};
    }
    if (across == Point())
        return board;
    // Each end: the piece going on from it keeps its way, made longer or
    // shorter to meet the piece moved; when it cannot (it runs along the
    // piece, or would turn back), and at an end on a pad, a via or a fork, a
    // piece straight across joins the old end to the new; a free end moves.
    Track moved = track;
    moved.from = track.from + across;
    moved.to = track.to + across;
    QSet<ObjectId> gone;
    QList<Track> added;
    for (const bool atTo : {false, true}) {
        const Point end = atTo ? track.to : track.from;
        Point &movedEnd = atTo ? moved.to : moved.from;
        const ObjectId otherId = cornerNeighbour(board, library, id, atTo);
        if (!otherId.isNull()) {
            const Track &other = *findById(board.tracks, otherId);
            const Point far = other.from == end ? other.to : other.from;
            const std::optional<Point> meet = linesMeet(far, end - far, movedEnd, way);
            if (meet && dot(*meet - far, end - far) >= 0) {
                Track longer = other;
                (other.from == end ? longer.from : longer.to) = *meet;
                gone.insert(otherId);
                added.append(longer);
                movedEnd = *meet;
                continue;
            }
        } else if (!padOrViaAt(board, library, track.side, end)
                   && piecesAt(board, track.side, end, id).isEmpty()) {
            continue;
        }
        Track joiner = track;
        joiner.id = newObjectId();
        joiner.from = end;
        joiner.to = movedEnd;
        added.append(joiner);
    }
    // A piece ending on it between its ends keeps its way too, made longer
    // or shorter to meet it again; when it cannot (it runs along the piece,
    // would shrink to nothing or turn back, or would miss the piece moved),
    // the piece is not moved.
    for (const Track &stem : board.tracks) {
        if (stem.side != track.side)
            continue;
        for (const bool atTo : {false, true}) {
            const Point end = atTo ? stem.to : stem.from;
            if (!insidePiece(track, end))
                continue;
            const Point far = atTo ? stem.from : stem.to;
            const std::optional<Point> meet = linesMeet(far, end - far, moved.from, way);
            if (!meet || *meet == far || dot(*meet - far, end - far) <= 0
                || (*meet != moved.from && *meet != moved.to && !insidePiece(moved, *meet)))
                return board;
            Track longer = stem;
            (atTo ? longer.to : longer.from) = *meet;
            gone.insert(stem.id);
            added.append(longer);
        }
    }
    return withPiecesMerged(withPiecesReplaced(board, id, {moved}, gone, added), library);
}

namespace {

// How far a piece is moved to spread the tracks, in cells, and how many
// times the tracks are gone over.
const int SPREAD_CELLS = 2;
const int SPREAD_PASSES = 4;
// The least more room, in millimetres, a move is made for.
const double SPREAD_GAIN_MM = 0.01;

// The room of 'track' of 'board': to the nearest copper of another net on
// its side (the pads it ends on are its own), and to the edge 'edge' of the
// board, held by 'outline' (none when empty), in millimetres.
struct Room
{
    double copper = std::numeric_limits<double>::max();
    double edge = std::numeric_limits<double>::max();
};
Room roomOf(const Board &board, const QList<PadPoint> &pads, const QPainterPath &outline,
            const QPolygonF &edge, const Track &track)
{
    Room room;
    const double half = nmToMm(track.width) / 2;
    const QPointF a = toMm(track.from);
    const QPointF b = toMm(track.to);
    for (const Track &other : board.tracks) {
        if (other.side == track.side && other.net != track.net)
            room.copper = std::min(room.copper, piecesApart(a, b, toMm(other.from), toMm(other.to))
                                                    - half - nmToMm(other.width) / 2);
    }
    for (const Via &via : board.vias) {
        if (via.net != track.net)
            room.copper = std::min(room.copper,
                                   QLineF(nearestOnPiece(a, b, toMm(via.at)), toMm(via.at)).length()
                                       - half - nmToMm(via.size) / 2);
    }
    for (const PadPoint &pad : pads) {
        if (pad.on(track.side) && pad.net != track.net && pad.at != track.from
            && pad.at != track.to)
            room.copper = std::min(room.copper,
                                   piecesApart(a, b, toMm(pad.axisFrom), toMm(pad.axisTo)) - half
                                       - nmToMm(pad.radius));
    }
    if (!outline.isEmpty()) {
        if (!outline.contains(a) || !outline.contains(b))
            room.edge = -1;
        for (int i = 0; i + 1 < edge.size(); ++i)
            room.edge = std::min(room.edge, piecesApart(a, b, edge.at(i), edge.at(i + 1)) - half);
    }
    return room;
}

// 'track' touches or crosses a piece of its own net on its side that shares
// no end with it: a move put it onto copper it is not joined to as laid.
bool touchesOwnNet(const Board &board, const Track &track)
{
    const QPointF a = toMm(track.from);
    const QPointF b = toMm(track.to);
    return std::any_of(board.tracks.cbegin(), board.tracks.cend(), [&](const Track &other) {
        return other.id != track.id && other.side == track.side && other.net == track.net
               && other.from != track.from && other.from != track.to && other.to != track.from
               && other.to != track.to
               && piecesApart(a, b, toMm(other.from), toMm(other.to))
                      < nmToMm(track.width + other.width) / 2;
    });
}

} // namespace

Board withTracksSpread(const Board &board, const Design &design, Coord gridStep,
                       const QSet<ObjectId> &ids)
{
    const Library &library = design.library;
    const QList<PadPoint> pads = padPoints(board, library);
    const QPainterPath outline = outlinePath(board.outline);
    const QPolygonF edge = outline.toFillPolygon();
    const double edgeClearance = nmToMm(design.project.rules.edgeClearance);
    // The least room of the pieces of 'after' that are not as in 'before';
    // nothing when one of them comes too near the edge.
    const auto leastRoom = [&](const Board &before, const Board &after) -> std::optional<double> {
        double least = std::numeric_limits<double>::max();
        for (const Track &track : after.tracks) {
            const Track *was = findById(before.tracks, track.id);
            if (was != nullptr && was->from == track.from && was->to == track.to)
                continue;
            const Room room = roomOf(after, pads, outline, edge, track);
            if (room.edge < edgeClearance || touchesOwnNet(after, track))
                return std::nullopt;
            least = std::min({least, room.copper, room.edge});
        }
        return least;
    };
    Board result = board;
    for (int pass = 0; pass < SPREAD_PASSES; ++pass) {
        bool moved = false;
        QList<ObjectId> pieces;
        for (const Track &track : result.tracks) {
            if (ids.isEmpty() || ids.contains(track.id))
                pieces.append(track.id);
        }
        for (const ObjectId &id : std::as_const(pieces)) {
            const Track *track = findById(result.tracks, id);
            if (track == nullptr || cornerNeighbour(result, library, id, false).isNull()
                || cornerNeighbour(result, library, id, true).isNull())
                continue;
            // A cell across it; a piece at another angle stays.
            const Point way = track->to - track->from;
            Point cell;
            if (way.x == 0)
                cell = Point{gridStep, 0};
            else if (way.y == 0)
                cell = Point{0, gridStep};
            else if (std::llabs(way.x) == std::llabs(way.y))
                cell = (way.x > 0) == (way.y > 0) ? Point{gridStep, -gridStep}
                                                  : Point{gridStep, gridStep};
            else
                continue;
            // The room it and the pieces beside it have now, and the best
            // a move gives.
            const QSet<ObjectId> beside{id, cornerNeighbour(result, library, id, false),
                                        cornerNeighbour(result, library, id, true)};
            double best = std::numeric_limits<double>::max();
            for (const Track &piece : result.tracks) {
                if (!beside.contains(piece.id))
                    continue;
                const Room room = roomOf(result, pads, outline, edge, piece);
                best = std::min({best, room.copper, room.edge});
            }
            std::optional<Board> better;
            for (const int sign : {1, -1}) {
                for (int cells = 1; cells <= SPREAD_CELLS; ++cells) {
                    const Coord by = sign * cells;
                    const Board candidate = withTrackPieceMoved(
                        result, library, id, Point{cell.x * by, cell.y * by}, gridStep);
                    QSet<ObjectId> changed;
                    for (const Track &piece : candidate.tracks) {
                        const Track *was = findById(result.tracks, piece.id);
                        if (was == nullptr || was->from != piece.from || was->to != piece.to)
                            changed.insert(piece.id);
                    }
                    if (changed.isEmpty() || !shortingTracks(candidate, design, changed).isEmpty())
                        continue;
                    const std::optional<double> room = leastRoom(result, candidate);
                    if (room && *room > best + SPREAD_GAIN_MM) {
                        best = *room;
                        better = candidate;
                    }
                }
            }
            if (better) {
                result = *better;
                moved = true;
            }
        }
        if (!moved)
            break;
    }
    return result;
}

Board withTracksMoved(const Board &board, const QSet<ObjectId> &ids, const Point &delta)
{
    Board result = board;
    for (Track &track : result.tracks) {
        if (ids.contains(track.id)) {
            track.from = track.from + delta;
            track.to = track.to + delta;
        }
    }
    for (Via &via : result.vias) {
        if (ids.contains(via.id))
            via.at = via.at + delta;
    }
    for (WireLink &link : result.links) {
        if (ids.contains(link.id)) {
            link.from = link.from + delta;
            link.to = link.to + delta;
        }
    }
    return result;
}

Board withTracksByHand(const Board &board, const QSet<ObjectId> &ids)
{
    Board result = board;
    for (Track &track : result.tracks) {
        if (ids.contains(track.id))
            track.autorouted = false;
    }
    for (Via &via : result.vias) {
        if (ids.contains(via.id))
            via.autorouted = false;
    }
    for (WireLink &link : result.links) {
        if (ids.contains(link.id))
            link.autorouted = false;
    }
    return result;
}

QStringList linkDesignators(const Board &board)
{
    QStringList designators;
    for (const WireLink &link : board.links) {
        if (!link.designator.isEmpty())
            designators.append(link.designator);
    }
    return designators;
}

Board withLinkDesignators(const Board &board, const Schematic &schematic, DesignatorStyle style)
{
    Board result = board;
    QStringList taken = linkDesignators(board);
    for (WireLink &link : result.links) {
        if (!link.designator.isEmpty())
            continue;
        link.designator = nextDesignator(schematic, linkPrefix(style), taken);
        taken.append(link.designator);
    }
    return result;
}

Board withLinkDesignatorStyle(const Board &board, const Schematic &schematic,
                              DesignatorStyle from, DesignatorStyle to)
{
    const QString oldPrefix = linkPrefix(from);
    const QString newPrefix = linkPrefix(to);
    if (oldPrefix == newPrefix)
        return board;
    QSet<QString> taken;
    for (const Sheet &sheet : schematic.sheets) {
        for (const Component &component : sheet.components)
            taken.insert(component.designator);
    }
    Board result = board;
    // Those switched, in the order of designatorLess(), so that the first
    // of two that would meet keeps its number.
    QList<WireLink *> switched;
    for (WireLink &link : result.links) {
        const QString &designator = link.designator;
        if (designator.startsWith(oldPrefix) && designator.size() > oldPrefix.size()
            && designator.at(oldPrefix.size()).isDigit())
            switched.append(&link);
        else if (!designator.isEmpty())
            taken.insert(designator);
    }
    std::stable_sort(switched.begin(), switched.end(), [](const WireLink *a, const WireLink *b) {
        return designatorLess(a->designator, b->designator);
    });
    for (WireLink *link : std::as_const(switched)) {
        const QString designator = newPrefix + link->designator.mid(oldPrefix.size());
        // A repeated one waits for a free number.
        if (taken.contains(designator)) {
            link->designator.clear();
        } else {
            link->designator = designator;
            taken.insert(designator);
        }
    }
    return withLinkDesignators(result, schematic, to);
}

Board withTracksWidth(const Board &board, const QSet<ObjectId> &ids, Coord width)
{
    Board result = board;
    for (Track &track : result.tracks) {
        if (ids.contains(track.id))
            track.width = width;
    }
    return result;
}

Board withTracksSide(const Board &board, const QSet<ObjectId> &ids, BoardSide side)
{
    Board result = board;
    for (Track &track : result.tracks) {
        if (ids.contains(track.id))
            track.side = side;
    }
    return result;
}

const NetClass *trackClass(const Design &design, const QString &net)
{
    return TrackClasses(design).of(net);
}

TrackClasses::TrackClasses(const Design &design)
    : m_design(design)
{
    for (const Net &found : nets(design.schematic, design.library, design.project)) {
        // The first of two nets of one name decides, as a search would.
        if (!m_names.contains(found.name))
            m_names.insert(found.name, netClassOf(found, design.project, design.schematic));
    }
}

const NetClass *TrackClasses::of(const QString &net) const
{
    const Project &project = m_design.project;
    const QString name =
        net.isEmpty() ? project.defaultClass : m_names.value(net, project.defaultClass);
    for (const NetClass &netClass : project.netClasses) {
        if (netClass.name == name)
            return &netClass;
    }
    return project.netClasses.isEmpty() ? nullptr : &project.netClasses.first();
}

} // namespace ppcb
