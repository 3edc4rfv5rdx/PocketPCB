#pragma once

#include "footprint.h"
#include "symbol.h"

#include <QColor>
#include <QPointF>
#include <QRectF>

class QPainter;

namespace ppcb {

// Drawing of library objects. The painter works in millimetres, the unit of
// the canvas scene; the model's nanometres are converted here.

// Every colour of the drawing, in one place.
namespace paintcolor {
inline QColor background() { return QColor(255, 255, 255); }
inline QColor symbol() { return QColor(132, 0, 0); }
inline QColor pinText() { return QColor(0, 100, 100); }
// "REF" and "VALUE" marks and the origin cross.
inline QColor placeholder() { return QColor(140, 140, 140); }
inline QColor silkscreen() { return QColor(0, 132, 132); }
inline QColor padBoth() { return QColor(190, 150, 0); }
inline QColor padFront() { return QColor(200, 50, 50); }
inline QColor padBack() { return QColor(50, 90, 200); }
inline QColor padText() { return QColor(255, 255, 255); }
} // namespace paintcolor

QPointF toMm(const Point &point);

// The end of a pin's line that touches the symbol body.
Point pinBodyEnd(const Pin &pin);

// Rectangles that hold everything paintSymbol() / paintFootprint() draw.
// Never empty, so a view can always be fitted to them.
QRectF symbolBounds(const Symbol &symbol);
QRectF footprintBounds(const Footprint &footprint);

void paintSymbol(QPainter &painter, const Symbol &symbol);
void paintFootprint(QPainter &painter, const Footprint &footprint);

} // namespace ppcb
