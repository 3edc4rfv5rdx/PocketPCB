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

} // namespace ppcb
