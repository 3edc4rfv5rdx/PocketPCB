#pragma once

#include "footprint.h"

#include <QColor>
#include <QList>
#include <QRectF>

class QPainter;

namespace ppcb {

// A picture of parts in three dimensions, seen from above, in front and to
// the right at the angles of an isometric drawing: what a footprint's body
// looks like, and later a whole board.

// A point in millimetres: X across and Y down the page as on the board, Z
// up from its top.
struct Point3
{
    double x = 0;
    double y = 0;
    double z = 0;
};

// One flat face of a solid, its corners going round it, and the way it
// faces out.
struct SolidFace
{
    QList<Point3> corners;
    Point3 normal;
    QColor color;
};

// The faces of a box over 'plan' (millimetres) from 'bottom' to 'top'.
QList<SolidFace> boxFaces(const QRectF &plan, double bottom, double top, const QColor &color);
// A cylinder standing up in the square 'plan', from 'bottom' to 'top'.
QList<SolidFace> uprightFaces(const QRectF &plan, double bottom, double top, const QColor &color);
// A cylinder lying along the longer side of 'plan', as thick as the
// shorter, its underside at 'bottom'.
QList<SolidFace> lyingFaces(const QRectF &plan, double bottom, const QColor &color);

// The faces of 'footprint' on a piece of board 1.6 mm thick round it: the
// board, its pads on the side of the part, and its body; a thin box over
// the drawing of its silkscreen when it has no body.
QList<SolidFace> footprintFaces(const Footprint &footprint);

// Paints the faces that face the viewer, the far ones first, each shaded by
// how it turns to the light, fitted into 'target' (the painter's units).
void paintIsometric(QPainter &painter, const QList<SolidFace> &faces, const QRectF &target);

} // namespace ppcb
