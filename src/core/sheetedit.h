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
