// Wires following the objects moved, turned or mirrored on a sheet, with
// the labels and marks on them (withWiresFollowing()).

#include "wiringdetail.h"

#include <QHash>

#include <algorithm>
#include <optional>

namespace ppcb {

using namespace wiring;

namespace {

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

} // namespace

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

namespace wiring {

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

} // namespace wiring

namespace {

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

} // namespace

namespace wiring {

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

} // namespace wiring

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

} // namespace

namespace wiring {

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

} // namespace wiring

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

} // namespace ppcb
