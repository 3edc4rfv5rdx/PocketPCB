#include "ratsnest.h"

#include "paint.h"
#include "trackedit.h"
#include "wiring.h"

#include <QHash>
#include <QLineF>

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace ppcb {

Point placedPadPoint(const Placement &placement, const Point &padAt)
{
    return placedPoint(placement.at, placement.rotation, placement.side == BoardSide::Bottom,
                       padAt);
}

bool operator==(const RatsnestLine &a, const RatsnestLine &b)
{
    return a.from == b.from && a.to == b.to && a.net == b.net;
}

QList<NetPad> netPads(const Board &board, const Library &library)
{
    QList<NetPad> pads;
    for (const Placement &placement : board.placements) {
        const Footprint *footprint = library.findFootprint(placement.footprint);
        if (footprint == nullptr)
            continue;
        for (const Pad &pad : footprint->pads) {
            const auto net = placement.padNets.constFind(pad.number);
            if (pad.number.isEmpty() || net == placement.padNets.constEnd() || net->isEmpty())
                continue;
            const CopperSides sides = padCopperSides(pad.layers, placement.side);
            pads.append(NetPad{placedPadPoint(placement, pad.at), pad.sizeX, pad.sizeY, *net,
                               copperOn(sides, BoardSide::Top),
                               copperOn(sides, BoardSide::Bottom)});
        }
    }
    for (const MountingHole &hole : board.holes) {
        if (hole.ring > 0 && !hole.net.isEmpty())
            pads.append(NetPad{hole.at, holeOuterDiameter(hole), holeOuterDiameter(hole),
                               hole.net, holePadOn(board, hole, BoardSide::Top),
                               holePadOn(board, hole, BoardSide::Bottom)});
    }
    return pads;
}

QList<int> copperGroups(const Board &board, const QList<NetPad> &pads)
{
    // Union-find over the pads, then the tracks, then the vias.
    const int padCount = int(pads.size());
    const int trackCount = int(board.tracks.size());
    QList<int> parent(padCount + trackCount + int(board.vias.size()));
    for (int i = 0; i < parent.size(); ++i)
        parent[i] = i;
    const auto find = [&parent](int i) {
        while (parent.at(i) != i) {
            parent[i] = parent.at(parent.at(i));
            i = parent.at(i);
        }
        return i;
    };
    const auto join = [&parent, &find](int a, int b) { parent[find(a)] = find(b); };
    const auto near = [](const Point &a, const Point &b, Coord reach) {
        return QLineF(toMm(a), toMm(b)).length() <= nmToMm(reach);
    };
    const auto onPad = [](const NetPad &pad, const Point &at) {
        const Coord half = std::max(pad.sizeX, pad.sizeY) / 2;
        return std::abs(at.x - pad.at.x) <= half && std::abs(at.y - pad.at.y) <= half;
    };
    // The distance from 'at' to the piece of 'track'.
    const auto toTrack = [](const Track &track, const Point &at) {
        return QLineF(nearestOnPiece(toMm(track.from), toMm(track.to), toMm(at)), toMm(at))
            .length();
    };
    for (int t = 0; t < trackCount; ++t) {
        const Track &track = board.tracks.at(t);
        for (const Point &end : {track.from, track.to}) {
            for (int p = 0; p < padCount; ++p) {
                const NetPad &pad = pads.at(p);
                if ((track.side == BoardSide::Top ? pad.top : pad.bottom) && onPad(pad, end))
                    join(padCount + t, p);
            }
            for (int v = 0; v < board.vias.size(); ++v) {
                if (near(end, board.vias.at(v).at, board.vias.at(v).size / 2))
                    join(padCount + t, padCount + trackCount + v);
            }
            for (int o = 0; o < trackCount; ++o) {
                const Track &other = board.tracks.at(o);
                if (o != t && other.side == track.side
                    && toTrack(other, end) <= nmToMm(other.width) / 2)
                    join(padCount + t, padCount + o);
            }
        }
    }
    // A via on a pad joins it.
    for (int v = 0; v < board.vias.size(); ++v) {
        for (int p = 0; p < padCount; ++p) {
            if (onPad(pads.at(p), board.vias.at(v).at))
                join(padCount + trackCount + v, p);
        }
    }
    QList<int> groups;
    for (int p = 0; p < padCount; ++p)
        groups.append(find(p));
    return groups;
}

RoutingProgress routingProgress(const Board &board, const Library &library)
{
    QHash<QString, int> padsOfNet;
    for (const NetPad &pad : netPads(board, library))
        ++padsOfNet[pad.net];
    RoutingProgress progress;
    for (const int count : std::as_const(padsOfNet))
        progress.total += count - 1;
    progress.unrouted = int(ratsnest(board, library).size());
    return progress;
}

QList<RatsnestLine> ratsnest(const Board &board, const Library &library)
{
    // The centres of the pads of every net, in the order of the placements,
    // and the group of copper of each.
    const QList<NetPad> all = netPads(board, library);
    const QList<int> groups = copperGroups(board, all);
    QHash<QString, QList<Point>> padsOfNet;
    QHash<QString, QList<int>> groupsOfNet;
    QStringList netOrder;
    for (int i = 0; i < all.size(); ++i) {
        const NetPad &pad = all.at(i);
        if (!padsOfNet.contains(pad.net))
            netOrder.append(pad.net);
        padsOfNet[pad.net].append(pad.at);
        groupsOfNet[pad.net].append(groups.at(i));
    }
    const auto distance = [](const Point &a, const Point &b) {
        const double dx = double(a.x - b.x);
        const double dy = double(a.y - b.y);
        return dx * dx + dy * dy;
    };
    QList<RatsnestLine> lines;
    for (const QString &net : std::as_const(netOrder)) {
        const QList<Point> &pads = padsOfNet.value(net);
        const QList<int> &group = groupsOfNet.value(net);
        // Pads the copper joins are as near as they can be.
        const auto cost = [&pads, &group, &distance](int a, int b) {
            return group.at(a) == group.at(b) ? 0.0 : distance(pads.at(a), pads.at(b));
        };
        const int count = int(pads.size());
        if (count < 2)
            continue;
        // Prim: the tree grows from the first pad, each time by the pad
        // nearest to it.
        QList<bool> inTree(count, false);
        QList<double> nearest(count, std::numeric_limits<double>::max());
        QList<int> from(count, 0);
        inTree[0] = true;
        for (int i = 1; i < count; ++i) {
            nearest[i] = cost(0, i);
            from[i] = 0;
        }
        for (int step = 1; step < count; ++step) {
            int next = -1;
            for (int i = 0; i < count; ++i) {
                if (!inTree.at(i) && (next < 0 || nearest.at(i) < nearest.at(next)))
                    next = i;
            }
            inTree[next] = true;
            // Pads on one point, or joined by copper, need no line.
            if (pads.at(next) != pads.at(from.at(next)) && group.at(next) != group.at(from.at(next)))
                lines.append(RatsnestLine{pads.at(from.at(next)), pads.at(next), net});
            for (int i = 0; i < count; ++i) {
                const double d = cost(next, i);
                if (!inTree.at(i) && d < nearest.at(i)) {
                    nearest[i] = d;
                    from[i] = next;
                }
            }
        }
    }
    return lines;
}

} // namespace ppcb
