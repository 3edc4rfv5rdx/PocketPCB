// The geometry of wires on a sheet: points and segments, where wires and
// pins join, cutting, merging and simplifying wires, and bus entries.

#include "wiringdetail.h"

#include "paint.h"

#include <QHash>

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace ppcb {

using namespace wiring;

namespace wiring {

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

} // namespace wiring

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
        const Symbol *symbol = library.findSymbolOfPart(component.part, component.unit);
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

WirePiece wirePieceAt(const Sheet &sheet, const Library &library, const ObjectId &line,
                      int index, const QPointF &at)
{
    WirePiece piece{sheet, index};
    Wire *wire = nullptr;
    for (Wire &candidate : piece.sheet.wires) {
        if (candidate.id == line)
            wire = &candidate;
    }
    if (wire == nullptr || index < 0 || index + 1 >= wire->points.size())
        return piece;
    const Point a = wire->points.at(index);
    const Point b = wire->points.at(index + 1);
    // Where along the segment, from 'a', a point lies: 0 to 1.
    const QPointF along = toMm(b) - toMm(a);
    const double length = QPointF::dotProduct(along, along);
    if (length == 0)
        return piece;
    const auto part = [&a, &along, length](const QPointF &point) {
        return QPointF::dotProduct(point - toMm(a), along) / length;
    };
    // The pins and the ends of other wires inside the segment.
    QList<Point> stops = pinPoints(sheet, library);
    for (const Wire &other : sheet.wires) {
        if (other.id != line && !other.points.isEmpty())
            stops << other.points.first() << other.points.last();
    }
    const double clicked = part(at);
    std::optional<Point> from;
    std::optional<Point> to;
    for (const Point &stop : std::as_const(stops)) {
        if (stop == a || stop == b || !isOnWire(QList<Point>{a, b}, stop))
            continue;
        const double t = part(toMm(stop));
        if (t <= clicked && (!from || t > part(toMm(*from))))
            from = stop;
        if (t > clicked && (!to || t < part(toMm(*to))))
            to = stop;
    }
    int insertAt = index + 1;
    if (from) {
        wire->points.insert(insertAt++, *from);
        ++piece.index;
    }
    if (to)
        wire->points.insert(insertAt, *to);
    return piece;
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

} // namespace

namespace wiring {

// The wires passing through 'point' of 'wires' without ending there.
int wiresThrough(const QList<Wire> &wires, const Point &point)
{
    return int(std::count_if(wires.cbegin(), wires.cend(),
                             [&point](const Wire &wire) { return passesThrough(wire, point); }));
}

} // namespace wiring

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

namespace wiring {

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

} // namespace wiring

} // namespace ppcb
