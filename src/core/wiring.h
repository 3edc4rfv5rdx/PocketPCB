#pragma once

#include "library.h"
#include "schematic.h"

#include <QSet>

namespace ppcb {

// Wires on a sheet: where they connect and how a new one is laid. A wire
// connects to a pin or to another wire only where one of its ends lies
// exactly on the pin's point or on the other wire; wires that only cross
// are not connected.

// Where 'point', given in a symbol's own coordinates, lies on the sheet for
// a symbol placed at 'at': mirrored first, then turned 'rotation' degrees
// clockwise, then moved there.
Point placedPoint(const Point &at, int rotation, bool mirrored, const Point &point);
// The same for the symbol of 'component'.
Point componentPoint(const Component &component, const Point &point);

// A point of 'sheet' that wires connect to: the end of a pin of a component
// whose symbol the library has, or the point of a power symbol, whether
// the library has its symbol or not.
struct SheetPin
{
    // The component or the power symbol it belongs to.
    ObjectId owner;
    Point at;
    // The other end of its line, which wires laid by the program keep off.
    Point bodyEnd;
    // The number of a component's pin; empty for a power symbol.
    QString number;
};

// The pins of 'sheet': of every component in the order of its symbol's
// pins, then of every power symbol.
QList<SheetPin> sheetPins(const Sheet &sheet, const Library &library);

// The points of sheetPins().
QList<Point> pinPoints(const Sheet &sheet, const Library &library);
// The points of the pins of components alone: where a no-connect mark goes.
QList<Point> partPinPoints(const Sheet &sheet, const Library &library);

// True when 'point' lies on 'wire': on one of its segments, ends included.
bool isOnWire(const Wire &wire, const Point &point);

// True when 'point' lies on a bus of 'sheet': a wire drawn to it ends there,
// as an entry into the bus, though it connects only by its label.
bool isOnBus(const Sheet &sheet, const Point &point);

// The wire 'points' that ends on a bus of 'sheet', entering it at 45
// degrees: when its last segment meets a level or upright segment of the
// bus square on and is 'step' long at least, its last 'step' is turned into
// a slant to the point 'step' along the bus, to the right of an upright wire
// and down from a level one, or the other way where the bus does not reach.
// A last segment that goes on the line of the bus into an end of it steps
// 'step' aside instead, down from a level bus and right of an upright one,
// and its last 'step' is a slant back into that end. 'points' as they are
// otherwise.
QList<Point> withBusEntry(const QList<Point> &points, const Sheet &sheet, Coord step);
// The same at both ends of the wire 'points', drawn on 'sheet' and not on it
// yet: each end that lies on a bus and does not join a pin or a wire there
// enters the bus at 45 degrees.
QList<Point> withBusEntries(const QList<Point> &points, const Sheet &sheet,
                            const Library &library, Coord step);

// True when a wire ending at 'point' would connect there: to a pin, or to
// a wire of 'sheet'.
bool isConnectionPoint(const Sheet &sheet, const Library &library, const Point &point);

// The ids of the wires joined to the wire 'wire' of 'sheet', one through
// another, the wire itself among them: where an end of one lies on another.
// Empty when the sheet has no such wire.
QSet<ObjectId> joinedWires(const Sheet &sheet, const ObjectId &wire);

// Buses joined to each other as wires are: where an end of one lies on
// another. A bus drawn on to another is a branch of it, with its name.
// The ids of the buses joined to the bus 'bus' of 'sheet', one through
// another, the bus itself among them; empty when it has no such bus.
QSet<ObjectId> joinedBuses(const Sheet &sheet, const ObjectId &bus);
// The points where three or more ends of buses and buses meet, as
// junctions() counts them for wires: where a dot of the bus colour is drawn.
QList<Point> busJunctions(const Sheet &sheet);

// The points where three or more ends, pins and wires meet: where a dot is
// drawn. A wire passing through a point counts twice, so a wire ending on
// the middle of another makes a dot, and so do three wire ends on one point.
QList<Point> junctions(const Sheet &sheet, const Library &library);

// The points after 'from' that lead to 'to': a corner, then 'to', each
// segment horizontal, vertical or at 45 degrees. 'diagonal' makes one of
// the two segments slant; 'swapped' turns the corner the other way: the
// vertical (or the slanted) segment first. Points that would repeat the
// one before are left out, so 'from' == 'to' gives none.
QList<Point> wireRoute(const Point &from, const Point &to, bool swapped, bool diagonal);

// 'points' without a point equal to the one before it and without corners
// that go on straight ahead.
QList<Point> simplifiedWire(const QList<Point> &points);

// How the program lays a wire again when what it is joined to goes: Hand
// tries a few ways with one or two bends and takes the least bad; Grid
// searches the nodes of half the pin grid for the cheapest way round the
// bodies of parts, the pins and the other wires, and lays it by hand where
// the wire is off that grid or no way is found.
enum class WireRouting { Hand, Grid };

// 'after', where the objects 'moved' of 'before' have been moved, turned or
// mirrored, with the other wires kept on them. A wire end that lay on a pin
// of a moved component or on a moved wire goes where that point went: the
// whole wire is shifted when both its ends went the same way, and the ends
// of other wires on it go with it, else its last
// segment slides along with the end, or one or two bends lead on to where
// it goes. Of the ways to do it that keep the other wires joined to it, the
// one through the ends of the fewest pins it does not end on, then through
// the fewest bodies of parts, then along the fewest pins and
// other wires, then with the fewest corners, then the shortest is taken; a wire going back over itself is cut
// short. An end that also lay on a pin of a component left in place stays
// there. A wire left with no length is removed. A moved pin that touched a
// pin left in place gets a new wire to it, laid the same way, from that pin
// or from a wire there, joining it straight across from the moved pin,
// unless it lands on a wire that already reaches the pin it left. A
// moved pin that was joined to something and lands on the middle of a wire
// cuts the wire there, so that it is joined to it. Where several
// wire ends went together, an end at a point a wire already led still
// passes through stays there, joined to it, unless it would run along
// that wire from there: then it goes too. A moved wire
// whose end lay on a pin of a component left in place or on a bus left in
// place keeps that end there; an end that lay on a wire left as it is, even
// at such a pin, slides along the wires left as they are towards where it
// goes, as the end of a moved segment does (withSegmentMoved()). A bend
// leads back to where the end is held, the least bad way, and a wire led
// after a pin keeps passing through it. An end on a pin
// that is moved as well just goes, whatever else lay there.
// A wire left in place that passes through a pin that goes counts as joined
// to it: it is cut at the pin first, and its two parts follow.
// A pin with nothing on it but the ends of two wires in one line, one going
// on the way the other comes, leaves the line they make as it is, and what
// else lies on the line stays on it: going along the line onto one of them,
// short of its next point, it slides along the line, one wire growing as
// the other shrinks; going anywhere else, it gets a new wire from the line,
// laid the way a pin that touched a pin left in place gets one, unless it
// lands on a wire joined to the line. The far ends of the two wires must
// stay for this.
// A wire that entered a bus at 45 degrees and still ends on one enters it
// at 45 degrees again where it comes to it (see withBusEntry()).
// A label or a no-connect mark not among 'moved' that lay on a pin goes with
// the pin; a label that lay on a wire stays as far along the same segment as
// it was while the wire keeps its number of corners, else where it was while
// still on the wire, else at the nearest point of the wire.
// The ways above are those of WireRouting::Hand. On the grid a wire end
// that goes is led on from the point of the wire, after the last one that
// another wire is joined to, where that costs least, and a wire laid anew
// or led back takes the cheapest way there. A wire with one end going and
// another wire joined to it may instead end at the last such joint, the
// end being reached by a new wire from the nearest point of the net, when
// that is cheaper than going on round. An end going where other wires
// already lead may end on the nearest point of them instead.
Sheet withWiresFollowing(const Sheet &before, const Sheet &after, const QSet<ObjectId> &moved,
                         const Library &library, WireRouting routing = WireRouting::Hand);

// 'sheet' with one end of the wire 'wire' taken to 'to': the last end when
// 'last', else the first. The wire is led there the way withWiresFollowing()
// leads a wire end, keeping the other wires joined to it; it is removed when
// left with no length; labels on it go with it as in withWiresFollowing().
// 'sheet' as it is when it has no such wire.
Sheet withWireEndAt(const Sheet &sheet, const Library &library, const ObjectId &wire, bool last,
                    const Point &to, WireRouting routing = WireRouting::Hand);

// 'sheet' with the segment 'index' (from point 'index' to the next) of the
// wire 'wire' moved by 'delta', across itself only when it is level or
// upright. The segments next to it grow or shrink, or bend to reach it; an
// end of the wire that lies on another wire slides along it as far as the
// move goes its way, even from a pin there, as far as the wires there reach,
// and a bend leads on from where it gets to; else an end joined to a pin,
// a wire or a bus stays, and a bend leads to it. The ends of other wires on the segment
// go along with it; those on a segment next to it that shrinks away from
// them go to the corner it leaves them for; labels on the wire go with it as
// in withWiresFollowing(). 'sheet' as it is when it has no such segment or
// the move is nothing.
Sheet withSegmentMoved(const Sheet &sheet, const Library &library, const ObjectId &wire,
                       int index, const Point &delta, WireRouting routing = WireRouting::Hand);

// withWireEndAt() and withSegmentMoved() for the bus 'bus': it is laid the
// way a wire is, and the wires that enter it go with it as the wires on a
// wire do, entering it at 45 degrees again. Its branches, and the bus it
// branches off (joinedBuses()), are laid as wires joined to it are. A whole
// bus moved in withWiresFollowing() takes the wires that enter it and its
// branches along the same way.
Sheet withBusEndAt(const Sheet &sheet, const Library &library, const ObjectId &bus, bool last,
                   const Point &to, WireRouting routing = WireRouting::Hand);
Sheet withBusSegmentMoved(const Sheet &sheet, const Library &library, const ObjectId &bus,
                          int index, const Point &delta, WireRouting routing = WireRouting::Hand);

} // namespace ppcb
