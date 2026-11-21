#pragma once

#include "library.h"
#include "project.h"
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
    // first in alphabetical order among them; else its 'tableName'; else
    // "TempN" and a number, given in the order of the nets' first pins,
    // a number giving a name another net has skipped.
    QString name;
    // True when the name is given by a power symbol, a label or the table
    // of nets: it stays when the schematic is edited, where an automatic
    // name may not.
    bool named = false;
    // The name given to it by its pins in the table of nets (see
    // NetByPins); empty when none is.
    QString tableName;
    // The table names of nets joined into it that gave way to 'tableName',
    // whose class is stronger: they are dropped from the project when a
    // class is chosen for it again (see withNetClass()).
    QStringList mergedTableNames;
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
// Each of the table names of 'project' (see NetByPins) goes to the net
// holding the most of its pins, and the one whose first pin comes first
// among equals. A net given several, nets with classes joined by a wire,
// takes the one whose class is the strongest (see strongerClass()), the
// first in alphabetical order among equals.
QList<Net> nets(const Schematic &schematic, const Library &library,
                const Project &project = Project());

// "R1.2, C3.1": the designators and numbers of 'pins'.
QString pinsText(const QList<NetPin> &pins);

// The sheets 'net' is drawn on, in their order: those of its wires, labels
// and power symbols, and those of the components of its pins.
QList<int> netSheets(const Net &net, const Schematic &schematic);

// True when the class 'a' of 'project' is stronger than 'b': its tracks are
// wider. A class not in the project's list is the weakest.
bool strongerClass(const Project &project, const QString &a, const QString &b);

// The net class of 'net' in 'project': the strongest of those chosen for it
// in the table of nets, by its names or by its table name; else the
// strongest given by labels on it; else the power class when a power symbol
// is on it; else the default class. A class not in the project's list
// counts for nothing.
QString netClassOf(const Net &net, const Project &project, const Schematic &schematic);

// 'project' with 'netClass' chosen for 'net', one of 'all', in the table of
// nets, its merged table names dropped: by its name when a power symbol or
// a label gives it; else by its table name, a new one ("NN" and the first
// number no net and no name in 'project' has) when it has none, the pins it
// holds now remembered for it. A net with no name of its own and no pins is
// left as it is.
Project withNetClass(const Project &project, const QList<Net> &all, const Net &net,
                     const QString &netClass);

// 'project' with a lasting name for every net of 'all' that has pins and
// no name a power symbol, a label or the table gives: a new table name
// ("NN" and the first free number), as withNetClass() gives, with no class.
// The table names held keep the pins their nets have now; a name no net
// holds any more is dropped unless a class is kept by it. The board names
// its nets by these, so that they stay when the schematic is edited.
Project withNetsNamedByPins(const Project &project, const QList<Net> &all);

// A net class as edited in the project properties: 'original' is the name
// it had, empty for a class added there.
struct NetClassEdit
{
    QString original;
    NetClass netClass;
};

// True when the table of nets or a label of 'design' gives class 'name'.
bool isNetClassUsed(const Design &design, const QString &name);

// 'design' with 'edits' as its net classes, in that order, and
// 'defaultClass' and 'powerClass' (names in 'edits'). What gave a class
// renamed gives it by its new name; what the table of nets gave a class
// left out goes to the default class, and a label giving one gives none.
// A name that was no class of 'design' is left as it is.
Design withNetClasses(const Design &design, const QList<NetClassEdit> &edits,
                      const QString &defaultClass, const QString &powerClass);

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
