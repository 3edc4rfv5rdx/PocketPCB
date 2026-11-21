#pragma once

// The helpers the files of the wiring code share: wiregeometry.cpp,
// wireroute.cpp, wirefollow.cpp and wireedit.cpp. Not for use outside them;
// each is described where it is defined.

#include "wiring.h"

#include <QRectF>

namespace ppcb::wiring {

// wiregeometry.cpp
Coord sign(Coord value);
qint64 cross(const Point &a, const Point &b);
qint64 dot(const Point &a, const Point &b);
bool isOnSegment(const Point &a, const Point &b, const Point &point);
quint64 key(const Point &point);
QList<Point> straightened(const QList<Point> &points);
bool overlap(const Point &a, const Point &b, const Point &c, const Point &d);
bool passesThrough(const Wire &wire, const Point &point);
bool passesThroughAny(const Wire &wire, const QList<Point> &points);
void splitAt(QList<Wire> &wires, int index, const Point &point, const ObjectId &restId);
void cutAt(QList<Wire> &wires, const Point &point);
int wiresThrough(const QList<Wire> &wires, const Point &point);
Point nearestOnWire(const Wire &wire, const Point &point);

// wireroute.cpp
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

Obstacles obstaclesOf(const Sheet &sheet, const Library &library, WireRouting routing);
QList<Point> ledTo(const QList<Point> &wirePoints, const Point &to, const QList<Point> &kept,
                   bool rejoin, const Obstacles &obstacles, QList<Point> *branch = nullptr,
                   bool exact = false);
QList<Point> laidBetween(const Point &from, const Point &to, bool upright,
                         const Obstacles &obstacles);
QList<Point> laidToJoint(const Point &from, const Point &to, const QList<Wire> &wires,
                         const Obstacles &obstacles);
QList<Point> jointsOn(const Sheet &sheet, const Wire &wire, const QList<ObjectId> &skip,
                      const QList<Point> &except);
QList<Point> ledBack(const QList<Point> &points, const Point &to, const Obstacles &obstacles);
Point slidAlongWires(const QList<Wire> &wires, const QSet<ObjectId> &skip, const Point &point,
                     const Point &to);

// wirefollow.cpp
Sheet withBusEntriesKept(const Sheet &before, const Sheet &after);
Sheet withMarksCarried(const Sheet &before, const Sheet &after, const QSet<ObjectId> &moved,
                       const Library &library);
QList<ObjectId> busGroups(const Sheet &sheet, const QSet<ObjectId> &ids);
Sheet busesAsWires(const Sheet &sheet, const QList<ObjectId> &ids);
Sheet wiresAsBuses(const Sheet &edited, const Sheet &original, const QList<ObjectId> &ids,
                   const QSet<ObjectId> &own);

} // namespace ppcb::wiring
