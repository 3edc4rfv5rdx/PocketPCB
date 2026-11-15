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
    // The segments of the wires, with the wire they belong to.
    QList<QPair<ObjectId, QPair<Point, Point>>> wires;

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
    for (const Wire &wire : sheet.wires)
        obstacles.add(wire);
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

// How bad a wire is: the times it goes through a component's body, then
// the times it runs along a pin or another wire, then its number of points,
// then its length. Less is better, in that order.
using Badness = std::tuple<int, int, int, qint64>;

Badness badness(const QList<Point> &points, const Obstacles &obstacles)
{
    int bodies = 0;
    int pins = 0;
    qint64 length = 0;
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
        for (const auto &[owner, segment] : obstacles.wires) {
            if (overlap(a, b, segment.first, segment.second))
                ++pins;
        }
        length += std::llabs(b.x - a.x) + std::llabs(b.y - a.y);
    }
    return std::make_tuple(bodies, pins, int(points.size()), length);
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
// 'kept', the least bad of those that do not.
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
        if (std::get<0>(wireBadness) == 0 && std::get<1>(wireBadness) == 0)
            return wire;
        if (best.isEmpty() || wireBadness < bestBadness) {
            best = wire;
            bestBadness = wireBadness;
        }
    }
    if (!best.isEmpty())
        return best;
    return bestWire(unkept, obstacles);
}

// A wire from 'from' to 'to', the least bad of routesBetween().
QList<Point> laidBetween(const Point &from, const Point &to, bool upright,
                         const Obstacles &obstacles)
{
    return bestWire(routesBetween(from, to, upright), obstacles);
}

// True when 'wire' passes through 'point' without ending there.
bool passesThrough(const Wire &wire, const Point &point)
{
    const QList<Point> &p = wire.points;
    return p.size() >= 2 && p.first() != point && p.last() != point && isOnWire(wire, point);
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
        } else if (p.at(j) != point) {
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
// least bad of all the ways (bestWire()).
QList<Point> laidToJoint(const Point &from, const Point &to, const QList<Wire> &wires,
                         const Obstacles &obstacles)
{
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
        // A wire left alone whose two ends go the same way goes whole, and
        // so does what lies on it: found one through another.
        for (bool found = true; found;) {
            found = false;
            for (const Wire &wire : before.wires) {
                if (moved.contains(wire.id) || wire.points.size() < 2 || isShifted(wire))
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
// 'to' added, the least bad of routesBetween(); nothing is cut away.
QList<Point> ledBack(const QList<Point> &points, const Point &to, const Obstacles &obstacles)
{
    const int n = points.size();
    const bool vertical = points.at(n - 2).x == points.at(n - 1).x;
    QList<QList<Point>> candidates;
    for (const QList<Point> &route : routesBetween(points.last(), to, vertical))
        candidates.append(straightened(points + route.mid(1)));
    return bestWire(candidates, obstacles);
}

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

QSet<ObjectId> joinedWires(const Sheet &sheet, const ObjectId &wire)
{
    const auto joined = [](const Wire &a, const Wire &b) {
        if (a.points.isEmpty() || b.points.isEmpty())
            return false;
        return isOnWire(b, a.points.first()) || isOnWire(b, a.points.last())
               || isOnWire(a, b.points.first()) || isOnWire(a, b.points.last());
    };
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
            if (!found.contains(other.id) && joined(*current, other)) {
                found.insert(other.id);
                waiting.append(&other);
            }
        }
    }
    return found;
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

Sheet withWiresFollowing(const Sheet &sheetBefore, const Sheet &sheetAfter,
                         const QSet<ObjectId> &moved, const Library &library)
{
    // A wire left in place that passes through a pin that goes is cut there
    // first, in both sheets alike, so that its two parts end on the pin and
    // follow it: a pin on a wire is joined to it, as its dot shows.
    Sheet before = sheetBefore;
    Sheet after = sheetAfter;
    for (const PointFollower::MovedPin &pin :
         PointFollower(sheetBefore, sheetAfter, moved, library).movedPins()) {
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
    Obstacles obstacles = obstaclesOf(after, library);
    QSet<ObjectId> following;
    for (const Wire &wire : before.wires) {
        if (!moved.contains(wire.id) && wire.points.size() >= 2
            && (follower.follow(wire.points.first()) || follower.follow(wire.points.last()))) {
            following.insert(wire.id);
            obstacles = obstacles.without(wire.id);
        }
    }
    // The ends of the wires that stay, with the wire they belong to: a wire
    // is joined to another where one of them lies on it.
    QList<QPair<Point, ObjectId>> ends;
    for (const Wire &wire : before.wires) {
        if (!moved.contains(wire.id) && wire.points.size() >= 2) {
            ends.append(qMakePair(wire.points.first(), wire.id));
            ends.append(qMakePair(wire.points.last(), wire.id));
        }
    }
    QHash<ObjectId, const Wire *> wiresBefore;
    for (const Wire &wire : before.wires)
        wiresBefore.insert(wire.id, &wire);
    // True when 'point' lies on a pin or a wire left in place.
    const auto staysJoined = [&](const Point &point) {
        if (follower.isStillPin(point))
            return true;
        return std::any_of(before.wires.cbegin(), before.wires.cend(), [&](const Wire &other) {
            return !moved.contains(other.id) && other.points.size() >= 2 && isOnWire(other, point);
        });
    };
    Sheet result = after;
    QList<Wire> wires;
    // The wires already led on from a point to where it went, with that
    // point. Another end at the same point stays there when one of them still
    // passes through it, joined to it, instead of going the same way beside it.
    QList<QPair<Point, Wire>> led;
    const auto onLed = [&led](const Point &point) {
        return std::any_of(led.cbegin(), led.cend(), [&point](const auto &other) {
            return other.first == point && isOnWire(other.second, point);
        });
    };
    for (Wire wire : result.wires) {
        if (wire.points.size() < 2) {
            wires.append(wire);
            continue;
        }
        if (moved.contains(wire.id)) {
            // Its ends that were joined to what stays are led back there.
            const Wire *was = wiresBefore.value(wire.id);
            if (was != nullptr && was->points.size() == wire.points.size()) {
                QList<Point> points = wire.points;
                const Obstacles own = obstacles.without(wire.id);
                if (points.last() != was->points.last() && staysJoined(was->points.last()))
                    points = ledBack(points, was->points.last(), own);
                if (points.first() != was->points.first() && staysJoined(was->points.first())) {
                    std::reverse(points.begin(), points.end());
                    points = ledBack(points, was->points.first(), own);
                    std::reverse(points.begin(), points.end());
                }
                wire.points = points;
            }
            wires.append(wire);
            continue;
        }
        const Point first = wire.points.first();
        const Point last = wire.points.last();
        std::optional<Point> newFirst = follower.follow(first);
        std::optional<Point> newLast = follower.follow(last);
        // A wire that goes whole has other ends following it: it is not held.
        const bool whole = newFirst && newLast && *newFirst - first == *newLast - last;
        if (!whole && newFirst && onLed(first))
            newFirst.reset();
        if (!whole && newLast && onLed(last))
            newLast.reset();
        if (whole) {
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
            if (newFirst && newLast && points.size() <= 3 && kept.isEmpty()) {
                // Both ends went different ways, there is no middle segment
                // to take it up and no wire joined to it to keep: the wire
                // is laid again between them.
                const bool upright = points.at(0).x == points.at(1).x;
                points = laidBetween(*newFirst, *newLast, upright, obstacles.without(wire.id));
            } else {
                if (newLast)
                    points = ledTo(points, *newLast, kept, obstacles.without(wire.id));
                if (newFirst && points.size() >= 2) {
                    std::reverse(points.begin(), points.end());
                    if (newLast)
                        kept.append(*newLast);
                    points = ledTo(points, *newFirst, kept, obstacles.without(wire.id));
                    std::reverse(points.begin(), points.end());
                }
            }
            wire.points = points;
            if (wire.points.size() < 2)
                continue;
            if (newFirst)
                led.append(qMakePair(first, wire));
            if (newLast)
                led.append(qMakePair(last, wire));
        }
        if (following.contains(wire.id))
            obstacles.add(wire);
        wires.append(wire);
    }

    // Pins that touched pins left in place: a new wire keeps them joined.
    // One for each pair of points.
    QSet<QPair<quint64, quint64>> joined;
    Sheet laid = after;
    laid.wires = wires;
    const Obstacles now = obstaclesOf(laid, library);
    for (const PointFollower::MovedPin &pin : follower.movedPins()) {
        if (!follower.isStillPin(pin.from) || pin.to == pin.from)
            continue;
        const QPair<quint64, quint64> pair = qMakePair(key(pin.from), key(pin.to));
        if (joined.contains(pair))
            continue;
        joined.insert(pair);
        // Onto a wire joined to the pin it left: it is cut there below, and
        // that joins them. Onto any other wire it still needs its own.
        Sheet joinedBy;
        joinedBy.wires = wires;
        QSet<ObjectId> reached;
        for (const Wire &other : wires) {
            if (isOnWire(other, pin.from))
                reached.unite(joinedWires(joinedBy, other.id));
        }
        if (std::any_of(wires.cbegin(), wires.cend(), [&](const Wire &other) {
                return reached.contains(other.id) && isOnWire(other, pin.to);
            }))
            continue;
        Wire wire;
        wire.id = newObjectId();
        wire.points = laidToJoint(pin.from, pin.to, wires, now);
        if (wire.points.size() >= 2)
            wires.append(wire);
    }

    // A pin that was joined to something and has come to lie on the middle
    // of a wire, with no wire ending there, cuts that wire, so that it is
    // joined to it.
    const auto endsAt = [&wires](const Point &point) {
        return std::any_of(wires.cbegin(), wires.cend(), [&point](const Wire &w) {
            return w.points.size() >= 2 && (w.points.first() == point || w.points.last() == point);
        });
    };
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

Sheet withWireEndAt(const Sheet &sheet, const Library &library, const ObjectId &wire, bool last,
                    const Point &to)
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
        points = ledTo(points, to, kept, obstaclesOf(sheet, library).without(wire));
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

Sheet withSegmentMoved(const Sheet &sheet, const Library &library, const ObjectId &wire,
                       int index, const Point &delta)
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
    const Obstacles obstacles = obstaclesOf(sheet, library);
    // An end of the wire stays where a pin or another wire holds it.
    const QList<Point> pins = pinPoints(sheet, library);
    const auto held = [&](const Point &point) {
        if (pins.contains(point))
            return true;
        return std::any_of(sheet.wires.cbegin(), sheet.wires.cend(), [&](const Wire &other) {
            return other.id != wire && other.points.size() >= 2 && isOnWire(other, point);
        });
    };
    // Unless it can slide along a wire it lies on, to where it goes: what
    // else is there stays joined to it through that wire.
    const auto slides = [&](const Point &point, const Point &to) {
        return std::any_of(sheet.wires.cbegin(), sheet.wires.cend(), [&](const Wire &other) {
            if (other.id == wire)
                return false;
            for (int i = 1; i < other.points.size(); ++i) {
                const Point &s0 = other.points.at(i - 1);
                const Point &s1 = other.points.at(i);
                if (isOnSegment(s0, s1, point) && isOnSegment(s0, s1, to))
                    return true;
            }
            return false;
        });
    };
    // The wire's ends held by something: they stay, or slide along it.
    const bool aHeld = index == 0 && held(a);
    const bool bHeld = index + 2 == n && held(b);
    const bool aStays = aHeld && !slides(a, na);
    const bool bStays = bHeld && !slides(b, nb);

    QList<Point> points;
    if (index > 0) {
        // The segment before goes on its own way to the moved one.
        points = p.mid(0, index);
        points += wireRoute(p.at(index - 1), na, p.at(index - 1).x == a.x, false);
    } else if (aStays) {
        points = laidBetween(a, na, d.x == 0, obstacles.without(wire));
    }
    if (points.isEmpty() || points.last() != na)
        points.append(na);
    points.append(nb);
    if (index + 2 < n) {
        // The segment after comes into its next point its own way.
        points += wireRoute(nb, p.at(index + 2), p.at(index + 2).x != b.x, false);
        points += p.mid(index + 3);
    } else if (bStays) {
        points += laidBetween(nb, b, d.x == 0, obstacles.without(wire)).mid(1);
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
        if (isOnWire(was, end) && !liesOn(now, end))
            return isOnWire(upToSegment, end) ? na : nb;
        return std::nullopt;
    };
    // What they keep off: the wire where it is now, not where it was.
    const Obstacles around = obstaclesOf(result, library);
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
            continue;
        }
        const Obstacles own = around.without(other.id);
        QList<Point> moving = other.points;
        if (last)
            moving = ledTo(moving, *last, jointsOn(sheet, other, {wire}, {lastEnd}), own);
        if (first && moving.size() >= 2) {
            QList<Point> kept = jointsOn(sheet, other, {wire}, {firstEnd});
            if (last)
                kept.append(*last);
            std::reverse(moving.begin(), moving.end());
            moving = ledTo(moving, *first, kept, own);
            std::reverse(moving.begin(), moving.end());
        }
        other.points = moving;
    }
    result.wires.removeIf([](const Wire &w) { return w.points.size() < 2; });
    return result;
}

} // namespace ppcb
