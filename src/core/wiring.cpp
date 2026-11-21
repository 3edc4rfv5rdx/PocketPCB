#include "wiring.h"

#include "gridroute.h"
#include "paint.h"

#include <QHash>

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <functional>
#include <numeric>
#include <optional>
#include <tuple>

namespace ppcb {

namespace {

Coord sign(Coord value)
{
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

// The z of the cross product of the two offsets: zero when they are parallel.
qint64 cross(const Point &a, const Point &b)
{
    return qint64(a.x) * b.y - qint64(a.y) * b.x;
}

qint64 dot(const Point &a, const Point &b)
{
    return qint64(a.x) * b.x + qint64(a.y) * b.y;
}

bool isOnSegment(const Point &a, const Point &b, const Point &point)
{
    if (cross(b - a, point - a) != 0)
        return false;
    return point.x >= qMin(a.x, b.x) && point.x <= qMax(a.x, b.x) && point.y >= qMin(a.y, b.y)
           && point.y <= qMax(a.y, b.y);
}

// A point as a key of a hash.
quint64 key(const Point &point)
{
    return (quint64(quint32(point.x)) << 32) | quint32(point.y);
}

// The point 'p' of segment a-b carried to segment 'a2'-'b2', the same share
// of the way along it. Exact for segments of the same length, which is what
// a move, a turn or a mirror leaves.
Point carriedAlong(const Point &a, const Point &b, const Point &a2, const Point &b2,
                   const Point &p)
{
    const qint64 length = qMax(std::llabs(b.x - a.x), std::llabs(b.y - a.y));
    if (length == 0)
        return a2;
    const qint64 along = qMax(std::llabs(p.x - a.x), std::llabs(p.y - a.y));
    return Point{a2.x + Coord((b2.x - a2.x) * along / length),
                 a2.y + Coord((b2.y - a2.y) * along / length)};
}

// 'points' without repeated points and without corners where the wire goes
// on straight ahead or turns straight back over itself.
QList<Point> straightened(const QList<Point> &points)
{
    QList<Point> result;
    for (const Point &point : points) {
        while (result.size() >= 2
               && cross(result.last() - result.at(result.size() - 2), point - result.last()) == 0)
            result.removeLast();
        if (!result.isEmpty() && result.last() == point)
            continue;
        result.append(point);
    }
    return result;
}

// What a wire laid by the program keeps off: the bodies of the components
// (nanometres) and, if it can, the lines of their pins.
struct Obstacles
{
    QList<QPair<Point, Point>> pins;
    QList<QRectF> bodies;
    // The segments of the wires, with the wire they belong to.
    QList<QPair<ObjectId, QPair<Point, Point>>> wires;
    // The segments of the buses, which only a wire laid on the grid keeps
    // off.
    QList<QPair<Point, Point>> buses;
    // How a wire is laid among them.
    WireRouting routing = WireRouting::Hand;

    // The same without the segments of the wire 'id': what that wire keeps
    // off when it is laid again.
    Obstacles without(const ObjectId &id) const
    {
        Obstacles result = *this;
        result.wires.removeIf([&id](const auto &segment) { return segment.first == id; });
        return result;
    }

    // Adds the segments of 'wire'.
    void add(const Wire &wire)
    {
        for (int i = 1; i < wire.points.size(); ++i)
            wires.append(qMakePair(wire.id, qMakePair(wire.points.at(i - 1), wire.points.at(i))));
    }
};

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

// True when the segments a-b and c-d lie on one level or upright line and
// share a stretch of some length: touching at a point is not enough.
bool overlap(const Point &a, const Point &b, const Point &c, const Point &d)
{
    const auto shared = [](Coord a1, Coord a2, Coord b1, Coord b2) {
        return qMin(qMax(a1, a2), qMax(b1, b2)) - qMax(qMin(a1, a2), qMin(b1, b2)) > 0;
    };
    if (a.y == b.y && c.y == d.y && a.y == c.y)
        return shared(a.x, b.x, c.x, d.x);
    if (a.x == b.x && c.x == d.x && a.x == c.x)
        return shared(a.y, b.y, c.y, d.y);
    return false;
}

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
                   bool rejoin, const Obstacles &obstacles, QList<Point> *branch = nullptr,
                   bool exact = false)
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

// True when 'wire' passes through 'point' without ending there.
bool passesThrough(const Wire &wire, const Point &point)
{
    const QList<Point> &p = wire.points;
    return p.size() >= 2 && p.first() != point && p.last() != point && isOnWire(wire, point);
}

// True when one of 'points' lies on 'wire' other than at its ends: what
// holds the wire there, so that it cannot be shifted whole.
bool passesThroughAny(const Wire &wire, const QList<Point> &points)
{
    return std::any_of(points.cbegin(), points.cend(),
                       [&wire](const Point &point) { return passesThrough(wire, point); });
}

// The wire 'index' of 'wires', which passes through 'point', cut in two at
// it: the first part keeps the wire's id, the second, put after it, gets
// 'restId'. Both then end at 'point', and so are joined to a pin there.
void splitAt(QList<Wire> &wires, int index, const Point &point, const ObjectId &restId)
{
    const Wire whole = wires.at(index);
    const QList<Point> &p = whole.points;
    QList<Point> before;
    QList<Point> after = {point};
    bool passed = false;
    for (int j = 0; j < p.size(); ++j) {
        if (!passed) {
            before.append(p.at(j));
            if (j + 1 < p.size() && isOnSegment(p.at(j), p.at(j + 1), point)) {
                if (before.last() != point)
                    before.append(point);
                passed = true;
            }
        } else if (p.at(j) != after.last()) {
            after.append(p.at(j));
        }
    }
    wires[index].points = before;
    Wire rest = whole;
    rest.id = restId;
    rest.points = after;
    wires.insert(index + 1, rest);
}

// 'wires' with the first wire that passes through 'point' without ending
// there cut in two at it (splitAt()), the second part with a new id.
void cutAt(QList<Wire> &wires, const Point &point)
{
    for (int i = 0; i < wires.size(); ++i) {
        if (passesThrough(wires.at(i), point)) {
            splitAt(wires, i, point, newObjectId());
            return;
        }
    }
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

// Where a point of 'before' goes when the objects 'moved' become what they
// are in 'after': nothing when it lies on none of them.
class PointFollower
{
public:
    // A pin of a moved component: where its end was and where it went.
    struct MovedPin
    {
        Point from;
        Point to;
    };

    PointFollower(const Sheet &before, const Sheet &after, const QSet<ObjectId> &moved,
                  const Library &library)
    {
        // Where the pins of each moved owner are now, in the order of sheetPins().
        QHash<ObjectId, QList<Point>> pinsNow;
        for (const SheetPin &pin : sheetPins(after, library)) {
            if (moved.contains(pin.owner))
                pinsNow[pin.owner].append(pin.at);
        }
        QHash<ObjectId, int> taken;
        for (const SheetPin &pin : sheetPins(before, library)) {
            const QList<Point> now = pinsNow.value(pin.owner);
            const int index = taken[pin.owner]++;
            if (index < now.size())
                m_pins.append({pin.at, now.at(index)});
            else
                m_stillPins.append(pin.at);
        }
        QHash<ObjectId, const Wire *> movedWires;
        for (const Wire &wire : after.wires) {
            if (moved.contains(wire.id))
                movedWires.insert(wire.id, &wire);
        }
        for (const Wire &wire : before.wires) {
            const Wire *now = movedWires.value(wire.id);
            if (now != nullptr && now->points.size() == wire.points.size())
                m_wires.append(qMakePair(&wire, now));
        }
        // A wire left alone whose two ends go the same way goes whole, and
        // so does what lies on it: found one through another. A pin left in
        // place on its middle holds it.
        for (bool found = true; found;) {
            found = false;
            for (const Wire &wire : before.wires) {
                if (moved.contains(wire.id) || wire.points.size() < 2 || isShifted(wire)
                    || passesThroughAny(wire, m_stillPins))
                    continue;
                const std::optional<Point> first = follow(wire.points.first());
                const std::optional<Point> last = follow(wire.points.last());
                if (first && last && *first - wire.points.first() == *last - wire.points.last()) {
                    m_shifted.append(qMakePair(&wire, *first - wire.points.first()));
                    found = true;
                }
            }
        }
    }

    // True when the end of a pin of a component left in place is at 'point'.
    bool isStillPin(const Point &point) const { return m_stillPins.contains(point); }

    // The ends of the pins of the components left in place.
    const QList<Point> &stillPins() const { return m_stillPins; }

    // Where 'point' goes; nothing when it lies on nothing moved, or when
    // what it lies on leaves it where it is: an end that does not move is
    // not led again.
    std::optional<Point> follow(const Point &point) const
    {
        const std::optional<Point> to = goesTo(point);
        if (to && *to == point)
            return std::nullopt;
        return to;
    }

    // True when the end of a pin of a moved component was at 'point'.
    bool isMovedPin(const Point &point) const
    {
        return std::any_of(m_pins.cbegin(), m_pins.cend(),
                           [&point](const MovedPin &pin) { return pin.from == point; });
    }

    // Where 'point' goes with a wire left alone that goes whole; nothing
    // when it lies on none.
    std::optional<Point> shiftedWith(const Point &point) const
    {
        for (const auto &[wire, shift] : m_shifted) {
            if (isOnWire(*wire, point))
                return point + shift;
        }
        return std::nullopt;
    }

    const QList<MovedPin> &movedPins() const { return m_pins; }

private:
    QList<MovedPin> m_pins;
    // The pins of the components that stay: an end on one of them stays too.
    QList<Point> m_stillPins;
    QList<QPair<const Wire *, const Wire *>> m_wires;
    // The wires left alone that go whole, with the way they go.
    QList<QPair<const Wire *, Point>> m_shifted;

    // Where 'point' goes with what it lies on, even when that is where it is.
    std::optional<Point> goesTo(const Point &point) const
    {
        if (isStillPin(point))
            return std::nullopt;
        for (const MovedPin &pin : m_pins) {
            if (pin.from == point)
                return pin.to;
        }
        for (const auto &[was, now] : m_wires) {
            const QList<Point> &a = was->points;
            const QList<Point> &b = now->points;
            if (a.size() == 1 && a.first() == point)
                return b.first();
            for (int i = 1; i < a.size(); ++i) {
                if (isOnSegment(a.at(i - 1), a.at(i), point))
                    return carriedAlong(a.at(i - 1), a.at(i), b.at(i - 1), b.at(i), point);
            }
        }
        return shiftedWith(point);
    }

    bool isShifted(const Wire &wire) const
    {
        return std::any_of(m_shifted.cbegin(), m_shifted.cend(),
                           [&wire](const auto &other) { return other.first == &wire; });
    }
};

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

} // namespace

Point placedPoint(const Point &at, int rotation, bool mirrored, const Point &point)
{
    Point p = point;
    if (mirrored)
        p.x = -p.x;
    // A quarter turn clockwise on screen, where Y grows downwards.
    for (int turn = 0; turn < rotation / 90; ++turn)
        p = Point{-p.y, p.x};
    return at + p;
}

Point componentPoint(const Component &component, const Point &point)
{
    return placedPoint(component.at, component.rotation, component.mirrored, point);
}

QList<SheetPin> sheetPins(const Sheet &sheet, const Library &library)
{
    QList<SheetPin> pins;
    for (const Component &component : sheet.components) {
        const Symbol *symbol = library.findSymbolOfPart(component.part);
        if (symbol == nullptr)
            continue;
        for (const Pin &pin : symbol->pins) {
            pins.append(SheetPin{component.id, componentPoint(component, pin.at),
                                 componentPoint(component, pinBodyEnd(pin)), pin.number});
        }
    }
    for (const PowerSymbol &power : sheet.powerSymbols) {
        // Its one pin is at the symbol's origin; without the symbol there
        // is no line to keep off.
        const Symbol *symbol = library.findSymbol(power.symbol);
        const Point end = symbol == nullptr || symbol->pins.isEmpty()
                              ? Point()
                              : pinBodyEnd(symbol->pins.first());
        pins.append(SheetPin{power.id, power.at, placedPoint(power.at, power.rotation, false, end),
                             QString()});
    }
    return pins;
}

QList<Point> pinPoints(const Sheet &sheet, const Library &library)
{
    QList<Point> points;
    for (const SheetPin &pin : sheetPins(sheet, library))
        points.append(pin.at);
    return points;
}

bool isOnBus(const Sheet &sheet, const Point &point)
{
    return std::any_of(sheet.buses.cbegin(), sheet.buses.cend(),
                       [&point](const Bus &bus) { return isOnWire(bus.points, point); });
}

QList<Point> withBusEntry(const QList<Point> &points, const Sheet &sheet, Coord step)
{
    const int n = int(points.size());
    if (n < 2 || step <= 0)
        return points;
    const Point end = points.at(n - 1);
    const Point from = points.at(n - 2);
    const Point way = end - from;
    // Only a wire that comes square on, from far enough.
    const bool upright = way.x == 0 && std::llabs(way.y) >= step;
    const bool level = way.y == 0 && std::llabs(way.x) >= step;
    if (!upright && !level)
        return points;
    for (const Bus &bus : sheet.buses) {
        for (int i = 1; i < bus.points.size(); ++i) {
            const Point &a = bus.points.at(i - 1);
            const Point &b = bus.points.at(i);
            if (!isOnSegment(a, b, end))
                continue;
            const Point back{sign(way.x) * step, sign(way.y) * step};
            // The wire goes on the line of the bus into its end: its last
            // cell steps aside, down from a level bus, right of an upright
            // one, and comes back at 45 degrees into the end.
            const bool lastOfBus = end == bus.points.first() || end == bus.points.last();
            const bool inLine = upright ? a.x == b.x : a.y == b.y;
            if (inLine) {
                // Along the bus, or into its corner, where the next segment
                // may run across.
                if (!lastOfBus || isOnSegment(a, b, from))
                    continue;
                const Point aside = upright ? Point{step, 0} : Point{0, step};
                QList<Point> result = points.mid(0, n - 1);
                result.append(from + aside);
                result.append(end - back + aside);
                result.append(end);
                // Stepping aside back over a wire that came that way shortens it.
                return straightened(result);
            }
            // The bus runs across the wire here.
            const Point along = upright ? Point{step, 0} : Point{0, step};
            Point target = end + along;
            if (!isOnSegment(a, b, target))
                target = end - along;
            if (!isOnSegment(a, b, target))
                return points;
            QList<Point> result = points.mid(0, n - 1);
            result.append(end - back);
            result.append(target);
            return simplifiedWire(result);
        }
    }
    return points;
}

QList<Point> withBusEntries(const QList<Point> &points, const Sheet &sheet,
                            const Library &library, Coord step)
{
    const auto enters = [&](const Point &end) {
        return isOnBus(sheet, end) && !isConnectionPoint(sheet, library, end);
    };
    QList<Point> result = points;
    if (result.size() < 2)
        return result;
    if (enters(result.last()))
        result = withBusEntry(result, sheet, step);
    if (enters(result.first())) {
        std::reverse(result.begin(), result.end());
        result = withBusEntry(result, sheet, step);
        std::reverse(result.begin(), result.end());
    }
    return result;
}

QList<Point> partPinPoints(const Sheet &sheet, const Library &library)
{
    QList<Point> points;
    for (const SheetPin &pin : sheetPins(sheet, library)) {
        const bool ofPart =
            std::any_of(sheet.components.cbegin(), sheet.components.cend(),
                        [&pin](const Component &component) { return component.id == pin.owner; });
        if (ofPart)
            points.append(pin.at);
    }
    return points;
}

bool isOnWire(const QList<Point> &points, const Point &point)
{
    if (points.size() == 1)
        return points.first() == point;
    for (int i = 1; i < points.size(); ++i) {
        if (isOnSegment(points.at(i - 1), points.at(i), point))
            return true;
    }
    return false;
}

bool isOnWire(const Wire &wire, const Point &point)
{
    return isOnWire(wire.points, point);
}

bool wiresJoined(const QList<Point> &a, const QList<Point> &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    return isOnWire(b, a.first()) || isOnWire(b, a.last()) || isOnWire(a, b.first())
           || isOnWire(a, b.last());
}

bool isConnectionPoint(const Sheet &sheet, const Library &library, const Point &point)
{
    if (pinPoints(sheet, library).contains(point))
        return true;
    for (const Wire &wire : sheet.wires) {
        if (isOnWire(wire, point))
            return true;
    }
    return false;
}

QSet<ObjectId> joinedWires(const Sheet &sheet, const ObjectId &wire)
{
    QSet<ObjectId> found;
    QList<const Wire *> waiting;
    for (const Wire &candidate : sheet.wires) {
        if (candidate.id == wire) {
            found.insert(candidate.id);
            waiting.append(&candidate);
        }
    }
    while (!waiting.isEmpty()) {
        const Wire *current = waiting.takeLast();
        for (const Wire &other : sheet.wires) {
            if (!found.contains(other.id) && wiresJoined(current->points, other.points)) {
                found.insert(other.id);
                waiting.append(&other);
            }
        }
    }
    return found;
}

Sheet withWiresMerged(const Sheet &sheet)
{
    Sheet result = sheet;
    QList<Wire> &wires = result.wires;
    // Whether wire 'i' may be joined at its end 'at' (its first point when
    // not 'last') to another one: the index of that one, or -1.
    const auto partnerAt = [&](int i, const Point &at) {
        if (isOnBus(result, at))
            return -1;
        int partner = -1;
        for (int j = 0; j < wires.size(); ++j) {
            if (j == i || wires.at(j).points.isEmpty() || !isOnWire(wires.at(j), at))
                continue;
            const QList<Point> &points = wires.at(j).points;
            // One end of it there, not the other, nor a pass through.
            if (partner >= 0 || (points.first() == at) == (points.last() == at))
                return -1;
            partner = j;
        }
        return partner;
    };
    bool merged = true;
    while (merged) {
        merged = false;
        for (int i = 0; i < wires.size() && !merged; ++i) {
            const QList<Point> &points = wires.at(i).points;
            if (points.size() < 2 || points.first() == points.last())
                continue;
            for (const bool last : {true, false}) {
                const Point at = last ? points.last() : points.first();
                const int j = partnerAt(i, at);
                if (j < 0)
                    continue;
                // 'i' leads into the point, 'j' leads on from it.
                QList<Point> into = wires.at(i).points;
                if (!last)
                    std::reverse(into.begin(), into.end());
                QList<Point> on = wires.at(j).points;
                if (on.first() != at)
                    std::reverse(on.begin(), on.end());
                // One going back along the other stays apart: one wire would
                // run back over itself.
                const Point in = at - into.at(into.size() - 2);
                const Point out = on.at(1) - at;
                if (cross(in, out) == 0 && dot(in, out) < 0)
                    continue;
                const int kept = qMin(i, j);
                wires[kept].points = simplifiedWire(into + on.mid(1));
                wires.removeAt(qMax(i, j));
                merged = true;
                break;
            }
        }
    }
    return result;
}

namespace {

// 'sheet' with its buses as its wires, and nothing else: buses join each
// other as wires do.
Sheet busLines(const Sheet &sheet)
{
    Sheet lines;
    for (const Bus &bus : sheet.buses) {
        Wire line;
        line.id = bus.id;
        line.points = bus.points;
        lines.wires.append(line);
    }
    return lines;
}

} // namespace

QSet<ObjectId> joinedBuses(const Sheet &sheet, const ObjectId &bus)
{
    return joinedWires(busLines(sheet), bus);
}

QList<Point> busJunctions(const Sheet &sheet)
{
    return junctions(busLines(sheet), Library());
}

QList<Point> junctions(const Sheet &sheet, const Library &library)
{
    // The points that may be junctions: wire ends and pins, each counted
    // once for every end and pin on it.
    QHash<quint64, int> counts;
    QList<Point> candidates;
    const auto addEnd = [&](const Point &point) {
        int &count = counts[key(point)];
        if (count == 0)
            candidates.append(point);
        ++count;
    };
    for (const Wire &wire : sheet.wires) {
        if (wire.points.size() < 2)
            continue;
        addEnd(wire.points.first());
        addEnd(wire.points.last());
    }
    for (const Point &pin : pinPoints(sheet, library))
        addEnd(pin);

    QList<Point> found;
    for (const Point &point : candidates) {
        int count = counts.value(key(point));
        // A wire that goes on through the point joins it from two sides.
        for (const Wire &wire : sheet.wires) {
            if (wire.points.size() >= 2 && wire.points.first() != point
                && wire.points.last() != point && isOnWire(wire, point)) {
                count += 2;
            }
        }
        if (count >= 3)
            found.append(point);
    }
    return found;
}

namespace {

// The way the wire 'wire', which ends at 'point', leaves it: the offset of
// the next point from there.
Point legAt(const Wire &wire, const Point &point)
{
    const QList<Point> &p = wire.points;
    return p.first() == point ? p.at(1) - point : p.at(p.size() - 2) - point;
}

// The wires of 'wires' that end at 'point', two by two, each pair going on
// straight across it, the indices of each pair in their order in 'wires';
// none when some wire ending there has no such partner, or ends there twice.
QList<std::pair<int, int>> straightPairsAt(const QList<Wire> &wires, const Point &point)
{
    QList<int> ends;
    for (int i = 0; i < wires.size(); ++i) {
        const QList<Point> &p = wires.at(i).points;
        if (p.size() < 2 || (p.first() != point && p.last() != point))
            continue;
        if (p.first() == point && p.last() == point)
            return {};
        ends.append(i);
    }
    QList<std::pair<int, int>> pairs;
    while (!ends.isEmpty()) {
        const int a = ends.takeFirst();
        const Point leg = legAt(wires.at(a), point);
        const auto partner = std::find_if(ends.cbegin(), ends.cend(), [&](int b) {
            const Point other = legAt(wires.at(b), point);
            return cross(leg, other) == 0 && dot(leg, other) < 0;
        });
        if (partner == ends.cend())
            return {};
        pairs.append({a, *partner});
        ends.removeAt(int(partner - ends.cbegin()));
    }
    return pairs;
}

// The wires passing through 'point' of 'wires' without ending there.
int wiresThrough(const QList<Wire> &wires, const Point &point)
{
    return int(std::count_if(wires.cbegin(), wires.cend(),
                             [&point](const Wire &wire) { return passesThrough(wire, point); }));
}

} // namespace

bool canJoinWiresAt(const Sheet &sheet, const Library &library, const Point &point)
{
    const bool endsThere = std::any_of(sheet.wires.cbegin(), sheet.wires.cend(),
                                       [&point](const Wire &wire) {
                                           return !wire.points.isEmpty()
                                                  && (wire.points.first() == point
                                                      || wire.points.last() == point);
                                       });
    return !endsThere && wiresThrough(sheet.wires, point) >= 2
           && !pinPoints(sheet, library).contains(point);
}

Sheet withWiresJoinedAt(const Sheet &sheet, const Library &library, const Point &point)
{
    if (!canJoinWiresAt(sheet, library, point))
        return sheet;
    Sheet result = sheet;
    cutAt(result.wires, point);
    return result;
}

bool canSeparateWiresAt(const Sheet &sheet, const Library &library, const Point &point)
{
    const QList<std::pair<int, int>> pairs = straightPairsAt(sheet.wires, point);
    return !pairs.isEmpty() && int(pairs.size()) + wiresThrough(sheet.wires, point) >= 2
           && !isOnBus(sheet, point) && !pinPoints(sheet, library).contains(point);
}

Sheet withWiresSeparatedAt(const Sheet &sheet, const Library &library, const Point &point)
{
    if (!canSeparateWiresAt(sheet, library, point))
        return sheet;
    Sheet result = sheet;
    QList<Wire> &wires = result.wires;
    QList<int> gone;
    for (const auto &[a, b] : straightPairsAt(wires, point)) {
        // 'a' leads into the point, 'b' leads on from it; 'a' keeps its id.
        QList<Point> into = wires.at(a).points;
        if (into.last() != point)
            std::reverse(into.begin(), into.end());
        QList<Point> on = wires.at(b).points;
        if (on.first() != point)
            std::reverse(on.begin(), on.end());
        wires[a].points = simplifiedWire(into + on.mid(1));
        gone.append(b);
    }
    std::sort(gone.begin(), gone.end(), std::greater<int>());
    for (const int index : gone)
        wires.removeAt(index);
    return result;
}

QList<Point> wireRoute(const Point &from, const Point &to, bool swapped, bool diagonal)
{
    const Point delta = to - from;
    Point corner;
    if (!diagonal) {
        corner = swapped ? Point{from.x, to.y} : Point{to.x, from.y};
    } else {
        // The slanted segment takes the shorter of the two distances; the
        // straight one runs along the longer, first or last.
        const Coord dx = std::llabs(delta.x);
        const Coord dy = std::llabs(delta.y);
        const Coord slant = qMin(dx, dy);
        const Point slantStep{sign(delta.x) * slant, sign(delta.y) * slant};
        corner = swapped ? from + slantStep : to - slantStep;
    }
    QList<Point> points;
    if (corner != from && corner != to)
        points.append(corner);
    if (to != from)
        points.append(to);
    return points;
}

QList<Point> simplifiedWire(const QList<Point> &points)
{
    QList<Point> result;
    for (const Point &point : points) {
        if (!result.isEmpty() && result.last() == point)
            continue;
        // The last corner is dropped when the wire goes on the same way.
        if (result.size() >= 2) {
            const Point before = result.at(result.size() - 2);
            const Point corner = result.last();
            const Point in = corner - before;
            const Point out = point - corner;
            if (cross(in, out) == 0 && dot(in, out) > 0)
                result.removeLast();
        }
        result.append(point);
    }
    return result;
}

QList<QList<Point>> wiresNotTurningBack(const QList<Point> &points)
{
    QList<QList<Point>> result;
    QList<Point> rest = points;
    for (;;) {
        int turn = -1;
        for (int i = 1; i + 1 < rest.size(); ++i) {
            const Point in = rest.at(i) - rest.at(i - 1);
            const Point out = rest.at(i + 1) - rest.at(i);
            if (cross(in, out) == 0 && dot(in, out) < 0) {
                turn = i;
                break;
            }
        }
        if (turn < 0)
            break;
        // Back short of the segment's start: the rest goes on from there;
        // else the wire ends at that start and the rest goes from the corner.
        const bool backShort =
            isOnSegment(rest.at(turn - 1), rest.at(turn), rest.at(turn + 1));
        const int split = backShort ? turn + 1 : turn;
        const QList<Point> head = rest.mid(0, split);
        if (head.size() >= 2)
            result.append(head);
        rest = rest.mid(split);
    }
    if (rest.size() >= 2)
        result.append(rest);
    return result;
}

static Sheet wiresFollowing(const Sheet &sheetBefore, const Sheet &sheetAfter,
                            const QSet<ObjectId> &moved, const Library &library,
                            WireRouting routing)
{
    // A wire left in place that passes through a pin that goes is cut there
    // first, in both sheets alike, so that its two parts end on the pin and
    // follow it: a pin on a wire is joined to it, as its dot shows.
    Sheet before = sheetBefore;
    Sheet after = sheetAfter;
    const PointFollower uncut(sheetBefore, sheetAfter, moved, library);
    for (const PointFollower::MovedPin &pin : uncut.movedPins()) {
        if (pin.to == pin.from)
            continue;
        for (int i = 0; i < before.wires.size(); ++i) {
            const ObjectId id = before.wires.at(i).id;
            if (moved.contains(id) || !passesThrough(before.wires.at(i), pin.from))
                continue;
            const ObjectId restId = newObjectId();
            splitAt(before.wires, i, pin.from, restId);
            for (int j = 0; j < after.wires.size(); ++j) {
                if (after.wires.at(j).id == id) {
                    splitAt(after.wires, j, pin.from, restId);
                    break;
                }
            }
        }
    }
    const PointFollower follower(before, after, moved, library);
    // The wires that have an end to follow are no obstacles where they were:
    // each becomes one again where it is laid.
    Obstacles obstacles = obstaclesOf(after, library, routing);
    QSet<ObjectId> following;
    for (const Wire &wire : before.wires) {
        if (!moved.contains(wire.id) && wire.points.size() >= 2
            && (follower.follow(wire.points.first()) || follower.follow(wire.points.last()))) {
            following.insert(wire.id);
            obstacles = obstacles.without(wire.id);
        }
    }
    // A pin with two wires ending on it in one line, one going on the way
    // the other comes: the line they make stays as it is, so that what else
    // lies on it stays on it. Moved along the line onto one of them, short
    // of its next point, the pin slides along it, one wire growing as the
    // other shrinks; moved anywhere else, the two stay as they are and a
    // new wire joins the pin to the line (see 'offLine' below). Nothing else
    // may be on the pin, and the far ends of the two wires must stay. Their
    // points, by their ids, and the pins gone off their line.
    QHash<ObjectId, QList<Point>> lineKept;
    QList<PointFollower::MovedPin> offLine;
    for (const PointFollower::MovedPin &pin : follower.movedPins()) {
        if (pin.to == pin.from || follower.isStillPin(pin.from) || isOnBus(before, pin.from))
            continue;
        const auto samePin = [&pin](const PointFollower::MovedPin &other) {
            return other.from == pin.from;
        };
        if (std::count_if(follower.movedPins().cbegin(), follower.movedPins().cend(), samePin)
            != 1)
            continue;
        QList<const Wire *> ending;
        bool more = false;
        for (const Wire &wire : before.wires) {
            if (wire.points.size() < 2 || !isOnWire(wire, pin.from))
                continue;
            const bool atFirst = wire.points.first() == pin.from;
            const bool atLast = wire.points.last() == pin.from;
            if (moved.contains(wire.id) || atFirst == atLast)
                more = true;
            else
                ending.append(&wire);
        }
        if (more || ending.size() != 2)
            continue;
        const auto next = [&pin](const Wire &wire) {
            const QList<Point> &p = wire.points;
            return p.first() == pin.from ? p.at(1) : p.at(p.size() - 2);
        };
        const auto farEnd = [&pin](const Wire &wire) {
            return wire.points.first() == pin.from ? wire.points.last() : wire.points.first();
        };
        const Wire &a = *ending.at(0);
        const Wire &b = *ending.at(1);
        if (follower.follow(farEnd(a)) || follower.follow(farEnd(b)))
            continue;
        const Point way = pin.to - pin.from;
        const Point toA = next(a) - pin.from;
        const Point toB = next(b) - pin.from;
        if (cross(toA, toB) != 0 || dot(toA, toB) >= 0)
            continue;
        const Point onto = dot(way, toA) > 0 ? next(a) : next(b);
        const bool along =
            cross(way, toA) == 0 && isOnSegment(pin.from, onto, pin.to) && pin.to != onto;
        for (const Wire *wire : {&a, &b}) {
            QList<Point> points = wire->points;
            if (along && points.first() == pin.from)
                points.first() = pin.to;
            else if (along)
                points.last() = pin.to;
            lineKept.insert(wire->id, points);
        }
        if (!along)
            offLine.append(pin);
    }
    QHash<ObjectId, const Wire *> wiresBefore;
    for (const Wire &wire : before.wires)
        wiresBefore.insert(wire.id, &wire);
    QHash<ObjectId, const Wire *> wiresAfter;
    for (const Wire &wire : after.wires)
        wiresAfter.insert(wire.id, &wire);
    // The wires that do not stay as they are: an end of a moved wire slides
    // only along the others.
    const QSet<ObjectId> changing = QSet<ObjectId>(moved).unite(following);
    const auto onWireOf = [&](const Point &end, const QSet<ObjectId> &skip) {
        return std::any_of(before.wires.cbegin(), before.wires.cend(), [&](const Wire &other) {
            return !skip.contains(other.id) && other.points.size() >= 2 && isOnWire(other, end);
        });
    };
    // Where the end 'end' of a moved wire, gone to 'goneTo' with it, is held
    // by what is not moved with it: on a pin or a bus left in place, there;
    // on a wire that stays as it is, even on such a pin, slid along the
    // wires that stay towards 'goneTo', as the end of a moved segment
    // slides; on a wire laid again, slid along the wires not moved, where
    // they were, towards 'goneTo', and that wire keeps passing through the
    // point it gets to; where a wire that goes whole takes it. Nothing when
    // it is free, or on a pin that goes as well.
    // 'still', when given, tells whether it is held by what stays.
    const auto heldAt = [&](const Point &end, const Point &goneTo,
                            bool *still = nullptr) -> std::optional<Point> {
        if (still != nullptr)
            *still = true;
        const bool onStillWire = onWireOf(end, changing);
        if (follower.isStillPin(end))
            return onStillWire ? slidAlongWires(before.wires, changing, end, goneTo) : end;
        // A bus left in place keeps the end of a wire that enters it.
        const bool onStillBus =
            std::any_of(before.buses.cbegin(), before.buses.cend(), [&](const Bus &bus) {
                return !moved.contains(bus.id) && isOnWire(bus.points, end);
            });
        if (onStillBus)
            return end;
        if (follower.isMovedPin(end))
            return std::nullopt;
        if (const std::optional<Point> to = follower.shiftedWith(end)) {
            if (still != nullptr)
                *still = false;
            return to;
        }
        if (onStillWire)
            return slidAlongWires(before.wires, changing, end, goneTo);
        if (!onWireOf(end, moved))
            return std::nullopt;
        return slidAlongWires(before.wires, moved, end, goneTo);
    };
    // Where the moved wire 'was' has taken its last end, or its first: its
    // end itself when it is not laid the same.
    const auto goneTo = [&](const Wire &was, bool last) {
        const Wire *now = wiresAfter.value(was.id);
        if (now == nullptr || now->points.size() != was.points.size())
            return last ? was.points.last() : was.points.first();
        return last ? now->points.last() : now->points.first();
    };
    // The ends that stay, with the wire they belong to: those of the wires
    // that stay, and those of the moved ones held by what stays, where they
    // are held. A wire is joined to another where one of them lies on it.
    QList<QPair<Point, ObjectId>> ends;
    for (const Wire &wire : before.wires) {
        if (wire.points.size() < 2)
            continue;
        for (const bool last : {false, true}) {
            const Point end = last ? wire.points.last() : wire.points.first();
            if (!moved.contains(wire.id)) {
                ends.append(qMakePair(end, wire.id));
                continue;
            }
            bool still = false;
            const std::optional<Point> held = heldAt(end, goneTo(wire, last), &still);
            if (held && still)
                ends.append(qMakePair(*held, wire.id));
        }
    }
    Sheet result = after;
    QList<Wire> wires;
    // The wires already led on from a point to where it went, with that
    // point. Another end at the same point stays there when one of them still
    // passes through it, joined to it, instead of going the same way beside it.
    QList<QPair<Point, Wire>> led;
    // Where a branch of no length was taken: the net already crosses the
    // wire there, and is joined to it below.
    QList<Point> crossingsToJoin;
    // 'next' is the point of the wire after that end: a wire that would
    // run along the led one from there does not stay, but goes too.
    const auto onLed = [&led](const Point &point, const Point &next) {
        return std::any_of(led.cbegin(), led.cend(), [&](const auto &other) {
            if (other.first != point || !isOnWire(other.second, point))
                return false;
            const QList<Point> &p = other.second.points;
            for (int i = 1; i < p.size(); ++i) {
                if (overlap(point, next, p.at(i - 1), p.at(i)))
                    return false;
            }
            return true;
        });
    };
    for (Wire wire : result.wires) {
        if (wire.points.size() < 2) {
            wires.append(wire);
            continue;
        }
        if (moved.contains(wire.id)) {
            // Its ends held by what is not moved with it are led back there.
            const Wire *was = wiresBefore.value(wire.id);
            if (was != nullptr && was->points.size() == wire.points.size()) {
                QList<Point> points = wire.points;
                const Obstacles own = obstacles.without(wire.id);
                const std::optional<Point> last = heldAt(was->points.last(), points.last());
                const std::optional<Point> first = heldAt(was->points.first(), points.first());
                if (last && points.last() != *last)
                    points = ledBack(points, *last, own);
                if (first && points.first() != *first) {
                    std::reverse(points.begin(), points.end());
                    points = ledBack(points, *first, own);
                    std::reverse(points.begin(), points.end());
                }
                wire.points = points;
            }
            wires.append(wire);
            continue;
        }
        if (lineKept.contains(wire.id)) {
            wire.points = lineKept.value(wire.id);
            if (following.contains(wire.id))
                obstacles.add(wire);
            wires.append(wire);
            continue;
        }
        const Point first = wire.points.first();
        const Point last = wire.points.last();
        // A new wire to where an end went, put after this one (see below).
        Wire branchWire;
        std::optional<Point> newFirst = follower.follow(first);
        std::optional<Point> newLast = follower.follow(last);
        // A wire that goes whole has other ends following it: it is not held.
        // A pin left in place on its middle holds it: it is laid again.
        const bool whole = newFirst && newLast && *newFirst - first == *newLast - last
                           && !passesThroughAny(wire, follower.stillPins());
        if (!whole && newFirst && onLed(first, wire.points.at(1)))
            newFirst.reset();
        if (!whole && newLast && onLed(last, wire.points.at(wire.points.size() - 2)))
            newLast.reset();
        if (whole) {
            const Point shift = *newFirst - first;
            for (Point &point : wire.points)
                point = point + shift;
        } else if (newFirst || newLast) {
            // The ends of other wires joined to this one, apart from those
            // at the ends that go, and the pins left in place on its middle,
            // joined to it there: they must stay on it.
            QList<Point> kept;
            for (const auto &[end, owner] : ends) {
                if (owner == wire.id || (newFirst && end == first) || (newLast && end == last))
                    continue;
                if (isOnWire(wire, end) && !kept.contains(end))
                    kept.append(end);
            }
            for (const Point &pin : follower.stillPins()) {
                if (pin != first && pin != last && isOnWire(wire, pin) && !kept.contains(pin))
                    kept.append(pin);
            }
            QList<Point> points = wire.points;
            // With one end going, a wire with joints to keep may end at the
            // last of them and the pin be reached by a new wire branching
            // off the net (see gridLedTo()).
            QList<Point> branch;
            QList<Point> *branchOut = newFirst && newLast ? nullptr : &branch;
            if (newFirst && newLast && points.size() <= 3 && kept.isEmpty()) {
                // Both ends went different ways, there is no middle segment
                // to take it up and no wire joined to it to keep: the wire
                // is laid again between them.
                const bool upright = points.at(0).x == points.at(1).x;
                points = laidBetween(*newFirst, *newLast, upright, obstacles.without(wire.id));
            } else {
                // An end that stays may leave its point for another of the
                // wires joined to it there.
                if (newLast)
                    points = ledTo(points, *newLast, kept, !newFirst, obstacles.without(wire.id),
                                   branchOut);
                if (newFirst && points.size() >= 2) {
                    std::reverse(points.begin(), points.end());
                    if (newLast)
                        kept.append(*newLast);
                    points = ledTo(points, *newFirst, kept, !newLast, obstacles.without(wire.id),
                                   branchOut);
                    std::reverse(points.begin(), points.end());
                }
            }
            wire.points = points;
            if (branch.size() >= 2) {
                // The new wire goes where the end went, the wire stays short.
                branchWire.id = newObjectId();
                branchWire.points = branch;
                led.append(qMakePair(newFirst ? first : last, branchWire));
            } else if (branch.size() == 1) {
                crossingsToJoin.append(branch.first());
            }
            if (wire.points.size() < 2)
                continue;
            if (newFirst && branch.isEmpty())
                led.append(qMakePair(first, wire));
            if (newLast && branch.isEmpty())
                led.append(qMakePair(last, wire));
        }
        if (following.contains(wire.id))
            obstacles.add(wire);
        wires.append(wire);
        if (branchWire.points.size() >= 2) {
            obstacles.add(branchWire);
            wires.append(branchWire);
        }
    }

    // Pins that touched pins left in place: a new wire keeps them joined.
    // One for each pair of points.
    QSet<QPair<quint64, quint64>> joined;
    Sheet laid = after;
    laid.wires = wires;
    const Obstacles now = obstaclesOf(laid, library, routing);
    // True when a pin gone from 'from' to 'to' lands on a wire joined to
    // one on 'from', one wire through another: it is cut there below, and
    // that joins them, so the pin needs no wire of its own.
    const auto netReaches = [&wires](const Point &from, const Point &to) {
        Sheet joinedBy;
        joinedBy.wires = wires;
        QSet<ObjectId> reached;
        for (const Wire &other : wires) {
            if (isOnWire(other, from))
                reached.unite(joinedWires(joinedBy, other.id));
        }
        return std::any_of(wires.cbegin(), wires.cend(), [&](const Wire &other) {
            return reached.contains(other.id) && isOnWire(other, to);
        });
    };
    // A pin gone off the line of its two wires: a new wire from the line,
    // from where the pin was or straight across from where it is.
    for (const PointFollower::MovedPin &pin : offLine) {
        if (netReaches(pin.from, pin.to))
            continue;
        Wire wire;
        wire.id = newObjectId();
        wire.points = laidToJoint(pin.from, pin.to, wires, now);
        if (wire.points.size() >= 2)
            wires.append(wire);
    }
    for (const PointFollower::MovedPin &pin : follower.movedPins()) {
        if (!follower.isStillPin(pin.from) || pin.to == pin.from)
            continue;
        const QPair<quint64, quint64> pair = qMakePair(key(pin.from), key(pin.to));
        if (joined.contains(pair))
            continue;
        joined.insert(pair);
        // Onto any wire not joined to the pin it left it needs its own.
        if (netReaches(pin.from, pin.to))
            continue;
        Wire wire;
        wire.id = newObjectId();
        wire.points = laidToJoint(pin.from, pin.to, wires, now);
        if (wire.points.size() >= 2)
            wires.append(wire);
    }

    // True when a wire ends at 'point'.
    const auto endsAt = [&wires](const Point &point) {
        return std::any_of(wires.cbegin(), wires.cend(), [&point](const Wire &w) {
            return w.points.size() >= 2 && (w.points.first() == point || w.points.last() == point);
        });
    };
    // A branch of no length ends where the wire it starts on crosses a wire
    // already reaching the pin: they are joined there, one cut on the other.
    for (const Point &point : crossingsToJoin) {
        if (!endsAt(point) && wiresThrough(wires, point) >= 2)
            cutAt(wires, point);
    }
    // A pin that was joined to something and has come to lie on the middle
    // of a wire, with no wire ending there, cuts that wire, so that it is
    // joined to it.
    for (const PointFollower::MovedPin &pin : follower.movedPins()) {
        const bool wasJoined =
            follower.isStillPin(pin.from)
            || std::any_of(before.wires.cbegin(), before.wires.cend(), [&pin](const Wire &w) {
                   return w.points.size() >= 2
                          && (w.points.first() == pin.from || w.points.last() == pin.from);
               });
        if (wasJoined && !endsAt(pin.to))
            cutAt(wires, pin.to);
    }
    result.wires = wires;
    return result;
}

// withWireEndAt() without the labels; with 'exact' the end goes to 'to'
// itself, not to a wire already reaching it (gridLedTo()).
static Sheet wireEndAt(const Sheet &sheet, const Library &library, const ObjectId &wire,
                       bool last, const Point &to, WireRouting routing, bool exact = false)
{
    Sheet result = sheet;
    for (int i = 0; i < result.wires.size(); ++i) {
        Wire &dragged = result.wires[i];
        if (dragged.id != wire || dragged.points.size() < 2)
            continue;
        QList<Point> points = dragged.points;
        if (!last)
            std::reverse(points.begin(), points.end());
        // The other wires joined to this one, apart from at the end that
        // goes, must stay joined.
        const QList<Point> kept = jointsOn(sheet, dragged, {}, {points.last()});
        // The wire dragged keeps its other end where it is.
        points = ledTo(points, to, kept, false,
                       obstaclesOf(sheet, library, routing).without(wire), nullptr, exact);
        if (!last)
            std::reverse(points.begin(), points.end());
        if (points.size() < 2)
            result.wires.removeAt(i);
        else
            dragged.points = points;
        break;
    }
    return result;
}

static Sheet segmentMoved(const Sheet &sheet, const Library &library, const ObjectId &wire,
                          int index, const Point &delta, WireRouting routing)
{
    int at = -1;
    for (int i = 0; i < sheet.wires.size(); ++i) {
        if (sheet.wires.at(i).id == wire)
            at = i;
    }
    if (at < 0)
        return sheet;
    const QList<Point> &p = sheet.wires.at(at).points;
    const int n = p.size();
    if (index < 0 || index + 1 >= n)
        return sheet;
    const Point a = p.at(index);
    const Point b = p.at(index + 1);
    // A level segment moves up and down, an upright one sideways.
    Point d = delta;
    if (a.y == b.y)
        d.x = 0;
    else if (a.x == b.x)
        d.y = 0;
    if (d == Point())
        return sheet;
    const Point na = a + d;
    const Point nb = b + d;
    const Obstacles obstacles = obstaclesOf(sheet, library, routing);
    // An end of the wire stays where a pin, another wire or a bus holds it.
    const QList<Point> pins = pinPoints(sheet, library);
    const auto held = [&](const Point &point) {
        if (pins.contains(point) || isOnBus(sheet, point))
            return true;
        return std::any_of(sheet.wires.cbegin(), sheet.wires.cend(), [&](const Wire &other) {
            return other.id != wire && other.points.size() >= 2 && isOnWire(other, point);
        });
    };
    // The wire's ends held by something: they stay, or slide along the
    // wires they lie on, towards where they go, so that what else is there
    // stays joined to them through those wires; a bend leads on from where
    // they get to.
    const bool aHeld = index == 0 && held(a);
    const bool bHeld = index + 2 == n && held(b);
    const Point aAt = aHeld ? slidAlongWires(sheet.wires, {wire}, a, na) : na;
    const Point bAt = bHeld ? slidAlongWires(sheet.wires, {wire}, b, nb) : nb;

    QList<Point> points;
    if (index > 0) {
        // The segment before goes on its own way to the moved one.
        points = p.mid(0, index);
        points += wireRoute(p.at(index - 1), na, p.at(index - 1).x == a.x, false);
    } else if (aAt != na) {
        points = laidBetween(aAt, na, d.x == 0, obstacles.without(wire));
    }
    if (points.isEmpty() || points.last() != na)
        points.append(na);
    points.append(nb);
    if (index + 2 < n) {
        // The segment after comes into its next point its own way.
        points += wireRoute(nb, p.at(index + 2), p.at(index + 2).x != b.x, false);
        points += p.mid(index + 3);
    } else if (bAt != nb) {
        points += laidBetween(nb, bAt, d.x == 0, obstacles.without(wire)).mid(1);
    }

    Sheet result = sheet;
    result.wires[at].points = straightened(points);

    // The ends of other wires on the segment go along with it, and those
    // on the wire next to it that it has left go to the corner it left them
    // for, so that they stay joined.
    Wire segment;
    segment.points = {a, b};
    Wire upToSegment;
    upToSegment.points = p.mid(0, index + 1);
    const Wire &was = sheet.wires.at(at);
    const QList<Point> now = result.wires.at(at).points;
    const auto goesTo = [&](const Point &end) -> std::optional<Point> {
        // What holds an end of the wire does not go with it.
        if ((aHeld && end == a) || (bHeld && end == b))
            return std::nullopt;
        if (isOnWire(segment, end))
            return end + d;
        if (isOnWire(was, end) && !isOnWire(now, end))
            return isOnWire(upToSegment, end) ? na : nb;
        return std::nullopt;
    };
    // What they keep off: the wire where it is now, not where it was, and
    // of the wires that go with it only those laid already, where they are
    // laid: one still where it was would count as a way already there.
    Obstacles around = obstaclesOf(result, library, routing);
    for (int i = 0; i < result.wires.size(); ++i) {
        const Wire &other = result.wires.at(i);
        if (i != at && other.points.size() >= 2
            && (goesTo(other.points.first()) || goesTo(other.points.last())))
            around = around.without(other.id);
    }
    for (int i = 0; i < result.wires.size(); ++i) {
        if (i == at)
            continue;
        Wire &other = result.wires[i];
        if (other.points.size() < 2)
            continue;
        const Point firstEnd = other.points.first();
        const Point lastEnd = other.points.last();
        const std::optional<Point> first = goesTo(firstEnd);
        const std::optional<Point> last = goesTo(lastEnd);
        if (!first && !last)
            continue;
        if (first && last && *first - firstEnd == *last - lastEnd) {
            const Point shift = *first - firstEnd;
            for (Point &point : other.points)
                point = point + shift;
            around.add(other);
            continue;
        }
        const Obstacles own = around.without(other.id);
        QList<Point> moving = other.points;
        if (last)
            moving = ledTo(moving, *last, jointsOn(sheet, other, {wire}, {lastEnd}), !first, own);
        if (first && moving.size() >= 2) {
            QList<Point> kept = jointsOn(sheet, other, {wire}, {firstEnd});
            if (last)
                kept.append(*last);
            std::reverse(moving.begin(), moving.end());
            moving = ledTo(moving, *first, kept, !last, own);
            std::reverse(moving.begin(), moving.end());
        }
        other.points = moving;
        around.add(other);
    }
    result.wires.removeIf([](const Wire &w) { return w.points.size() < 2; });
    return result;
}

namespace {

// The point of 'wire' nearest to 'point': on a level or upright segment
// straight across from it, else the nearer end of the segment, so that it
// stays on the grid the wire is on.
Point nearestOnWire(const Wire &wire, const Point &point)
{
    Point best = wire.points.first();
    double bestDistance = -1;
    const auto consider = [&](const Point &candidate) {
        const double dx = double(candidate.x - point.x);
        const double dy = double(candidate.y - point.y);
        const double distance = dx * dx + dy * dy;
        if (bestDistance < 0 || distance < bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    };
    for (int i = 1; i < wire.points.size(); ++i) {
        const Point &a = wire.points.at(i - 1);
        const Point &b = wire.points.at(i);
        if (a.y == b.y)
            consider(Point{qBound(qMin(a.x, b.x), point.x, qMax(a.x, b.x)), a.y});
        else if (a.x == b.x)
            consider(Point{a.x, qBound(qMin(a.y, b.y), point.y, qMax(a.y, b.y))});
        consider(a);
        consider(b);
    }
    return best;
}

// 'after', where the wires that entered a bus of 'before' at 45 degrees and
// still end on a bus enter it at 45 degrees again over the same length (see
// withBusEntry()), where they come to it: a tail laid along the bus is cut
// off first.
Sheet withBusEntriesKept(const Sheet &before, const Sheet &after)
{
    Sheet result = after;
    for (Wire &wire : result.wires) {
        const Wire *was = findById(before.wires, wire.id);
        if (was == nullptr)
            continue;
        for (const bool last : {true, false}) {
            QList<Point> old = was->points;
            QList<Point> now = wire.points;
            if (old.size() < 2 || now.size() < 2)
                continue;
            if (!last) {
                std::reverse(old.begin(), old.end());
                std::reverse(now.begin(), now.end());
            }
            const Point way = old.last() - old.at(old.size() - 2);
            const bool slanted = way.x != 0 && std::llabs(way.x) == std::llabs(way.y);
            if (!slanted || !isOnBus(before, old.last()) || !isOnBus(after, now.last()))
                continue;
            // A tail laid along the bus goes: the wire enters where it
            // comes to the bus.
            while (now.size() >= 3 && isOnBus(after, now.at(now.size() - 2)))
                now.removeLast();
            QList<Point> entered = withBusEntry(now, after, std::llabs(way.x));
            if (!last)
                std::reverse(entered.begin(), entered.end());
            wire.points = entered;
        }
    }
    return result;
}

// Where the pin of 'before' at 'at' is in 'after': the same pin of the same
// owner. Nothing when no pin of 'before' is there, or 'after' lacks it.
std::optional<Point> pinMovedFrom(const QList<SheetPin> &pinsBefore,
                                  const QList<SheetPin> &pinsAfter, const Point &at)
{
    const auto pin = std::find_if(pinsBefore.cbegin(), pinsBefore.cend(),
                                  [&at](const SheetPin &p) { return p.at == at; });
    if (pin == pinsBefore.cend())
        return std::nullopt;
    const auto index = std::count_if(pinsBefore.cbegin(), pin,
                                     [&pin](const SheetPin &p) { return p.owner == pin->owner; });
    int seen = 0;
    for (const SheetPin &now : pinsAfter) {
        if (now.owner == pin->owner && seen++ == index)
            return now.at;
    }
    return std::nullopt;
}

// 'after', where the labels and the no-connect marks that lay on a pin of
// 'before', and the labels that lay on a wire, and are not among 'moved',
// have gone with it: see withWiresFollowing().
Sheet withMarksCarried(const Sheet &before, const Sheet &after, const QSet<ObjectId> &moved,
                       const Library &library)
{
    Sheet result = after;
    const QList<SheetPin> pinsBefore = sheetPins(before, library);
    const QList<SheetPin> pinsAfter = sheetPins(after, library);
    for (NoConnect &mark : result.noConnects) {
        const NoConnect *was = findById(before.noConnects, mark.id);
        if (moved.contains(mark.id) || was == nullptr || was->at != mark.at)
            continue;
        if (const std::optional<Point> now = pinMovedFrom(pinsBefore, pinsAfter, mark.at))
            mark.at = *now;
    }
    for (Label &label : result.labels) {
        if (moved.contains(label.id))
            continue;
        // Only where it was left in place.
        const Label *was = findById(before.labels, label.id);
        if (was == nullptr || was->at != label.at)
            continue;
        const Point at = label.at;

        // On a pin: the same pin of the same owner, where it is now.
        if (std::any_of(pinsBefore.cbegin(), pinsBefore.cend(),
                        [&at](const SheetPin &p) { return p.at == at; })) {
            if (const std::optional<Point> now = pinMovedFrom(pinsBefore, pinsAfter, at))
                label.at = *now;
            continue;
        }

        // On a wire: as far along the same segment as it was, when the wire
        // kept its shape; else where it was if the wire is still there, or
        // the nearest point of the wire.
        for (const Wire &wire : before.wires) {
            if (wire.points.size() < 2 || !isOnWire(wire, at))
                continue;
            const Wire *now = findById(after.wires, wire.id);
            if (now == nullptr || now->points.size() < 2)
                break;
            if (now->points.size() == wire.points.size()) {
                int segment = 1;
                while (!isOnSegment(wire.points.at(segment - 1), wire.points.at(segment), at))
                    ++segment;
                const Point &from = wire.points.at(segment - 1);
                const Point &a = now->points.at(segment - 1);
                const Point &b = now->points.at(segment);
                const Coord along = qMax(std::llabs(at.x - from.x), std::llabs(at.y - from.y));
                const Coord length = qMax(std::llabs(b.x - a.x), std::llabs(b.y - a.y));
                const Coord reach = qMin(along, length);
                label.at = a + Point{sign(b.x - a.x) * reach, sign(b.y - a.y) * reach};
            } else if (!isOnWire(*now, at)) {
                label.at = nearestOnWire(*now, at);
            }
            break;
        }
    }
    return result;
}

} // namespace

namespace {

// 'sheet' with its bus 'id' laid as a wire of the same id and points, last
// of the wires, so that the wire functions lay it and the wires that enter
// it as they do wires joined to one; 'sheet' as it is without such a bus.
Sheet busAsWire(const Sheet &sheet, const ObjectId &id)
{
    Sheet result = sheet;
    for (int i = 0; i < result.buses.size(); ++i) {
        if (result.buses.at(i).id != id)
            continue;
        Wire line;
        line.id = id;
        line.points = result.buses.at(i).points;
        result.buses.removeAt(i);
        result.wires.append(line);
        break;
    }
    return result;
}

// 'edited', a sheet of busAsWire(), with the wire 'id' made the bus of
// 'original' again, where it was among the buses, with the wire's points,
// or with its own when 'own'; gone when 'edited' has no such wire.
Sheet wireAsBus(const Sheet &edited, const Sheet &original, const ObjectId &id, bool own)
{
    Sheet result = edited;
    QList<Point> points;
    bool laid = false;
    for (int i = 0; i < result.wires.size(); ++i) {
        if (result.wires.at(i).id == id) {
            points = result.wires.at(i).points;
            result.wires.removeAt(i);
            laid = true;
            break;
        }
    }
    for (int i = 0; i < original.buses.size(); ++i) {
        if (original.buses.at(i).id != id || !laid)
            continue;
        Bus bus = original.buses.at(i);
        if (!own)
            bus.points = points;
        result.buses.insert(qMin(i, int(result.buses.size())), bus);
    }
    return result;
}

// The buses of 'sheet' joined to one of 'ids' (joinedBuses()), those among
// them too, in the order of the sheet.
QList<ObjectId> busGroups(const Sheet &sheet, const QSet<ObjectId> &ids)
{
    QSet<ObjectId> group;
    for (const ObjectId &id : ids)
        group.unite(joinedBuses(sheet, id));
    QList<ObjectId> ordered;
    for (const Bus &bus : sheet.buses) {
        if (group.contains(bus.id))
            ordered.append(bus.id);
    }
    return ordered;
}

// 'sheet' with the buses 'ids' laid as wires (busAsWire()).
Sheet busesAsWires(const Sheet &sheet, const QList<ObjectId> &ids)
{
    Sheet result = sheet;
    for (const ObjectId &id : ids)
        result = busAsWire(result, id);
    return result;
}

// 'edited', a sheet of busesAsWires(), with the wires 'ids' made the buses
// of 'original' again (wireAsBus()), keeping their own points when 'own'
// holds their id.
Sheet wiresAsBuses(const Sheet &edited, const Sheet &original, const QList<ObjectId> &ids,
                   const QSet<ObjectId> &own)
{
    Sheet result = edited;
    for (const ObjectId &id : ids)
        result = wireAsBus(result, original, id, own.contains(id));
    return result;
}

} // namespace

namespace {

// 'after', where wires of 'before' have been laid again by dragging, with a
// wire of its own for each pin one of them passed through, at a corner or
// along a segment, that it no longer reaches and that no wire joined to it
// does: to the nearest point of the wire. A wire that ended on the pin may
// leave it: that is the drag.
Sheet withPinsKept(const Sheet &before, const Sheet &after, const Library &library,
                   WireRouting routing)
{
    Sheet result = after;
    Obstacles around = obstaclesOf(result, library, routing);
    const QList<Point> pins = pinPoints(before, library);
    for (const Wire &was : before.wires) {
        const Wire *now = findById(after.wires, was.id);
        if (was.points.size() < 2 || now == nullptr || now->points == was.points)
            continue;
        const Wire moved = *now;
        for (const Point &pin : pins) {
            if (!passesThrough(was, pin) || isOnWire(moved, pin))
                continue;
            const QSet<ObjectId> net = joinedWires(result, moved.id);
            const bool joined = std::any_of(result.wires.cbegin(), result.wires.cend(),
                                            [&](const Wire &w) {
                                                return net.contains(w.id) && isOnWire(w, pin);
                                            });
            if (joined)
                continue;
            const Point to = nearestOnWire(moved, pin);
            Wire stub;
            stub.id = newObjectId();
            stub.points = laidBetween(pin, to, pin.x == to.x, around.without(moved.id));
            if (stub.points.size() >= 2) {
                around.add(stub);
                result.wires.append(stub);
            }
        }
    }
    return result;
}

} // namespace

Sheet withWiresFollowing(const Sheet &before, const Sheet &after, const QSet<ObjectId> &moved,
                         const Library &library, WireRouting routing)
{
    // A moved bus takes the wires that enter it and its branches along, as
    // a moved wire does the wires on it; it goes itself just as it was
    // moved.
    QSet<ObjectId> movedBuses;
    for (const Bus &bus : before.buses) {
        if (moved.contains(bus.id))
            movedBuses.insert(bus.id);
    }
    const QList<ObjectId> buses = busGroups(before, movedBuses);
    const Sheet from = busesAsWires(before, buses);
    const Sheet to = busesAsWires(after, buses);
    Sheet followed = wiresFollowing(from, to, moved, library, routing);
    followed = wiresAsBuses(followed, after, buses, movedBuses);
    followed = withBusEntriesKept(before, followed);
    return withMarksCarried(before, followed, moved, library);
}

Sheet withBusEndAt(const Sheet &sheet, const Library &library, const ObjectId &bus, bool last,
                   const Point &to, WireRouting routing)
{
    // Its branches, and the bus it branches off, are laid as wires are.
    const QList<ObjectId> buses = busGroups(sheet, {bus});
    const Sheet laid = wireEndAt(busesAsWires(sheet, buses), library, bus, last, to, routing);
    const Sheet result = withBusEntriesKept(sheet, wiresAsBuses(laid, sheet, buses, {}));
    return withMarksCarried(sheet, result, QSet<ObjectId>(), library);
}

Sheet withBusSegmentMoved(const Sheet &sheet, const Library &library, const ObjectId &bus,
                          int index, const Point &delta, WireRouting routing)
{
    const QList<ObjectId> buses = busGroups(sheet, {bus});
    const Sheet laid =
        segmentMoved(busesAsWires(sheet, buses), library, bus, index, delta, routing);
    const Sheet result = withBusEntriesKept(sheet, wiresAsBuses(laid, sheet, buses, {}));
    return withMarksCarried(sheet, result, QSet<ObjectId>(), library);
}

Sheet withWireEndAt(const Sheet &sheet, const Library &library, const ObjectId &wire, bool last,
                    const Point &to, WireRouting routing)
{
    const Sheet laid =
        withPinsKept(sheet, wireEndAt(sheet, library, wire, last, to, routing), library, routing);
    return withMarksCarried(sheet, laid, QSet<ObjectId>(), library);
}

Sheet withSegmentMoved(const Sheet &sheet, const Library &library, const ObjectId &wire,
                       int index, const Point &delta, WireRouting routing)
{
    const Sheet laid =
        withPinsKept(sheet, segmentMoved(sheet, library, wire, index, delta, routing), library,
                     routing);
    const Sheet moved = withBusEntriesKept(sheet, laid);
    return withMarksCarried(sheet, moved, QSet<ObjectId>(), library);
}

ObjectId jointWire(const Sheet &sheet, const Library &library, const Point &point)
{
    if (isOnBus(sheet, point) || pinPoints(sheet, library).contains(point))
        return ObjectId();
    ObjectId through;
    bool ending = false;
    for (const Wire &wire : sheet.wires) {
        if (wire.points.size() < 2 || !isOnWire(wire, point))
            continue;
        if (wire.points.first() == point || wire.points.last() == point) {
            ending = true;
        } else {
            if (!through.isNull())
                return ObjectId();
            through = wire.id;
        }
    }
    return ending ? through : ObjectId();
}

Sheet withJointMoved(const Sheet &sheet, const Library &library, const Point &from,
                     const Point &to, WireRouting routing)
{
    const ObjectId through = jointWire(sheet, library, from);
    if (through.isNull())
        return sheet;
    const Wire *wire = findById(sheet.wires, through);
    const Point target = nearestOnWire(*wire, to);
    if (target == from)
        return sheet;
    // The wires ending at the joint, with which of their ends is there.
    QList<QPair<ObjectId, bool>> ending;
    for (const Wire &other : sheet.wires) {
        if (other.id == through || other.points.size() < 2)
            continue;
        if (other.points.last() == from)
            ending.append(qMakePair(other.id, true));
        else if (other.points.first() == from)
            ending.append(qMakePair(other.id, false));
    }
    Sheet result = sheet;
    for (const auto &[id, last] : ending)
        result = wireEndAt(result, library, id, last, target, routing, true);
    result = withPinsKept(sheet, result, library, routing);
    return withMarksCarried(sheet, result, QSet<ObjectId>(), library);
}

} // namespace ppcb
