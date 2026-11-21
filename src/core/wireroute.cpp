// How the program lays a wire: what it keeps off (Obstacles), the ways by
// hand (a few with one or two bends, the least bad taken) and on the grid
// (the cheapest over the nodes of half the pin grid, gridRoute()).

#include "wiringdetail.h"

#include "gridroute.h"
#include "paint.h"

#include <QHash>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numeric>
#include <optional>
#include <tuple>

namespace ppcb {

using namespace wiring;

namespace wiring {

// The obstacles of the components of 'sheet' whose symbol 'library' has,
// for wires laid by 'routing'.
Obstacles obstaclesOf(const Sheet &sheet, const Library &library, WireRouting routing)
{
    Obstacles obstacles;
    obstacles.routing = routing;
    for (const SheetPin &pin : sheetPins(sheet, library))
        obstacles.pins.append(qMakePair(pin.at, pin.bodyEnd));
    for (const Component &component : sheet.components) {
        const Symbol *symbol = library.findSymbolOfPart(component.part);
        if (symbol == nullptr)
            continue;
        const QRectF body = symbolBodyBounds(*symbol);
        if (body.isNull())
            continue;
        // The corners of the body turned and mirrored with the component.
        Coord left = 0, top = 0, right = 0, bottom = 0;
        bool first = true;
        for (const QPointF &corner : {body.topLeft(), body.topRight(), body.bottomLeft(),
                                      body.bottomRight()}) {
            const Point p = componentPoint(component, Point{mmToNm(corner.x()), mmToNm(corner.y())});
            left = first ? p.x : qMin(left, p.x);
            right = first ? p.x : qMax(right, p.x);
            top = first ? p.y : qMin(top, p.y);
            bottom = first ? p.y : qMax(bottom, p.y);
            first = false;
        }
        obstacles.bodies.append(QRectF(QPointF(left, top), QPointF(right, bottom)));
    }
    for (const Wire &wire : sheet.wires)
        obstacles.add(wire);
    for (const Bus &bus : sheet.buses) {
        for (int i = 1; i < bus.points.size(); ++i)
            obstacles.buses.append(qMakePair(bus.points.at(i - 1), bus.points.at(i)));
    }
    return obstacles;
}

} // namespace wiring

namespace {

// True when the segment a-b passes through the inside of 'rect': running
// along its edge or touching it does not count.
bool crossesInside(const Point &a, const Point &b, const QRectF &rect)
{
    // Liang-Barsky: the share of the segment, t0..t1, that is inside.
    double t0 = 0;
    double t1 = 1;
    const double dx = double(b.x - a.x);
    const double dy = double(b.y - a.y);
    // Keeps the part of the segment where p * t < q.
    const auto clip = [&](double p, double q) {
        if (p == 0)
            return q > 0;
        const double t = q / p;
        if (p < 0)
            t0 = qMax(t0, t);
        else
            t1 = qMin(t1, t);
        return t0 < t1;
    };
    return clip(-dx, double(a.x) - rect.left()) && clip(dx, rect.right() - double(a.x))
           && clip(-dy, double(a.y) - rect.top()) && clip(dy, rect.bottom() - double(a.y));
}

// How bad a wire is: the times it passes through the end of a pin it does
// not end on, which would join it to that pin, then the times it goes
// through a component's body, then the times it runs along a pin or another
// wire, then its number of points, then its length. Less is better, in that
// order.
using Badness = std::tuple<int, int, int, int, qint64>;

// True when a wire as bad as 'badness' joins nothing it should not, goes
// through no body and runs along no pin or wire.
bool isClear(const Badness &badness)
{
    return std::get<0>(badness) == 0 && std::get<1>(badness) == 0 && std::get<2>(badness) == 0;
}

Badness badness(const QList<Point> &points, const Obstacles &obstacles)
{
    int joins = 0;
    int bodies = 0;
    int pins = 0;
    qint64 length = 0;
    for (int i = 1; i < points.size(); ++i) {
        const Point &a = points.at(i - 1);
        const Point &b = points.at(i);
        for (const auto &[tip, end] : obstacles.pins) {
            // Once for a corner on it: with the segment that comes into it.
            if (tip != a && tip != points.last() && isOnSegment(a, b, tip))
                ++joins;
        }
        for (const QRectF &body : obstacles.bodies) {
            if (crossesInside(a, b, body))
                ++bodies;
        }
        for (const auto &[tip, end] : obstacles.pins) {
            if (overlap(a, b, tip, end))
                ++pins;
        }
        for (const auto &[owner, segment] : obstacles.wires) {
            if (overlap(a, b, segment.first, segment.second))
                ++pins;
        }
        length += std::llabs(b.x - a.x) + std::llabs(b.y - a.y);
    }
    return std::make_tuple(joins, bodies, pins, int(points.size()), length);
}

// The least bad of 'candidates' (one or more); the earliest of equals.
QList<Point> bestWire(const QList<QList<Point>> &candidates, const Obstacles &obstacles)
{
    int best = 0;
    auto bestBadness = badness(candidates.first(), obstacles);
    for (int i = 1; i < candidates.size(); ++i) {
        const auto b = badness(candidates.at(i), obstacles);
        if (b < bestBadness) {
            best = i;
            bestBadness = b;
        }
    }
    return candidates.at(best);
}

// Half the pin grid, where wires may bend.
constexpr Coord BEND_GRID = PIN_GRID / 2;

// 'value' brought to the nearest point of BEND_GRID.
Coord onHalfGrid(Coord value)
{
    return Coord(std::llround(double(value) / BEND_GRID)) * BEND_GRID;
}

// The ways from 'from' to 'to', each starting with 'from' and straightened:
// with one bend, upright first when 'upright', then the other way; then with
// two bends halfway, upright, level, upright, and level, upright, level.
QList<QList<Point>> routesBetween(const Point &from, const Point &to, bool upright)
{
    QList<QList<Point>> result;
    for (const bool swapped : {upright, !upright})
        result.append(straightened(QList<Point>{from} + wireRoute(from, to, swapped, false)));
    const Coord midY = onHalfGrid((from.y + to.y) / 2);
    result.append(straightened({from, Point{from.x, midY}, Point{to.x, midY}, to}));
    const Coord midX = onHalfGrid((from.x + to.x) / 2);
    result.append(straightened({from, Point{midX, from.y}, Point{midX, to.y}, to}));
    return result;
}

// 'points' with every point of 'joints' that lies inside one of its
// segments made a point of the wire too, so that it can be laid again from
// there.
QList<Point> withJoints(const QList<Point> &points, const QList<Point> &joints)
{
    QList<Point> result;
    for (int i = 0; i < points.size(); ++i) {
        if (i > 0) {
            const Point a = points.at(i - 1);
            const Point b = points.at(i);
            QList<Point> inside;
            for (const Point &joint : joints) {
                if (joint != a && joint != b && isOnSegment(a, b, joint) && !inside.contains(joint))
                    inside.append(joint);
            }
            const auto distance = [&a](const Point &p) {
                return qMax(std::llabs(p.x - a.x), std::llabs(p.y - a.y));
            };
            std::sort(inside.begin(), inside.end(),
                      [&](const Point &p, const Point &q) { return distance(p) < distance(q); });
            result.append(inside);
        }
        result.append(points.at(i));
    }
    return result;
}

// Laying a wire on the grid (WireRouting::Grid): the cheapest way over the
// nodes of BEND_GRID, found by gridRoute().

// What a way costs: a step of the grid, a bend, and a wire crossed. A
// wire laid again from a point of it gains a point for each step it keeps,
// fewer than a crossing costs: of wires as cheap, the one that keeps more.
constexpr int GRID_STEP_COST = 100;

constexpr int GRID_BEND_COST = 400;

constexpr int GRID_CROSSING_COST = 50;

constexpr int GRID_KEPT_GAIN_MAX = GRID_CROSSING_COST - 1;

// A new wire branching off the net costs as much as a bend: of ways as
// cheap, the wire goes on rather than branches.
constexpr int GRID_BRANCH_COST = GRID_BEND_COST;

// How far the grid reaches beyond everything on the sheet, in steps.
constexpr int GRID_MARGIN = 8;

// A larger grid, or a longer search, is given up for the ways by hand.
constexpr qint64 GRID_MAX_NODES = 4000000;

constexpr int GRID_MAX_EXPANDED = 300000;

// 'a' / 'b' rounded down and up, 'b' > 0.
Coord floorDiv(Coord a, Coord b)
{
    return a / b - (a % b < 0 ? 1 : 0);
}

Coord ceilDiv(Coord a, Coord b)
{
    return -floorDiv(-a, b);
}

// True when 'point' is a node of the grid of BEND_GRID through 'origin'.
bool isGridNode(const Point &point, const Point &origin)
{
    return (point.x - origin.x) % BEND_GRID == 0 && (point.y - origin.y) % BEND_GRID == 0;
}

// The way from 'a' to 'b' in steps of one: (1, 0), (-1, 1) and so on.
Point headingOf(const Point &a, const Point &b)
{
    return Point{sign(b.x - a.x), sign(b.y - a.y)};
}

// The nodes of the grid through 'origin' on the segment a-b, from 'a' to
// 'b', ends included; none for a segment not level, upright or at 45
// degrees.
QList<Point> gridNodesOn(const Point &a, const Point &b, const Point &origin)
{
    if (a == b)
        return isGridNode(a, origin) ? QList<Point>{a} : QList<Point>{};
    if (!isStraightOrDiagonal(b - a))
        return {};
    const Point heading = headingOf(a, b);
    const Coord length = qMax(std::llabs(b.x - a.x), std::llabs(b.y - a.y));
    // The first distance along the segment that is on the grid across it.
    const Coord along = heading.x != 0 ? origin.x - a.x : origin.y - a.y;
    const Coord first =
        ((heading.x != 0 ? heading.x : heading.y) * along % BEND_GRID + BEND_GRID) % BEND_GRID;
    QList<Point> nodes;
    for (Coord t = first; t <= length; t += BEND_GRID) {
        const Point node{a.x + heading.x * t, a.y + heading.y * t};
        if (isGridNode(node, origin))
            nodes.append(node);
    }
    return nodes;
}

// Where a wire laid on the grid may start: a point, the points of the wire
// it goes on from that come before it, and the way that wire goes into it
// (zero for none): a first step another way is a bend, and none goes back.
// What it costs is what the wire it keeps costs (keptCost()), so that the
// cheapest wire is laid as a whole.
struct GridStart
{
    Point at;
    QList<Point> before;
    Point heading;
    int cost = 0;
    // A new wire from there, branching off the net, not the wire going on.
    bool branch = false;
};

// What the wire 'points' would cost were it laid on the grid, its level
// and upright length in steps and its bends, less the gain for the steps it
// keeps.
int keptCost(const QList<Point> &points)
{
    const QList<Point> wire = straightened(points);
    qint64 length = 0;
    for (int i = 1; i < wire.size(); ++i)
        length += std::llabs(wire.at(i).x - wire.at(i - 1).x)
                  + std::llabs(wire.at(i).y - wire.at(i - 1).y);
    const qint64 steps = length / BEND_GRID;
    return int(length * GRID_STEP_COST / BEND_GRID
               + qMax<qint64>(0, wire.size() - 2) * GRID_BEND_COST
               - qMin<qint64>(steps, GRID_KEPT_GAIN_MAX));
}

// The cheapest way from one of 'starts' to 'to' over the nodes of BEND_GRID
// through 'to', as the points of the whole wire: the 'before' of the start
// it takes, that start and the corners after it. It goes through no body of
// a part, no pin's line, no point of another wire or a bus and no point of
// 'own' (what it keeps of the wire it goes on from), and along no other wire
// or bus; to cross
// one costs a little. Empty when there is no such way, when no start is on that grid or
// when the grid would be too large: then it is laid by hand. 'branched',
// when given, tells whether the start taken is a branch. The way may end on
// a point of 'alsoTo' instead of 'to', those on the grid through 'to'.
QList<Point> gridLaid(const Obstacles &obstacles, const QList<Point> &own,
                      const QList<GridStart> &starts, const Point &to, bool *branched = nullptr,
                      const QList<Point> &alsoTo = {})
{
    const Coord step = BEND_GRID;
    // The nodes of the grid cover everything with a margin round it.
    Coord lowX = to.x, highX = to.x, lowY = to.y, highY = to.y;
    const auto cover = [&](double x, double y) {
        lowX = qMin(lowX, Coord(std::floor(x)));
        highX = qMax(highX, Coord(std::ceil(x)));
        lowY = qMin(lowY, Coord(std::floor(y)));
        highY = qMax(highY, Coord(std::ceil(y)));
    };
    for (const GridStart &start : starts)
        cover(start.at.x, start.at.y);
    for (const Point &point : own)
        cover(point.x, point.y);
    for (const auto &[tip, end] : obstacles.pins) {
        cover(tip.x, tip.y);
        cover(end.x, end.y);
    }
    for (const QRectF &body : obstacles.bodies) {
        cover(body.left(), body.top());
        cover(body.right(), body.bottom());
    }
    for (const auto &[owner, segment] : obstacles.wires) {
        cover(segment.first.x, segment.first.y);
        cover(segment.second.x, segment.second.y);
    }
    for (const auto &[a, b] : obstacles.buses) {
        cover(a.x, a.y);
        cover(b.x, b.y);
    }
    const Coord left = to.x - (ceilDiv(to.x - lowX, step) + GRID_MARGIN) * step;
    const Coord top = to.y - (ceilDiv(to.y - lowY, step) + GRID_MARGIN) * step;
    const qint64 columns = (to.x - left) / step + ceilDiv(highX - to.x, step) + GRID_MARGIN + 1;
    const qint64 rows = (to.y - top) / step + ceilDiv(highY - to.y, step) + GRID_MARGIN + 1;
    if (columns * rows > GRID_MAX_NODES)
        return {};

    // 'point', a node of the grid, as a node of gridRoute().
    const auto nodeOf = [&](const Point &point) {
        return GridNode{int((point.x - left) / step), int((point.y - top) / step), 0};
    };
    const auto pointOf = [&](const GridNode &node) {
        return Point{left + node.column * step, top + node.row * step};
    };
    const auto indexOf = [&](int column, int row) { return qint64(row) * columns + column; };
    // A step, by the node it starts from on the left or above it, times
    // two, plus one for an upright step.
    const auto stepIndex = [&](const GridNode &a, const GridNode &b) {
        return a.row == b.row ? indexOf(qMin(a.column, b.column), a.row) * 2
                              : indexOf(a.column, qMin(a.row, b.row)) * 2 + 1;
    };

    QSet<qint64> blocked;
    QSet<qint64> blockedSteps;
    QSet<qint64> crossed;
    const auto blockNodesOn = [&](const Point &a, const Point &b, QSet<qint64> &set) {
        for (const Point &node : gridNodesOn(a, b, to)) {
            const GridNode n = nodeOf(node);
            set.insert(indexOf(n.column, n.row));
        }
    };
    // The steps that share some length with the level or upright segment
    // a-b on a line of the grid.
    const auto blockStepsAlong = [&](const Point &a, const Point &b) {
        if (a.y == b.y && a.x != b.x && (a.y - to.y) % step == 0) {
            const int row = int((a.y - top) / step);
            const Coord first = floorDiv(qMin(a.x, b.x) - left, step);
            const Coord last = ceilDiv(qMax(a.x, b.x) - left, step);
            for (Coord column = first; column < last; ++column)
                blockedSteps.insert(indexOf(int(column), row) * 2);
        } else if (a.x == b.x && a.y != b.y && (a.x - to.x) % step == 0) {
            const int column = int((a.x - left) / step);
            const Coord first = floorDiv(qMin(a.y, b.y) - top, step);
            const Coord last = ceilDiv(qMax(a.y, b.y) - top, step);
            for (Coord row = first; row < last; ++row)
                blockedSteps.insert(indexOf(column, int(row)) * 2 + 1);
        }
    };
    for (const auto &[tip, end] : obstacles.pins) {
        blockNodesOn(tip, end, blocked);
        blockStepsAlong(tip, end);
    }
    for (const QRectF &body : obstacles.bodies) {
        const auto inside = [&body](double x, double y) {
            return x > body.left() && x < body.right() && y > body.top() && y < body.bottom();
        };
        const int firstColumn =
            int(qMax<Coord>(0, floorDiv(Coord(std::floor(body.left())) - left, step)));
        const int lastColumn =
            int(qMin<Coord>(columns - 1, ceilDiv(Coord(std::ceil(body.right())) - left, step)));
        const int firstRow =
            int(qMax<Coord>(0, floorDiv(Coord(std::floor(body.top())) - top, step)));
        const int lastRow =
            int(qMin<Coord>(rows - 1, ceilDiv(Coord(std::ceil(body.bottom())) - top, step)));
        for (int row = firstRow; row <= lastRow; ++row) {
            for (int column = firstColumn; column <= lastColumn; ++column) {
                const Point p = pointOf(GridNode{column, row, 0});
                if (inside(p.x, p.y))
                    blocked.insert(indexOf(column, row));
                if (inside(p.x + step / 2.0, p.y))
                    blockedSteps.insert(indexOf(column, row) * 2);
                if (inside(p.x, p.y + step / 2.0))
                    blockedSteps.insert(indexOf(column, row) * 2 + 1);
            }
        }
    }
    for (const auto &[owner, segment] : obstacles.wires) {
        blockNodesOn(segment.first, segment.first, blocked);
        blockNodesOn(segment.second, segment.second, blocked);
        blockNodesOn(segment.first, segment.second, crossed);
        blockStepsAlong(segment.first, segment.second);
    }
    // A bus is crossed like a wire and never run along; a wire may end on it.
    for (const auto &[a, b] : obstacles.buses) {
        blockNodesOn(a, a, blocked);
        blockNodesOn(b, b, blocked);
        blockNodesOn(a, b, crossed);
        blockStepsAlong(a, b);
    }
    QSet<qint64> ownNodes;
    for (int i = 0; i < own.size(); ++i)
        blockNodesOn(own.at(i), own.at(i + 1 < own.size() ? i + 1 : i), ownNodes);

    const GridNode goal = nodeOf(to);
    QList<GridNode> goals = {goal};
    QSet<qint64> goalIndexes = {indexOf(goal.column, goal.row)};
    for (const Point &point : alsoTo) {
        if (!isGridNode(point, to) || point.x < left || point.y < top)
            continue;
        const GridNode node = nodeOf(point);
        if (node.column >= columns || node.row >= rows)
            continue;
        if (!goalIndexes.contains(indexOf(node.column, node.row))) {
            goals.append(node);
            goalIndexes.insert(indexOf(node.column, node.row));
        }
    }
    QHash<qint64, GridStart> startAt;
    QList<GridNode> startNodes;
    for (const GridStart &start : starts) {
        if (!isGridNode(start.at, to))
            continue;
        const GridNode node = nodeOf(start.at);
        const qint64 index = indexOf(node.column, node.row);
        if (!startAt.contains(index)) {
            startAt.insert(index, start);
            startNodes.append(node);
        }
    }
    // The wire would come back onto itself.
    const qint64 goalIndex = indexOf(goal.column, goal.row);
    if (ownNodes.contains(goalIndex) && !startAt.contains(goalIndex))
        return {};

    GridRouteRules rules;
    rules.columns = int(columns);
    rules.rows = int(rows);
    rules.straightCost = GRID_STEP_COST;
    rules.bend90Cost = GRID_BEND_COST;
    rules.maxExpanded = GRID_MAX_EXPANDED;
    rules.startCost = [&](const GridNode &node) {
        return startAt.value(indexOf(node.column, node.row)).cost;
    };
    rules.stepCost = [&](const GridNode &from, const GridNode &next) {
        if (blockedSteps.contains(stepIndex(from, next)))
            return -1;
        const qint64 index = indexOf(next.column, next.row);
        if (!goalIndexes.contains(index) && (blocked.contains(index) || ownNodes.contains(index)))
            return -1;
        int cost = crossed.contains(index) ? GRID_CROSSING_COST : 0;
        const auto start = startAt.constFind(indexOf(from.column, from.row));
        if (start != startAt.constEnd() && start->heading != Point()) {
            const Point heading{next.column - from.column, next.row - from.row};
            if (heading == Point() - start->heading)
                return -1;
            if (heading != start->heading)
                cost += GRID_BEND_COST;
        }
        return cost;
    };
    const QList<GridNode> path = gridRoute(rules, startNodes, goals);
    if (path.isEmpty())
        return {};
    const GridStart &start = startAt[indexOf(path.first().column, path.first().row)];
    if (branched != nullptr)
        *branched = start.branch;
    QList<Point> points = start.before;
    points.append(start.at);
    const QList<GridNode> corners = gridCorners(path);
    for (int i = 1; i < corners.size(); ++i)
        points.append(pointOf(corners.at(i)));
    return straightened(points);
}

// The wires of 'obstacles', from their segments in the order they were
// added, and for each the group of the wires it is joined to, one through
// another: two are joined where an end of one lies on the other or both end
// on the end of one pin.
struct WireGroups
{
    QList<QList<Point>> wires;
    QList<int> group;
};

WireGroups wireGroupsOf(const Obstacles &obstacles)
{
    QList<QList<Point>> wires;
    ObjectId last;
    for (const auto &[owner, segment] : obstacles.wires) {
        if (wires.isEmpty() || owner != last || wires.last().last() != segment.first) {
            wires.append({segment.first});
            last = owner;
        }
        wires.last().append(segment.second);
    }
    QList<int> group(wires.size());
    std::iota(group.begin(), group.end(), 0);
    const auto root = [&group](int i) {
        while (group.at(i) != i)
            i = group.at(i);
        return i;
    };
    const auto join = [&](int a, int b) { group[root(a)] = root(b); };
    QSet<quint64> tips;
    for (const auto &[tip, end] : obstacles.pins)
        tips.insert(key(tip));
    // The first wire found ending on each pin.
    QHash<quint64, int> onTip;
    for (int i = 0; i < wires.size(); ++i) {
        for (const Point &end : {wires.at(i).first(), wires.at(i).last()}) {
            if (!tips.contains(key(end)))
                continue;
            if (onTip.contains(key(end)))
                join(i, onTip.value(key(end)));
            else
                onTip.insert(key(end), i);
        }
    }
    for (int a = 0; a < wires.size(); ++a) {
        for (int b = a + 1; b < wires.size(); ++b) {
            if (wiresJoined(wires.at(a), wires.at(b)))
                join(a, b);
        }
    }
    for (int i = 0; i < wires.size(); ++i)
        group[i] = root(i);
    return WireGroups{wires, group};
}

// The groups of 'groups' (wireGroupsOf()) with a wire joined to the line
// 'points'.
QSet<int> groupsJoinedTo(const WireGroups &groups, const QList<Point> &points)
{
    QSet<int> nets;
    for (int i = 0; i < groups.wires.size(); ++i) {
        if (wiresJoined(points, groups.wires.at(i)))
            nets.insert(groups.group.at(i));
    }
    return nets;
}

// The wires of 'obstacles' joined to the point 'at' and those joined to
// them, one through another (wireGroupsOf()), when they hold together
// without a wire ending at 'at': where a wire that ends there alone could
// be joined instead. None when the wires on 'at' are not joined to each
// other but through that point, or when leaving it would leave the end of
// another wire hanging there.
QList<QList<Point>> netWiresAt(const Point &at, const Obstacles &obstacles)
{
    const WireGroups groups = wireGroupsOf(obstacles);
    const QList<QList<Point>> &wires = groups.wires;
    // What stays joined at 'at' once the wire leaves it: a pin, an end of a
    // wire, two for a wire passing through. One alone would be left hanging
    // there, the end of a wire that went on in the one that leaves.
    const bool onTip = std::any_of(obstacles.pins.cbegin(), obstacles.pins.cend(),
                                   [&at](const auto &pin) { return pin.first == at; });
    int stay = onTip ? 1 : 0;
    for (const QList<Point> &wire : wires) {
        if (wire.first() == at || wire.last() == at)
            stay += (wire.first() == at) + (wire.last() == at);
        else if (isOnWire(wire, at))
            stay += 2;
    }
    if (stay < 2)
        return {};
    int net = -1;
    for (int i = 0; i < wires.size(); ++i) {
        if (!isOnWire(wires.at(i), at))
            continue;
        if (net >= 0 && groups.group.at(i) != net)
            return {};
        net = groups.group.at(i);
    }
    QList<QList<Point>> result;
    for (int i = 0; i < wires.size(); ++i) {
        if (net >= 0 && groups.group.at(i) == net)
            result.append(wires.at(i));
    }
    return result;
}

// The wire of 'obstacles' that 'point' lies on, joined to the wire 'points'
// (an end of one lying on the other) or to a wire joined to it, one through
// another. Empty when there is none.
QList<Point> joinedWireAt(const QList<Point> &points, const Point &point,
                          const Obstacles &obstacles)
{
    const WireGroups groups = wireGroupsOf(obstacles);
    const QSet<int> nets = groupsJoinedTo(groups, points);
    for (int i = 0; i < groups.wires.size(); ++i) {
        if (nets.contains(groups.group.at(i)) && isOnWire(groups.wires.at(i), point))
            return groups.wires.at(i);
    }
    return {};
}

// The index of the last point of 'points' (with its joints, withJoints())
// that is one of 'kept'; 0 when none after the first is: the wire up to
// there is what it keeps.
int lastKeptIndex(const QList<Point> &points, const QList<Point> &kept)
{
    for (int i = points.size() - 1; i > 0; --i) {
        if (kept.contains(points.at(i)))
            return i;
    }
    return 0;
}

// True when a pin come to 'to' lies on 'keptPart', the part a wire keeps up
// to its last joint, or on a wire joined to it, and its line does not run
// along that wire: it is joined there, so the wire ends at its last joint
// and the pin cuts the wire it lies on.
bool isPinJoinedOnKeptPart(const QList<Point> &keptPart, const Point &to,
                           const Obstacles &obstacles)
{
    if (keptPart.size() < 2)
        return false;
    for (const auto &[tip, end] : obstacles.pins) {
        if (tip != to)
            continue;
        const QList<Point> under =
            isOnWire(keptPart, to) ? keptPart : joinedWireAt(keptPart, to, obstacles);
        if (under.isEmpty())
            continue;
        bool along = false;
        for (int i = 1; i < under.size(); ++i)
            along = along || overlap(tip, end, under.at(i - 1), under.at(i));
        if (!along)
            return true;
    }
    return false;
}

// ledTo() on the grid: the cheapest wire that keeps the wire up to the last
// point of 'kept' on it (its first point when 'kept' has none), laid again
// from there or from a point after it, keeping it up to that point; of
// wires as cheap, the one that keeps more. With 'rejoin', a wire held only
// by its first point, which stays, may instead be laid from any point of
// the wires joined to it there (netWiresAt()), leaving that point. A pin
// 'to' on the part it keeps, or on a wire joined to it, is joined there
// instead. With 'branch', a wire with a point of 'kept' on it may instead
// end at the last of them, and a new wire, put into 'branch', go to 'to'
// from any point of what it keeps or of the wires joined to that: when
// going on from its end means a way round. Unless 'exact', the wire may end
// on any point of the wires already reaching 'to', or joined to them,
// instead of at 'to' itself. Empty when gridLaid() finds no way.
QList<Point> gridLedTo(const QList<Point> &wirePoints, const Point &to, const QList<Point> &kept,
                       bool rejoin, const Obstacles &obstacles, QList<Point> *branch,
                       bool exact)
{
    const QList<Point> points = withJoints(wirePoints, kept);
    const int from = lastKeptIndex(points, kept);
    const QList<Point> keptPart = points.mid(0, from + 1);
    if (isPinJoinedOnKeptPart(keptPart, to, obstacles))
        return keptPart;
    // From the far end back: of starts as cheap, the one that keeps more.
    QList<GridStart> starts;
    for (int i = points.size() - 1; i >= from; --i) {
        if (i + 1 < points.size()) {
            const Point heading = headingOf(points.at(i), points.at(i + 1));
            const QList<Point> nodes = gridNodesOn(points.at(i), points.at(i + 1), to);
            for (int j = nodes.size() - 1; j >= 0; --j) {
                if (nodes.at(j) != points.at(i) && nodes.at(j) != points.at(i + 1)) {
                    const QList<Point> before = points.mid(0, i + 1);
                    starts.append(GridStart{nodes.at(j), before, heading,
                                            keptCost(before + QList<Point>{nodes.at(j)})});
                }
            }
        }
        const Point heading = i > 0 ? headingOf(points.at(i - 1), points.at(i)) : Point();
        starts.append(
            GridStart{points.at(i), points.mid(0, i), heading, keptCost(points.mid(0, i + 1))});
    }
    if (rejoin && from == 0) {
        for (const QList<Point> &wire : netWiresAt(points.first(), obstacles)) {
            for (int i = 1; i < wire.size(); ++i) {
                for (const Point &node : gridNodesOn(wire.at(i - 1), wire.at(i), to))
                    starts.append(GridStart{node, {}, Point()});
            }
        }
    }
    // A branch starts on what the wire keeps or on a wire joined to it;
    // the wire itself then costs what it keeps.
    if (branch != nullptr && from > 0) {
        const int branchCost = keptCost(keptPart) + GRID_BRANCH_COST;
        const WireGroups groups = wireGroupsOf(obstacles);
        const QSet<int> nets = groupsJoinedTo(groups, keptPart);
        QList<QList<Point>> lines = {keptPart};
        for (int i = 0; i < groups.wires.size(); ++i) {
            if (nets.contains(groups.group.at(i)))
                lines.append(groups.wires.at(i));
        }
        for (const QList<Point> &line : lines) {
            for (int i = 1; i < line.size(); ++i) {
                for (const Point &node : gridNodesOn(line.at(i - 1), line.at(i), to))
                    starts.append(GridStart{node, {}, Point(), branchCost, true});
            }
        }
    }
    // Only the part every start keeps is in the way: the rest of the old
    // wire goes, and a way back through what a start keeps costs at least
    // three bends more than starting there, so it is never the cheapest.
    // The wires already reaching 'to', and those joined to them: the wire
    // may end on any of their points instead, joining the net there.
    QList<Point> alsoTo;
    if (!exact) {
        const WireGroups groups = wireGroupsOf(obstacles);
        QSet<int> nets;
        for (int i = 0; i < groups.wires.size(); ++i) {
            if (isOnWire(groups.wires.at(i), to))
                nets.insert(groups.group.at(i));
        }
        for (int i = 0; i < groups.wires.size(); ++i) {
            if (!nets.contains(groups.group.at(i)))
                continue;
            const QList<Point> &wire = groups.wires.at(i);
            for (int j = 1; j < wire.size(); ++j)
                alsoTo += gridNodesOn(wire.at(j - 1), wire.at(j), to);
        }
    }
    // Where the wire may go on from a point, it does: those starts come
    // first, and the first start at a point is the one kept.
    bool branched = false;
    const QList<Point> laid = gridLaid(obstacles, keptPart, starts, to, &branched, alsoTo);
    if (branched) {
        *branch = laid;
        return keptPart;
    }
    return laid;
}

} // namespace

namespace wiring {

// The wire 'points' (two or more) with its last point gone to 'to'.
//
// First the way a drag in other editors does it: the last segment slides
// along with the end and the segment before it grows or shrinks, so no bend
// is added; this needs the two last segments to be one level and one
// upright. Next the wire is led on from its last point (routesBetween(),
// going on along the last segment first, so that a straight wire stays
// one). Each of these that keeps every point of 'kept' on the wire is a
// candidate, and the least bad (bestWire()) is taken when it is clear
// (isClear()). Otherwise the last point is given up and the
// wire is led on from the one before, and so on back to the first point:
// a wire is laid again only as far back as needed to keep it clear. The
// points of 'kept' inside its segments count as its points for this. When
// nothing comes clear, the least bad of all; when no candidate keeps
// 'kept', the wire up to its last joint when 'to' lies on it short of its
// other end, else the least
// bad of those that do not. A pin come onto what the wire keeps is joined
// there first (isPinJoinedOnKeptPart()). On the grid, gridLedTo() with
// 'rejoin' and 'exact' when it finds a way.
QList<Point> ledTo(const QList<Point> &wirePoints, const Point &to, const QList<Point> &kept,
                   bool rejoin, const Obstacles &obstacles, QList<Point> *branch,
                   bool exact)
{
    if (obstacles.routing == WireRouting::Grid) {
        const QList<Point> laid =
            gridLedTo(wirePoints, to, kept, rejoin, obstacles, branch, exact);
        if (!laid.isEmpty())
            return laid;
    }
    const QList<Point> points = withJoints(wirePoints, kept);
    // A pin come onto what the wire keeps is joined there, as on the grid.
    const QList<Point> keptPart = points.mid(0, lastKeptIndex(points, kept) + 1);
    if (isPinJoinedOnKeptPart(keptPart, to, obstacles))
        return keptPart;
    const auto keepsAll = [&](const QList<Point> &candidate) {
        return std::all_of(kept.cbegin(), kept.cend(),
                           [&](const Point &point) { return isOnWire(candidate, point); });
    };
    const int n = points.size();
    QList<Point> best;
    Badness bestBadness;
    QList<QList<Point>> unkept;
    for (int dropped = 0; dropped < n; ++dropped) {
        QList<QList<Point>> candidates;
        if (dropped == 0 && n >= 3) {
            const Point a = points.at(n - 3);
            const Point b = points.at(n - 2);
            const Point c = points.at(n - 1);
            const bool level = b.y == c.y && a.x == b.x;
            const bool upright = b.x == c.x && a.y == b.y;
            if (level || upright) {
                QList<Point> slid = points;
                slid[n - 2] = level ? Point{b.x, to.y} : Point{to.x, b.y};
                slid[n - 1] = to;
                slid = straightened(slid);
                (keepsAll(slid) ? candidates : unkept).append(slid);
            }
        }
        const QList<Point> base = points.mid(0, n - dropped);
        const Point last = base.last();
        const bool vertical = base.size() >= 2 && base.at(base.size() - 2).x == last.x;
        for (const QList<Point> &route : routesBetween(last, to, vertical)) {
            const QList<Point> candidate = straightened(base + route.mid(1));
            (keepsAll(candidate) ? candidates : unkept).append(candidate);
        }
        if (candidates.isEmpty())
            continue;
        const QList<Point> wire = bestWire(candidates, obstacles);
        const auto wireBadness = badness(wire, obstacles);
        if (isClear(wireBadness))
            return wire;
        if (best.isEmpty() || wireBadness < bestBadness) {
            best = wire;
            bestBadness = wireBadness;
        }
    }
    if (!best.isEmpty())
        return best;
    // No way there keeps the joints: an end come back onto what the wire
    // keeps, short of its other end, ends at its last joint, the point it
    // came to lying on it. Onto the other end, it is left with no length.
    if (keptPart.size() >= 2 && to != keptPart.first() && isOnWire(keptPart, to))
        return keptPart;
    return bestWire(unkept, obstacles);
}

// A wire from 'from' to 'to', the least bad of routesBetween(); on the
// grid, gridLaid() when it finds a way.
QList<Point> laidBetween(const Point &from, const Point &to, bool upright,
                         const Obstacles &obstacles)
{
    if (obstacles.routing == WireRouting::Grid) {
        const QList<Point> laid = gridLaid(obstacles, {}, {GridStart{from, {}, Point()}}, to);
        if (!laid.isEmpty())
            return laid;
    }
    return bestWire(routesBetween(from, to, upright), obstacles);
}

// A new wire to 'to' from 'from' or from the wires of 'wires' that pass
// through 'from' or end there: from 'from' itself, or from the point of one
// of their segments straight across from 'to', joining the wire there. The
// least bad of all the ways (bestWire()). On the grid, gridLaid() from
// 'from' or any point of those wires, when it finds a way.
QList<Point> laidToJoint(const Point &from, const Point &to, const QList<Wire> &wires,
                         const Obstacles &obstacles)
{
    if (obstacles.routing == WireRouting::Grid) {
        QList<GridStart> starts = {GridStart{from, {}, Point()}};
        for (const Wire &wire : wires) {
            if (wire.points.size() < 2 || !isOnWire(wire, from))
                continue;
            for (int i = 1; i < wire.points.size(); ++i) {
                for (const Point &node : gridNodesOn(wire.points.at(i - 1), wire.points.at(i), to))
                    starts.append(GridStart{node, {}, Point()});
            }
        }
        const QList<Point> laid = gridLaid(obstacles, {}, starts, to);
        if (!laid.isEmpty())
            return laid;
    }
    QList<Point> starts = {from};
    for (const Wire &wire : wires) {
        if (wire.points.size() < 2 || !isOnWire(wire, from))
            continue;
        for (int i = 1; i < wire.points.size(); ++i) {
            const Point &a = wire.points.at(i - 1);
            const Point &b = wire.points.at(i);
            std::optional<Point> across;
            if (a.y == b.y && to.x >= qMin(a.x, b.x) && to.x <= qMax(a.x, b.x))
                across = Point{to.x, a.y};
            else if (a.x == b.x && to.y >= qMin(a.y, b.y) && to.y <= qMax(a.y, b.y))
                across = Point{a.x, to.y};
            if (across && !starts.contains(*across))
                starts.append(*across);
        }
    }
    QList<QList<Point>> candidates;
    for (const Point &start : starts)
        candidates.append(routesBetween(start, to, false));
    return bestWire(candidates, obstacles);
}

// The ends of the wires of 'sheet' other than 'wire' and those in 'skip'
// that lie on 'wire', apart from those at 'except': where other wires are
// joined to it.
QList<Point> jointsOn(const Sheet &sheet, const Wire &wire, const QList<ObjectId> &skip,
                      const QList<Point> &except)
{
    QList<Point> joints;
    for (const Wire &other : sheet.wires) {
        if (other.id == wire.id || skip.contains(other.id) || other.points.size() < 2)
            continue;
        for (const Point &end : {other.points.first(), other.points.last()}) {
            if (!except.contains(end) && isOnWire(wire, end) && !joints.contains(end))
                joints.append(end);
        }
    }
    return joints;
}

// The wire 'points' (two or more) with a way from its last point back to
// 'to' added, the least bad of routesBetween(); on the grid, gridLaid()
// when it finds a way. Nothing is cut away.
QList<Point> ledBack(const QList<Point> &points, const Point &to, const Obstacles &obstacles)
{
    const int n = points.size();
    if (obstacles.routing == WireRouting::Grid) {
        const GridStart start{points.last(), points.mid(0, n - 1),
                              headingOf(points.at(n - 2), points.last())};
        const QList<Point> laid = gridLaid(obstacles, points, {start}, to);
        if (!laid.isEmpty())
            return laid;
    }
    const bool vertical = points.at(n - 2).x == points.at(n - 1).x;
    QList<QList<Point>> candidates;
    for (const QList<Point> &route : routesBetween(points.last(), to, vertical))
        candidates.append(straightened(points + route.mid(1)));
    return bestWire(candidates, obstacles);
}

// Where 'point', lying on wires of 'wires' not among 'skip', gets sliding
// along them towards 'to': along a level or upright segment as far as
// straight across from 'to', along a slanted one onto 'to' alone, and on
// from one wire to the next while that brings it nearer, as far as the
// wires reach. 'point' itself when it lies on none.
Point slidAlongWires(const QList<Wire> &wires, const QSet<ObjectId> &skip, const Point &point,
                     const Point &to)
{
    const auto distance = [&to](const Point &from) {
        return std::llabs(to.x - from.x) + std::llabs(to.y - from.y);
    };
    Point reached = point;
    for (bool further = true; further;) {
        further = false;
        for (const Wire &other : wires) {
            if (skip.contains(other.id))
                continue;
            for (int i = 1; i < other.points.size(); ++i) {
                const Point &s0 = other.points.at(i - 1);
                const Point &s1 = other.points.at(i);
                if (!isOnSegment(s0, s1, reached))
                    continue;
                Point along = reached;
                if (isOnSegment(s0, s1, to))
                    along = to;
                else if (s0.y == s1.y)
                    along.x = qBound(qMin(s0.x, s1.x), to.x, qMax(s0.x, s1.x));
                else if (s0.x == s1.x)
                    along.y = qBound(qMin(s0.y, s1.y), to.y, qMax(s0.y, s1.y));
                if (distance(along) < distance(reached)) {
                    reached = along;
                    further = true;
                }
            }
        }
    }
    return reached;
}

} // namespace wiring

} // namespace ppcb
