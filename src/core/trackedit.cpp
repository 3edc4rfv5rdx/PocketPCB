#include "trackedit.h"

#include "netlist.h"
#include "paint.h"
#include "ratsnest.h"

#include <QLineF>
#include <QRectF>

#include <algorithm>
#include <cstdlib>

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

QSet<ObjectId> trackRun(const Board &board, const ObjectId &id)
{
    QSet<ObjectId> run;
    if (findById(board.tracks, id) == nullptr)
        return run;
    // Where the run may go on: the ends of its tracks, by side; a via
    // carries an end to both sides.
    QList<ObjectId> open = {id};
    run.insert(id);
    while (!open.isEmpty()) {
        const Track &track = *findById(board.tracks, open.takeLast());
        for (const Point &end : {track.from, track.to}) {
            const bool via = std::any_of(board.vias.cbegin(), board.vias.cend(),
                                         [&end](const Via &v) { return v.at == end; });
            for (const Track &other : board.tracks) {
                if (run.contains(other.id) || (other.side != track.side && !via)
                    || (other.from != end && other.to != end))
                    continue;
                run.insert(other.id);
                open.append(other.id);
            }
        }
    }
    // The vias on its ends.
    for (const Via &via : board.vias) {
        for (const Track &track : board.tracks) {
            if (run.contains(track.id) && (track.from == via.at || track.to == via.at))
                run.insert(via.id);
        }
    }
    return run;
}

Board withoutTracks(const Board &board, const QSet<ObjectId> &ids)
{
    Board result = board;
    result.tracks.removeIf([&ids](const Track &track) { return ids.contains(track.id); });
    result.vias.removeIf([&ids](const Via &via) { return ids.contains(via.id); });
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
