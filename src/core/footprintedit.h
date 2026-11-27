#pragma once

#include "footprint.h"
#include "library.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>

namespace ppcb {

// Editing a footprint of the library. Its pads are told by their index in
// Footprint::pads: the editor's undo steps keep the whole footprint, so an
// index lasts as long as the footprint it was taken from.

// The smallest whole number from 1 up that no pad of 'footprint' has.
QString nextPadNumber(const Footprint &footprint);

// The pad under 'at' (millimetres), the one drawn last of several, within
// 'reach' millimetres of its edge; -1 when there is none.
int padAtPoint(const Footprint &footprint, const QPointF &at, double reach = 0);
// The pads lying wholly inside 'rect' (millimetres).
QSet<int> padsInside(const Footprint &footprint, const QRectF &rect);
// The rectangle holding the pads 'pads'; empty when there are none.
QRectF padsBounds(const Footprint &footprint, const QSet<int> &pads);

// 'footprint' with 'pads' added after its own.
Footprint withPadsAdded(const Footprint &footprint, const QList<Pad> &pads);
// The same with 'pads' moved by 'at' first, as pasted: a number 'footprint'
// has already given the next free one, the same for pads that shared it;
// a pad without a number keeps none.
Footprint withPadsPasted(const Footprint &footprint, const QList<Pad> &pads, const Point &at);
// With the pads 'pads' moved by 'delta'.
Footprint withPadsMoved(const Footprint &footprint, const QSet<int> &pads, const Point &delta);
// With the pads 'pads' turned a quarter clockwise on screen about 'center':
// their places turn, and so do their shapes, width and height swapped.
Footprint withPadsRotated(const Footprint &footprint, const QSet<int> &pads, const Point &center);
// The same for loose pads, as a row being placed.
QList<Pad> padsRotated(const QList<Pad> &pads, const Point &center);
// Without the pads 'pads'.
Footprint withoutPads(const Footprint &footprint, const QSet<int> &pads);
// With 'changed' put in the places of the pads 'pads', one for each in the
// order of their indices.
Footprint withPadsReplaced(const Footprint &footprint, const QList<int> &pads,
                           const QList<Pad> &changed);
// The same footprint with its origin at 'at': everything moved by -at, so
// that the part is held by that point on the board and turns about it.
Footprint withOriginAt(const Footprint &footprint, const Point &at);
// The middle of the part: the centre of the drawing of its body on the
// silkscreen, or of its pads when it has none; the origin when it has
// neither. Not on any grid.
Point footprintMiddle(const Footprint &footprint);

// A pad as the rules check measures it: a stroke from 'from' to 'to',
// 'radius' round it; a rectangle is taken as the oval it holds.
struct PadStroke
{
    Point from;
    Point to;
    Coord radius = 0;
};
PadStroke padStroke(const Pad &pad);
// The copper between 'a' and 'b', edge to edge as padStroke() takes them;
// 0 for pads that touch or overlap.
Coord padsApart(const Pad &a, const Pad &b);

// Two pads of a footprint nearer than a clearance: their indices, the gap
// between their copper, and the point between them.
struct PadGap
{
    int first = -1;
    int second = -1;
    Coord gap = 0;
    Point at;
};

bool operator==(const PadGap &a, const PadGap &b);

// The pads of 'footprint' whose copper on a side they share comes nearer
// than 'clearance', in the order of their indices. Pads of one number are
// one pin and may touch.
QList<PadGap> padsTooNear(const Footprint &footprint, Coord clearance);

// Where the body of 'footprint' stands, seen from above, in millimetres:
// its size about the middle of the part (footprintMiddle()), or the
// drawing of the silkscreen; a square for an upright cylinder, the circle
// in it. Null with no body, or with no size and no drawing.
QRectF bodyPlan(const Footprint &footprint);

// The parts of 'library' that list the footprint 'name', by name.
QStringList partsUsingFootprint(const Library &library, const QString &name);
// 'part' listing the footprint 'to' in the place of 'from'.
Part withFootprintRenamed(const Part &part, const QString &from, const QString &to);

// Why 'pad' cannot be in a footprint file, as footprintFromJson() would
// refuse it; empty when it can.
QString padProblem(const Pad &pad);

// How a row of pads is laid out. All go down the page from pad 1, as the
// pin headers and the DIP packages of the library do.
enum class PadRowKind {
    // One column.
    Single,
    // Two columns, numbered across: 1 and 2 side by side, then 3 and 4.
    Double,
    // Two columns, numbered around: down the left one, up the right one.
    Dip,
};

struct PadRow
{
    PadRowKind kind = PadRowKind::Single;
    // How many pads in all; even for two columns.
    int count = 1;
    // From one pad to the next down a column, and from column to column.
    Coord pitch = 2540000;
    Coord spacing = 7620000;
    int firstNumber = 1;
    // The first pad of the row square, telling it on the board.
    bool firstSquare = true;
};

// The pads of 'row', each as 'like' but for its place and number, with the
// first at the origin; none for a count below 1, or an odd one for two
// columns.
QList<Pad> padRow(const PadRow &row, const Pad &like);

} // namespace ppcb
