#include "trackedit.h"

#include "netlist.h"
#include "paint.h"
#include "ratsnest.h"

#include <QLineF>
#include <QRectF>

#include <algorithm>
#include <cstdlib>
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
    return inside;
}

namespace {


// How far apart the pieces 'a'-'b' and 'c'-'d' are, in millimetres: 0 when
// they cross.
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

Board withoutTracks(const Board &board, const QSet<ObjectId> &ids)
{
    Board result = board;
    result.tracks.removeIf([&ids](const Track &track) { return ids.contains(track.id); });
    result.vias.removeIf([&ids](const Via &via) { return ids.contains(via.id); });
    return result;
}

Board withoutAnyTracks(const Board &board)
{
    Board result = board;
    result.tracks.clear();
    result.vias.clear();
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

Board withoutShortingStretches(const Board &before, const Board &after, const Design &design)
{
    const QList<PadPoint> pads = padPoints(after, design.library);
    QSet<ObjectId> gone;
    for (const Track &track : after.tracks) {
        const Track *was = findById(before.tracks, track.id);
        if (was == nullptr || gone.contains(track.id) || track.to - track.from == was->to - was->from)
            continue;
        const NetClass *netClass = trackClass(design, track.net);
        const double clearance =
            nmToMm(netClass != nullptr ? netClass->clearance : design.project.rules.clearance);
        const double half = nmToMm(track.width) / 2;
        const QPointF a = toMm(track.from);
        const QPointF b = toMm(track.to);
        // Too near the copper of another net on its side.
        bool shorts = false;
        for (const Track &other : after.tracks) {
            shorts = shorts
                     || (other.side == track.side && other.net != track.net
                         && piecesApart(a, b, toMm(other.from), toMm(other.to))
                                < half + nmToMm(other.width) / 2 + clearance);
        }
        for (const Via &via : after.vias) {
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
            gone.unite(runOf(after, pads, track.id));
    }
    return withoutTracks(after, gone);
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
    return result;
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
    const Project &project = design.project;
    QString name = project.defaultClass;
    if (!net.isEmpty()) {
        for (const Net &found : nets(design.schematic, design.library, project)) {
            if (found.name == net) {
                name = netClassOf(found, project, design.schematic);
                break;
            }
        }
    }
    for (const NetClass &netClass : project.netClasses) {
        if (netClass.name == name)
            return &netClass;
    }
    return project.netClasses.isEmpty() ? nullptr : &project.netClasses.first();
}

} // namespace ppcb
