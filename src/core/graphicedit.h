#pragma once

#include "graphic.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSet>

#include <optional>

namespace ppcb {

// Editing the drawings of a footprint's silkscreen or of a symbol:
// a list of Graphic, each told by its index in it.

// A text drawn on the silkscreen of a footprint at first, its height.
constexpr Coord DEFAULT_TEXT_HEIGHT = 1000000;

// 'point' turned a quarter clockwise on screen about 'center'.
Point turnedQuarter(const Point &point, const Point &center);
// 'graphic' moved by 'delta'.
Graphic graphicMoved(Graphic graphic, const Point &delta);

// The drawing under 'at' (millimetres), the one drawn last of several:
// within 'reach' of its line, or inside its rectangle grown by 'reach' for
// one found by its area (a text, a filled polygon); -1 when there is none.
int graphicAt(const QList<Graphic> &graphics, const QPointF &at, double reach);
// The smallest circle of 'graphics' that 'at' (millimetres) lies inside;
// -1 for none. Taken by a click inside it, after everything else there.
int circleAround(const QList<Graphic> &graphics, const QPointF &at);
// The drawings lying wholly inside 'rect' (millimetres).
QSet<int> graphicsInside(const QList<Graphic> &graphics, const QRectF &rect);
// The rectangle holding the drawings 'chosen'; null when there are none.
QRectF graphicsBounds(const QList<Graphic> &graphics, const QSet<int> &chosen);

QList<Graphic> withGraphicsMoved(const QList<Graphic> &graphics, const QSet<int> &chosen,
                                 const Point &delta);
// Turned a quarter clockwise on screen about 'center'. A text keeps
// reading across: only its place turns.
QList<Graphic> withGraphicsRotated(const QList<Graphic> &graphics, const QSet<int> &chosen,
                                   const Point &center);
QList<Graphic> withoutGraphics(const QList<Graphic> &graphics, const QSet<int> &chosen);

// The corners of 'graphic' that can be dragged one by one: the ends of a
// line, the four corners of a rectangle, the corners of a polygon; none
// for the others.
QList<Point> graphicCorners(const Graphic &graphic);
// The corner of 'graphic' within 'reach' of 'at' (millimetres); -1 for none.
int cornerAt(const Graphic &graphic, const QPointF &at, double reach);
// 'graphic' with its corner 'corner' (graphicCorners()) at 'to'; a
// rectangle keeps its corner across from it.
Graphic withCornerMoved(const Graphic &graphic, int corner, const Point &to);

// 'to' moved, the least it can be, onto a line from 'from' across, up and
// down or at 45 degrees.
Point keptStraightOrDiagonal(const Point &from, const Point &to);

// The drawings begun by a click at 'first'.
Graphic lineBetween(const Point &from, const Point &to);
Graphic rectBetween(const Point &a, const Point &b);
// The oval in the rectangle of 'a' and 'b' (GraphicKind::Oval).
Graphic ovalBetween(const Point &a, const Point &b);
// A circle about 'center' through 'through'.
Graphic circleThrough(const Point &center, const Point &through);
// The arc from 'start' through 'middle' to 'end', whichever way round that
// goes; its centre to the nanometre, its angles in whole degrees. None when
// the three lie on one line or two of them meet.
std::optional<Graphic> arcThrough(const Point &start, const Point &middle, const Point &end);
Graphic polygonOf(const QList<Point> &points);
Graphic textAt(const Point &at, const QString &text);

} // namespace ppcb
