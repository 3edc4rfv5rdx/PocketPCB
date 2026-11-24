// Wires edited directly: an end, a segment or a joint dragged.

#include "wiringdetail.h"

#include <QHash>

#include <algorithm>
#include <optional>

namespace ppcb {

using namespace wiring;

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
    // The wires both of whose ends go by one shift, by their index: they are
    // shifted whole, and the ends of wires on them go along by that shift.
    QHash<int, Point> shifted;
    const auto goesTo = [&](const Point &end, int self) -> std::optional<Point> {
        // What holds an end of the wire does not go with it.
        if ((aHeld && end == a) || (bHeld && end == b))
            return std::nullopt;
        if (isOnWire(segment, end))
            return end + d;
        if (isOnWire(was, end) && !isOnWire(now, end))
            return isOnWire(upToSegment, end) ? na : nb;
        for (auto it = shifted.cbegin(); it != shifted.cend(); ++it) {
            if (it.key() != self && isOnWire(sheet.wires.at(it.key()), end))
                return end + it.value();
        }
        return std::nullopt;
    };
    // Shifted whole, a wire takes along the ends on it, which may shift
    // another whole: until none more does.
    for (bool more = true; more;) {
        more = false;
        for (int i = 0; i < sheet.wires.size(); ++i) {
            const Wire &other = sheet.wires.at(i);
            if (i == at || shifted.contains(i) || other.points.size() < 2)
                continue;
            const std::optional<Point> first = goesTo(other.points.first(), i);
            const std::optional<Point> last = goesTo(other.points.last(), i);
            if (first && last && *first - other.points.first() == *last - other.points.last()) {
                shifted.insert(i, *first - other.points.first());
                more = true;
            }
        }
    }
    // What they keep off: the wire where it is now, not where it was, and
    // of the wires that go with it only those laid already, where they are
    // laid: one still where it was would count as a way already there.
    Obstacles around = obstaclesOf(result, library, routing);
    for (int i = 0; i < result.wires.size(); ++i) {
        const Wire &other = result.wires.at(i);
        if (i != at && other.points.size() >= 2
            && (goesTo(other.points.first(), i) || goesTo(other.points.last(), i)))
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
        const std::optional<Point> first = goesTo(firstEnd, i);
        const std::optional<Point> last = goesTo(lastEnd, i);
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
