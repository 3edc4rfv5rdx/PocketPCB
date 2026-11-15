#pragma once

#include "paint.h"

#include <QSet>

namespace ppcb {

// What the mouse finds on a sheet and what is done to a selection there.
// A selection is a set of object ids; ids of objects not on the sheet are
// left alone, so a selection may outlive the objects it names.

// The object under 'point' (millimetres), the topmost when several are:
// one found by its line when within 'tolerance' of it, any other when the
// point is in its rectangle grown by 'tolerance'. A null id when nothing is.
ObjectId objectAt(const QList<SheetObjectShape> &shapes, const QPointF &point, double tolerance);

// The objects that lie wholly inside 'rect' (millimetres).
QSet<ObjectId> objectsInside(const QList<SheetObjectShape> &shapes, const QRectF &rect);

// The rectangle around the selected objects; empty when none is on the sheet.
QRectF selectionBounds(const QList<SheetObjectShape> &shapes, const QSet<ObjectId> &ids);

// The point of the object 'id' that a drag lands on the grid: a part's own
// point, the point of a wire or a bus nearest to 'near' (millimetres), a
// label's or a mark's point, a drawing's first point. 'near' itself, in
// nanometres, when the sheet has no such object.
Point dragAnchor(const Sheet &sheet, const ObjectId &id, const QPointF &near);

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

// Segments of wires: segment 'index' runs from point 'index' to the next.
// The segment of 'wire' nearest to 'point' (millimetres); -1 when the wire
// has no segment.
int segmentAt(const Wire &wire, const QPointF &point);
// The sheet without the segment 'index' of the wire 'wire': what is left on
// either side of it stays as two wires, the second with a new id, and a side
// with no segment goes. The sheet as it is when it has no such segment.
Sheet withoutSegment(const Sheet &sheet, const ObjectId &wire, int index);
// A piece of a sheet holding the segment 'index' of the wire 'wire' as a
// wire of its own, with the wire's id; empty when there is no such segment.
Sheet segmentOf(const Sheet &sheet, const ObjectId &wire, int index);

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

} // namespace ppcb
