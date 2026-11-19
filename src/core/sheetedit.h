#pragma once

#include "paint.h"

#include <QSet>

namespace ppcb {

// What the mouse finds on a sheet and what is done to a selection there.
// A selection is a set of object ids; ids of objects not on the sheet are
// left alone, so a selection may outlive the objects it names.

// True when 'point' lies within 'tolerance' of the line 'polyline' (all in
// millimetres), or of its one point.
bool isNearPolyline(const QPolygonF &polyline, const QPointF &point, double tolerance);

// The object under 'point' (millimetres): one found by its line when within
// 'tolerance' of it, any other when the point is in its rectangle grown by
// 'tolerance'. One found by its line comes first, so that a wire can be
// taken where it reaches a part; then one by its drawing without its texts
// (SheetObjectShape::body), so that the texts of a part beside it do not
// take the click; then the topmost. A null id when nothing is.
ObjectId objectAt(const QList<SheetObjectShape> &shapes, const QPointF &point, double tolerance);

// The objects that lie wholly inside 'rect' (millimetres): a component or
// a power symbol by its drawing alone (SheetObjectShape::body).
QSet<ObjectId> objectsInside(const QList<SheetObjectShape> &shapes, const QRectF &rect);

// The rectangle around the selected objects; empty when none is on the sheet.
QRectF selectionBounds(const QList<SheetObjectShape> &shapes, const QSet<ObjectId> &ids);

// The point of the object 'id' that a drag lands on the grid: a part's own
// point, the point of a wire or a bus nearest to 'near' (millimetres), a
// label's or a mark's point, a drawing's first point. 'near' itself, in
// nanometres, when the sheet has no such object.
Point dragAnchor(const Sheet &sheet, const ObjectId &id, const QPointF &near);

// A text that a click chooses alone and a drag moves while its object
// stays: the designator or the value of a component, the text of a label or
// one place of the name of a bus, whose 'text' is not used. A null owner
// when there is none.
struct TextHit
{
    ObjectId owner;
    ComponentText text = ComponentText::Designator;
    // Which place of a bus's name (Bus::namesAt); 0 for the rest.
    int index = 0;
};
inline bool operator==(const TextHit &a, const TextHit &b)
{
    return a.owner == b.owner && a.text == b.text && a.index == b.index;
}
// The text under 'point' (millimetres): the topmost whose rectangle holds
// the point, the texts of components first. No room is added around it, as
// the rectangle has room enough and a click on the symbol next to it must
// take the component. An empty value is found by its mark (see
// componentTextShown()), when 'library' shows one; an empty designator not
// at all.
TextHit textAt(const Sheet &sheet, const Library &library, const QPointF &point);
// True when 'sheet' has the text 'hit'.
bool hasText(const Sheet &sheet, const TextHit &hit);
// Where the text stands, which a drag lands on the grid, and the point of
// its object; the origin when 'sheet' has no such text.
Point textPoint(const Sheet &sheet, const TextHit &hit);
Point textOwnerPoint(const Sheet &sheet, const TextHit &hit);
// A rectangle surely holding the text, in millimetres; empty when 'sheet'
// has no such text.
QRectF textRect(const Sheet &sheet, const Library &library, const TextHit &hit);
// 'sheet' with the text 'hit' moved by 'delta'; its object stays.
Sheet withTextMoved(const Sheet &sheet, const TextHit &hit, const Point &delta);
// 'sheet' with the bus 'bus' showing its name once more, standing at 'at';
// 'sheet' as it is without such a bus.
Sheet withBusNameAdded(const Sheet &sheet, const ObjectId &bus, const Point &at);
// True when 'hit' is a place of a bus's name that may go: the bus, or a
// bus joined to it (joinedBuses()), shows the name somewhere else too.
bool isBusNameRemovable(const Sheet &sheet, const TextHit &hit);
// 'sheet' without that place of the name; as it is when it may not go.
Sheet withoutBusName(const Sheet &sheet, const TextHit &hit);

// Properties of components. Each returns 'sheet' with the components of
// 'ids' changed; the other objects stay as they are.

// The value set to 'value', without the spaces around it.
Sheet withComponentValue(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &value);
// The net name of the power symbols 'ids' set to 'name', without the spaces
// around it; 'sheet' as it is when that leaves no name.
Sheet withPowerName(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &name);
// The same for the text of the labels 'ids'.
Sheet withLabelText(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &text);
// The same for the name of the buses 'ids' and of the buses joined to
// them (joinedBuses()).
Sheet withBusName(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &name);
// The name a new bus gets: "BUS" and the lowest number no bus of the
// schematic has ("BUS1" first). Only a label joins a net, so the name is a
// caption and may be anything; it is given so that a bus always has one.
QString nextBusName(const Schematic &schematic);
// The text a label placed after one with 'text' is offered: the number at
// its end one more, as wide as it was at least ("D0" gives "D1", "A09"
// gives "A10"); 'text' itself when it ends in no number.
QString nextLabelText(const QString &text);
// The designator set to 'designator', without the spaces around it; an empty
// one leaves the designator as it was.
Sheet withComponentDesignator(const Sheet &sheet, const QSet<ObjectId> &ids,
                              const QString &designator);
Sheet withComponentFootprint(const Sheet &sheet, const QSet<ObjectId> &ids,
                             const QString &footprint);
Sheet withComponentOffBoard(const Sheet &sheet, const QSet<ObjectId> &ids, bool offBoard);
// The comment, without the spaces round it.
Sheet withComponentComment(const Sheet &sheet, const QSet<ObjectId> &ids, const QString &comment);
// Rated power in milliwatts, 0 for none; only components of parts that
// have one (Part::powerMw) take it.
Sheet withComponentPower(const Sheet &sheet, const QSet<ObjectId> &ids, int powerMw,
                         const Library &library);
// True when a component of the schematic other than 'except' has 'designator'.
bool isDesignatorTaken(const Schematic &schematic, const QString &designator,
                       const ObjectId &except);

// The selected objects moved by 'delta'.
Sheet movedObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &delta);
// The selected objects turned by 90 degrees clockwise on screen about
// 'center'. Turning four times gives the sheet back.
Sheet rotatedObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &center);
// The selected objects mirrored left to right about the vertical line
// through 'center'. Mirroring twice gives the sheet back.
Sheet mirroredObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &center);
// The sheet without the selected objects.
Sheet withoutObjects(const Sheet &sheet, const QSet<ObjectId> &ids);
// The no-connect marks on the pins of the components 'ids' of 'sheet' that
// no pin of another component shares: they go when those components go.
QSet<ObjectId> marksOnPinsOf(const Sheet &sheet, const QSet<ObjectId> &ids,
                             const Library &library);

// Segments of lines, wires and buses alike: segment 'index' runs from point
// 'index' to the next. The segment of the line 'points' nearest to 'point'
// (millimetres); -1 when the line has no segment.
int segmentAt(const QList<Point> &points, const QPointF &point);
int segmentAt(const Wire &wire, const QPointF &point);
// The points of the wire or the bus 'line' of 'sheet'; empty when there is none.
QList<Point> linePoints(const Sheet &sheet, const ObjectId &line);
// The sheet without the segment 'index' of the wire or the bus 'line': what
// is left on either side of it stays as two lines, the second with a new id
// (a bus keeps its name on both), and a side with no segment goes. The sheet
// as it is when it has no such segment.
Sheet withoutSegment(const Sheet &sheet, const ObjectId &line, int index);
// A piece of a sheet holding the segment 'index' of the wire or the bus
// 'line' as a line of its own, with the line's id; empty when there is no
// such segment.
Sheet segmentOf(const Sheet &sheet, const ObjectId &line, int index);

// Copying and pasting. A piece of a sheet is a Sheet that holds only some
// objects; its id, title and paper mean nothing.

// The selected objects of 'sheet'.
Sheet objectsOf(const Sheet &sheet, const QSet<ObjectId> &ids);
// The ids of every object of 'sheet'.
QSet<ObjectId> objectIds(const Sheet &sheet);
// 'piece' with a new id for every object.
Sheet withNewIds(const Sheet &piece);
// 'sheet' with the objects of 'piece' added after its own.
Sheet withObjectsAdded(const Sheet &sheet, const Sheet &piece);
// 'piece' with its components numbered on after those of 'schematic' and
// after each other, by the prefix their part has in 'library'. A component
// whose part 'library' lacks keeps its designator.
Sheet numberedOn(const Sheet &piece, const Schematic &schematic, const Library &library,
                 DesignatorStyle style);

// Placing parts.

// The designator prefix of 'part' in 'style': R, VD, DD...
QString designatorPrefix(const Part &part, DesignatorStyle style);
// 'schematic' with its designators switched from style 'from' to 'to': one
// made of its part's prefix in 'from' and a number with whatever follows
// it ("VD3", "VD3a") gets the prefix in 'to' with the same rest. One that
// would repeat a designator kept, or one given before it in the order of
// designatorLess() (R1 and RV1 both become R1 in GOST), gets the first free
// number instead. The others stay: those of parts 'library' lacks, of parts
// whose prefix is the same in both styles, and those not made so.
Schematic withDesignatorStyle(const Schematic &schematic, const Library &library,
                              DesignatorStyle from, DesignatorStyle to);
// The first designator with 'prefix' no component of the schematic has,
// on any sheet: R1, then R2, and a gap left by a deleted R3 is filled.
QString nextDesignator(const Schematic &schematic, const QString &prefix);
// A component of the part 'partName' of 'library', not placed yet: a new id,
// at the origin, with the symbol's text places, the part's value and
// 'footprint'. It stays off the board when the part has no footprints.
Component newComponent(const Library &library, const QString &partName,
                       const QString &footprint);
// 'project' with the part, its symbol and all its footprints taken from
// 'source' where 'project' lacks them. What 'project' has stays as it is:
// the project does not change because the library did.
Library withPartFrom(const Library &project, const Library &source, const QString &partName);
// The same for the symbol 'symbolName' of a power symbol.
Library withSymbolFrom(const Library &project, const Library &source, const QString &symbolName);

} // namespace ppcb
