#pragma once

#include "netlist.h"
#include "project.h"

#include <QSet>

namespace ppcb {

// The part a contact is: a wire pad, a pad on the board a wire is soldered
// to.
extern const char WIRE_PAD_PART[];

// What withContactsPlaced() did.
struct PlacedContacts
{
    Design design;
    // The contacts placed, by id.
    QSet<ObjectId> placed;
};

// 'design' with a contact (WIRE_PAD_PART, taken from 'source' into the
// project's library when it lacks it) at a pin of an off-board component
// on every net that reaches the board — holds a pin of a component on it —
// and has no contact yet: no pin of a connector on the board. The pin is
// the first of the net's off-board ones, among the components 'only' when
// it is not empty; nets of off-board components alone, a battery and its
// switch, are left. A contact joins its pin's point, its body further out
// along the pin, on the wire that leaves it; it is numbered on after the
// others. Nothing is placed when 'source' and the project lack the part.
PlacedContacts withContactsPlaced(const Design &design, const Library &source,
                                  const QSet<ObjectId> &only = {});

// True when 'placement' is a wire pad: its footprint is WIRE_PAD_PART or
// one of its kin ("wire-pads-2").
bool isWirePad(const Placement &placement);
// Where the wire of the wire pad 'placement' goes, by the nets of its
// pads in their order: the pins of off-board components on them
// ("BA1.1, SA1.2"), or the name of a net that has none; each once. Empty
// for a pad on no net.
QString wirePadLabel(const Placement &placement, const QList<Net> &nets,
                     const Schematic &schematic);
// 'design''s board with Placement::padLabel made for every wire pad, and
// cleared for the other parts.
Board withWirePadLabels(const Design &design);

} // namespace ppcb
