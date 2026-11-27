#pragma once

#include "library.h"

#include <QString>
#include <QStringList>

namespace ppcb {

// What the part editor tells of a part, read against 'library': a symbol
// or a footprint it names that the library lacks, pin numbers in two of its
// units, and for each footprint the pin numbers of its units it has no pad
// for and the numbers of its pads no pin has. Empty when pins and pads
// match in every footprint.
QStringList partMismatches(const Part &part, const Library &library);
// The numbers of the pins of 'symbol' that 'footprint' has no pad for, in
// the order of the pins.
QStringList pinsWithoutPad(const Symbol &symbol, const Footprint &footprint);
// The numbers of the pads of 'footprint' that no pin of 'symbol' has, in
// the order of the pads; pads without a number, a screw's, left out.
QStringList padsWithoutPin(const Symbol &symbol, const Footprint &footprint);
// Why 'part' cannot be written into a library file: no symbol, no
// designator prefix in either style, or no name in CODE_LANGUAGE; empty
// when it can.
QString partProblem(const Part &part);
// 'part' as it is written: a prefix missing in one style given that of the
// other.
Part withBothPrefixes(const Part &part);
// 'part' with the footprint at 'from' moved to 'to' in its list; the same
// for an index out of the list.
Part withFootprintMoved(const Part &part, int from, int to);

} // namespace ppcb
