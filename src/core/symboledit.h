#pragma once

#include "library.h"
#include "symbol.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>

namespace ppcb {

// Editing a symbol of the library. Its pins are told by their index in
// Symbol::pins, as the pads of a footprint are (footprintedit.h). Every pin
// stays on PIN_GRID: whatever moves or turns pins moves them by whole
// steps of it, about a point on it.

// The smallest whole number from 1 up that no pin of 'symbol' has.
QString nextPinNumber(const Symbol &symbol);

// The pin whose line passes within 'reach' millimetres of 'at', the one
// drawn last of several; -1 when there is none.
int pinAtPoint(const Symbol &symbol, const QPointF &at, double reach);
// The pins whose line lies wholly inside 'rect' (millimetres).
QSet<int> pinsInside(const Symbol &symbol, const QRectF &rect);
// The rectangle holding the lines of the pins 'pins', of no size for one
// pin of no length; null when there are none.
QRectF pinsBounds(const Symbol &symbol, const QSet<int> &pins);

// The side a pin faces after a quarter turn clockwise on screen.
PinSide sideTurned(PinSide side);
// 'pin' turned a quarter clockwise on screen about 'center': its point and
// its side.
Pin pinTurned(Pin pin, const Point &center);

// 'pin' with the end at its body (pinBodyEnd()) as near 'to' as its line
// lets it: its length the way into the body, never below 0; its point,
// the end a wire meets, stays.
Pin withBodyEndAt(Pin pin, const Point &to);

Symbol withPinsAdded(const Symbol &symbol, const QList<Pin> &pins);
Symbol withPinsMoved(const Symbol &symbol, const QSet<int> &pins, const Point &delta);
Symbol withPinsRotated(const Symbol &symbol, const QSet<int> &pins, const Point &center);
Symbol withoutPins(const Symbol &symbol, const QSet<int> &pins);
// With 'changed' put in the places of the pins 'pins', one for each in the
// order of their indices.
Symbol withPinsReplaced(const Symbol &symbol, const QList<int> &pins, const QList<Pin> &changed);
// 'symbol' with 'piece' put into it, moved by 'at', which must be on
// PIN_GRID: its drawings and pins added after those there, a pin whose
// number is taken given the next free one. An empty symbol takes the
// places of the designator and the value of the piece too.
Symbol withPiece(const Symbol &symbol, const Symbol &piece, const Point &at);
// The same symbol with its origin at 'at', which must be on PIN_GRID:
// everything moved by -at.
Symbol withOriginAt(const Symbol &symbol, const Point &at);
// The middle of the symbol: the centre of its drawings, or of its pins when
// it has none; the origin when it has neither. Not on any grid.
Point symbolMiddle(const Symbol &symbol);

// Why the pins of 'symbol' cannot be in a symbol file, as symbolFromJson()
// would refuse them; empty when they can.
QString pinsProblem(const Symbol &symbol);
// The same for the whole symbol: its pins, and a power symbol's one pin at
// its origin.
QString symbolProblem(const Symbol &symbol);

// The parts of 'library' drawn with the symbol 'name', by name.
QStringList partsUsingSymbol(const Library &library, const QString &name);

} // namespace ppcb
