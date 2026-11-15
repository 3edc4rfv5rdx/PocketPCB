#include "wiring.h"

#include "paint.h"

#include <QHash>

#include <algorithm>
#include <cstdlib>
#include <cmath>
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
};

// The obstacles of the components of 'sheet' whose symbol 'library' has.
Obstacles obstaclesOf(const Sheet &sheet, const Library &library)
{
    Obstacles obstacles;
    for (const Component &component : sheet.components) {
        const Symbol *symbol = library.findSymbolOfPart(component.part);
        if (symbol == nullptr)
            continue;
        for (const Pin &pin : symbol->pins) {
            obstacles.pins.append(qMakePair(componentPoint(component, pin.at),
                                            componentPoint(component, pinBodyEnd(pin))));
        }
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

// How bad the wire 'points' is: the times it goes through a component's
// body, then the times it runs along a pin, then its number of points. Less
// is better, in that order.
std::tuple<int, int, int> badness(const QList<Point> &points, const Obstacles &obstacles)
{
    int bodies = 0;
    int pins = 0;
    for (int i = 1; i < points.size(); ++i) {
        const Point &a = points.at(i - 1);
        const Point &b = points.at(i);
        for (const QRectF &body : obstacles.bodies) {
            if (crossesInside(a, b, body))
                ++bodies;
        }
        for (const auto &[tip, end] : obstacles.pins) {
            if (overlap(a, b, tip, end))
                ++pins;
        }
    }
    return std::make_tuple(bodies, pins, int(points.size()));
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

// 'value' brought to the nearest half of the pin grid, where wires may bend.
Coord onHalfGrid(Coord value)
{
    const Coord step = PIN_GRID / 2;
    return Coord(std::llround(double(value) / step)) * step;
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

// True when 'point' lies on the wire 'points'.
bool liesOn(const QList<Point> &points, const Point &point)
{
    Wire wire;
    wire.points = points;
    return isOnWire(wire, point);
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

// The wire 'points' (two or more) with its last point gone to 'to'.
//
// First the way a drag in other editors does it: the last segment slides
// along with the end and the segment before it grows or shrinks, so no bend
// is added; this needs the two last segments to be one level and one
// upright. Next the wire is led on from its last point (routesBetween(),
// going on along the last segment first, so that a straight wire stays
// one). Each of these that keeps every point of 'kept' on the wire is a
// candidate, and the least bad (bestWire()) is taken when it goes through
// no body and along no pin. Otherwise the last point is given up and the
// wire is led on from the one before, and so on back to the first point:
// a wire is laid again only as far back as needed to keep it clear. The
// points of 'kept' inside its segments count as its points for this. When
// nothing comes clear, the least bad of all; when no candidate keeps
// 'kept', a bend from the last point with nothing cut away.
QList<Point> ledTo(const QList<Point> &wirePoints, const Point &to, const QList<Point> &kept,
                   const Obstacles &obstacles)
{
    const QList<Point> points = withJoints(wirePoints, kept);
    const auto keepsAll = [&](const QList<Point> &candidate) {
        return std::all_of(kept.cbegin(), kept.cend(),
                           [&](const Point &point) { return liesOn(candidate, point); });
    };
    const int n = points.size();
    QList<Point> best;
    std::tuple<int, int, int> bestBadness;
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
                if (keepsAll(slid))
                    candidates.append(slid);
            }
        }
        const QList<Point> base = points.mid(0, n - dropped);
        const Point last = base.last();
        const bool vertical = base.size() >= 2 && base.at(base.size() - 2).x == last.x;
        for (const QList<Point> &route : routesBetween(last, to, vertical)) {
            const QList<Point> candidate = straightened(base + route.mid(1));
            if (keepsAll(candidate))
                candidates.append(candidate);
        }
        if (candidates.isEmpty())
            continue;
        const QList<Point> wire = bestWire(candidates, obstacles);
        const auto wireBadness = badness(wire, obstacles);
        if (std::get<0>(wireBadness) == 0 && std::get<1>(wireBadness) == 0)
            return wire;
        if (best.isEmpty() || wireBadness < bestBadness) {
            best = wire;
            bestBadness = wireBadness;
        }
    }
    if (!best.isEmpty())
        return best;
    const bool vertical = points.at(n - 2).x == points.at(n - 1).x;
    return simplifiedWire(points + wireRoute(points.last(), to, vertical, false));
}

// A wire from 'from' to 'to', the least bad of routesBetween().
QList<Point> laidBetween(const Point &from, const Point &to, bool upright,
                         const Obstacles &obstacles)
{
    return bestWire(routesBetween(from, to, upright), obstacles);
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
        QHash<ObjectId, const Component *> movedComponents;
        for (const Component &component : after.components) {
            if (moved.contains(component.id))
                movedComponents.insert(component.id, &component);
        }
        for (const Component &component : before.components) {
            const Symbol *symbol = library.findSymbolOfPart(component.part);
            if (symbol == nullptr)
                continue;
            const Component *now = movedComponents.value(component.id);
            for (const Pin &pin : symbol->pins) {
                const Point from = componentPoint(component, pin.at);
                if (now != nullptr)
                    m_pins.append({from, componentPoint(*now, pin.at)});
                else
                    m_stillPins.append(from);
            }
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
    }

    // True when the end of a pin of a component left in place is at 'point'.
    bool isStillPin(const Point &point) const { return m_stillPins.contains(point); }

    std::optional<Point> follow(const Point &point) const
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
        return std::nullopt;
    }

    const QList<MovedPin> &movedPins() const { return m_pins; }

private:
    QList<MovedPin> m_pins;
    // The pins of the components that stay: an end on one of them stays too.
    QList<Point> m_stillPins;
    QList<QPair<const Wire *, const Wire *>> m_wires;
};

} // namespace

Point componentPoint(const Component &component, const Point &point)
{
    Point p = point;
    if (component.mirrored)
        p.x = -p.x;
    // A quarter turn clockwise on screen, where Y grows downwards.
    for (int turn = 0; turn < component.rotation / 90; ++turn)
        p = Point{-p.y, p.x};
    return component.at + p;
}

QList<Point> pinPoints(const Sheet &sheet, const Library &library)
{
    QList<Point> points;
    for (const Component &component : sheet.components) {
        const Symbol *symbol = library.findSymbolOfPart(component.part);
        if (symbol == nullptr)
            continue;
        for (const Pin &pin : symbol->pins)
            points.append(componentPoint(component, pin.at));
    }
    return points;
}

bool isOnWire(const Wire &wire, const Point &point)
{
    if (wire.points.size() == 1)
        return wire.points.first() == point;
    for (int i = 1; i < wire.points.size(); ++i) {
        if (isOnSegment(wire.points.at(i - 1), wire.points.at(i), point))
            return true;
    }
    return false;
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

Sheet withWiresFollowing(const Sheet &before, const Sheet &after, const QSet<ObjectId> &moved,
                         const Library &library)
{
    const PointFollower follower(before, after, moved, library);
    const Obstacles obstacles = obstaclesOf(after, library);
    // The ends of the wires that stay, with the wire they belong to: a wire
    // is joined to another where one of them lies on it.
    QList<QPair<Point, ObjectId>> ends;
    for (const Wire &wire : before.wires) {
        if (!moved.contains(wire.id) && wire.points.size() >= 2) {
            ends.append(qMakePair(wire.points.first(), wire.id));
            ends.append(qMakePair(wire.points.last(), wire.id));
        }
    }
    Sheet result = after;
    QList<Wire> wires;
    for (Wire wire : result.wires) {
        if (moved.contains(wire.id) || wire.points.size() < 2) {
            wires.append(wire);
            continue;
        }
        const Point first = wire.points.first();
        const Point last = wire.points.last();
        const std::optional<Point> newFirst = follower.follow(first);
        const std::optional<Point> newLast = follower.follow(last);
        if (newFirst && newLast && *newFirst - first == *newLast - last) {
            const Point shift = *newFirst - first;
            for (Point &point : wire.points)
                point = point + shift;
        } else if (newFirst || newLast) {
            // The ends of other wires joined to this one, apart from those
            // at the ends that go: they must stay on it.
            QList<Point> kept;
            for (const auto &[end, owner] : ends) {
                if (owner == wire.id || (newFirst && end == first) || (newLast && end == last))
                    continue;
                if (isOnWire(wire, end) && !kept.contains(end))
                    kept.append(end);
            }
            QList<Point> points = wire.points;
            if (newFirst && newLast && points.size() <= 3) {
                // Both ends went different ways and there is no middle
                // segment to take it up: the wire is laid again between them.
                const bool upright = points.at(0).x == points.at(1).x;
                points = laidBetween(*newFirst, *newLast, upright, obstacles);
            } else {
                if (newLast)
                    points = ledTo(points, *newLast, kept, obstacles);
                if (newFirst && points.size() >= 2) {
                    std::reverse(points.begin(), points.end());
                    if (newLast)
                        kept.append(*newLast);
                    points = ledTo(points, *newFirst, kept, obstacles);
                    std::reverse(points.begin(), points.end());
                }
            }
            wire.points = points;
            if (wire.points.size() < 2)
                continue;
        }
        wires.append(wire);
    }

    // Pins that touched pins left in place: a new wire keeps them joined.
    // One for each pair of points.
    QSet<QPair<quint64, quint64>> joined;
    for (const PointFollower::MovedPin &pin : follower.movedPins()) {
        if (!follower.isStillPin(pin.from) || pin.to == pin.from)
            continue;
        const QPair<quint64, quint64> pair = qMakePair(key(pin.from), key(pin.to));
        if (joined.contains(pair))
            continue;
        joined.insert(pair);
        Wire wire;
        wire.id = newObjectId();
        wire.points = laidBetween(pin.from, pin.to, false, obstacles);
        if (wire.points.size() >= 2)
            wires.append(wire);
    }
    result.wires = wires;
    return result;
}

} // namespace ppcb
