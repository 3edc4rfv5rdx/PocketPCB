#pragma once

#include "library.h"
#include "schematic.h"

#include <QMap>
#include <QPointF>
#include <QSet>

namespace ppcb {

// The nets of a schematic: what is joined, on every sheet.
//
// On a sheet a wire joins a wire one of whose ends lies on it, and every pin
// of a component and every power symbol whose point lies on it; pins and
// power symbols at one point are joined; a label joins the wires it lies on
// and the pins at its point. On all sheets together, labels and power
// symbols of one name are one net. Buses and no-connect marks join nothing.

// A pin of a component on a net.
struct NetPin
{
    ObjectId component;
    // The component's designator and the pin's number: "R1", "2".
    QString designator;
    QString number;
};

bool operator==(const NetPin &a, const NetPin &b);

struct Net
{
    // The name of a power symbol on it, else the text of a label on it, the
    // first in alphabetical order among them; else "N" and a number, given
    // in the order of the nets' first pins.
    QString name;
    // True when the name is given by a power symbol or a label.
    bool named = false;
    // The other names given to it, in alphabetical order: a net with two
    // names is a mistake (see ERC).
    QStringList otherNames;
    // Its pins, in the order of designators and numbers ("R2" before "R10").
    QList<NetPin> pins;
    // What it is drawn with, by the index of the sheet: wires, labels and
    // power symbols.
    QMap<int, QSet<ObjectId>> objects;
};

// The nets of 'schematic', with the symbols of its components and power
// symbols from 'library': first the named ones, then the others, each in
// the order of their names. A wire joined to nothing is a net of its own.
QList<Net> nets(const Schematic &schematic, const Library &library);

// True when the pin 'number' of the component 'component' is on 'net'.
bool netHasPin(const Net &net, const ObjectId &component, const QString &number);

// The index in 'nets' of the net under 'point' (millimetres) on sheet
// 'sheet' of 'schematic', drawn with 'library': of a wire within
// 'tolerance' of it; else of the line of a pin; else of a label or a power
// symbol whose rectangle, grown by 'tolerance', holds it. -1 when there is
// none, or what is there is on no net (a pin joined to nothing).
int netAt(const QList<Net> &nets, const Schematic &schematic, const Library &library, int sheet,
          const QPointF &point, double tolerance);

// True when 'a' comes before 'b' with the numbers in them read as numbers:
// "R2" before "R10".
bool naturalLess(const QString &a, const QString &b);

} // namespace ppcb
