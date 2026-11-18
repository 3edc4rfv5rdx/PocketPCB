#pragma once

#include "library.h"
#include "netlist.h"
#include "project.h"
#include "schematic.h"

#include <QList>
#include <QSet>
#include <QString>

namespace ppcb {

// The electrical rules check of a schematic: what is likely a mistake.

enum class ErcKind {
    // A component whose part has a value (not Part::noValue) with none given.
    EmptyValue,
    // A designator two or more components have; one problem for each of them.
    DuplicateDesignator,
    // A pin of a component joined to no other pin, with no name on its net
    // and no no-connect mark.
    UnconnectedPin,
    // A no-connect mark on no pin of a component, or on a pin that is joined
    // to another pin or to a named net.
    MisplacedNoConnect,
    // A label that lies on no wire and no pin.
    LooseLabel,
    // An end of a wire that lies on no pin, no other wire, no bus and no
    // label: a piece of wire left behind.
    LooseWireEnd,
    // A stretch of wire lying along another, or along the same wire going
    // back over itself: one of them is likely a piece left behind. One
    // problem for each two segments, marked at the middle of the stretch
    // and about the wire that comes later.
    OverlappingWires,
    // Wires closing a loop: a segment between two points the wires already
    // lead between another way. Marked at the middle of the segment that
    // closes it, about its wire. Wires lying on each other are left to
    // OverlappingWires.
    WireLoop,
    // A component with two pins, both on one net: shorted.
    ShortedComponent,
    // A net given two or more names by its labels and power symbols.
    TwoNames,
    // A name of the table of nets (see NetByPins) no net holds a pin of any
    // more: its class is kept for nothing.
    LostTableName,
    // A label or a power symbol giving a net a name that the table of nets
    // gives another net.
    NameOfAnotherNet,
};

struct ErcProblem
{
    ErcKind kind = ErcKind::EmptyValue;
    // The sheet it is on and the point to mark there; -1 for one of the
    // project as a whole, which has no point.
    int sheet = -1;
    Point at;
    // What to choose on the sheet to show it.
    ObjectId object;
    // What it is about, for the message: a designator ("R1"), a pin
    // ("R1.2"), a label's text or the names of a net ("VCC, +5V").
    QString subject;
};

bool operator==(const ErcProblem &a, const ErcProblem &b);

// The problems of 'schematic' drawn with 'library', 'all' being its nets
// (see nets()) in 'project': sheet by sheet, in the order of the kinds
// above, then those of the project as a whole.
QList<ErcProblem> checkSchematic(const Schematic &schematic, const Library &library,
                                 const Project &project, const QList<Net> &all);

} // namespace ppcb
