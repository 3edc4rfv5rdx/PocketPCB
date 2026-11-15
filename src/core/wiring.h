#pragma once

#include "library.h"
#include "schematic.h"

namespace ppcb {

// Wires on a sheet: where they connect and how a new one is laid. A wire
// connects to a pin or to another wire only where one of its ends lies
// exactly on the pin's point or on the other wire; wires that only cross
// are not connected.

// Where 'point', given in the symbol's own coordinates, lies on the sheet
// for 'component': mirrored first, then turned, then moved to its place.
Point componentPoint(const Component &component, const Point &point);

// The points wires connect to on the pins of every component of 'sheet'
// whose symbol 'library' has.
QList<Point> pinPoints(const Sheet &sheet, const Library &library);

// True when 'point' lies on 'wire': on one of its segments, ends included.
bool isOnWire(const Wire &wire, const Point &point);

// True when a wire ending at 'point' would connect there: to a pin, or to
// a wire of 'sheet'.
bool isConnectionPoint(const Sheet &sheet, const Library &library, const Point &point);

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

} // namespace ppcb
