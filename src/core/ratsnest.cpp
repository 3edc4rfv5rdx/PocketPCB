#include "ratsnest.h"

#include "wiring.h"

#include <QHash>

#include <limits>

namespace ppcb {

Point placedPadPoint(const Placement &placement, const Point &padAt)
{
    return placedPoint(placement.at, placement.rotation, placement.side == BoardSide::Bottom,
                       padAt);
}

QList<QPair<Point, Point>> ratsnest(const Board &board, const Library &library)
{
    // The centres of the pads of every net, in the order of the placements.
    QHash<QString, QList<Point>> padsOfNet;
    QStringList netOrder;
    for (const Placement &placement : board.placements) {
        const Footprint *footprint = library.findFootprint(placement.footprint);
        if (footprint == nullptr)
            continue;
        for (const Pad &pad : footprint->pads) {
            const auto net = placement.padNets.constFind(pad.number);
            if (pad.number.isEmpty() || net == placement.padNets.constEnd() || net->isEmpty())
                continue;
            if (!padsOfNet.contains(*net))
                netOrder.append(*net);
            padsOfNet[*net].append(placedPadPoint(placement, pad.at));
        }
    }
    const auto distance = [](const Point &a, const Point &b) {
        const double dx = double(a.x - b.x);
        const double dy = double(a.y - b.y);
        return dx * dx + dy * dy;
    };
    QList<QPair<Point, Point>> lines;
    for (const QString &net : std::as_const(netOrder)) {
        const QList<Point> &pads = padsOfNet.value(net);
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
            nearest[i] = distance(pads.at(0), pads.at(i));
            from[i] = 0;
        }
        for (int step = 1; step < count; ++step) {
            int next = -1;
            for (int i = 0; i < count; ++i) {
                if (!inTree.at(i) && (next < 0 || nearest.at(i) < nearest.at(next)))
                    next = i;
            }
            inTree[next] = true;
            // Pads on one point need no line.
            if (pads.at(next) != pads.at(from.at(next)))
                lines.append(qMakePair(pads.at(from.at(next)), pads.at(next)));
            for (int i = 0; i < count; ++i) {
                const double d = distance(pads.at(next), pads.at(i));
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
