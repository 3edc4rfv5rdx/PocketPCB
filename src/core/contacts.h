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
// project's library when it lacks it) on every pin of an off-board
// component that has no wire pad on its end yet and no no-connect mark,
// of the components 'only'
// when it is not empty: a pad for the wire of each pin, whether the wire
// is soldered to it or not is the user's to say. A contact joins its pin's
// point, its body further out along the pin, on the wire that leaves it;
// it is numbered on after the others. Nothing is placed when 'source' and
// the project lack the part.
PlacedContacts withContactsPlaced(const Design &design, const Library &source,
                                  const QSet<ObjectId> &only = {});

// True when 'placement' of 'design' is a wire pad, a contact for the wire
// of another part: its component is of a connector part, on WIRE_PAD_PART
// or one of its kin ("wire-pads-2"). A part on such a footprint that is
// no connector, a potentiometer on the panel, has the pads for its own
// wires and is none.
bool isWirePad(const Placement &placement, const Design &design);
// Where the wire of the wire pad 'placement' goes, by the nets of its
// pads in their order: the pins of off-board components on them
// ("BA1.1, SA1.2"), or the name of a net that has none; each once. Empty
// for a pad on no net.
QString wirePadLabel(const Placement &placement, const QList<Net> &nets,
                     const Schematic &schematic);
// 'design''s board with Placement::padLabel made for every wire pad that
// shows its label, and cleared for the other parts.
Board withWirePadLabels(const Design &design);

} // namespace ppcb
