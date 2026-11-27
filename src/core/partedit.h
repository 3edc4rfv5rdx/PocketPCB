#pragma once

#include "library.h"

#include <QString>
#include <QStringList>

namespace ppcb {

// What the part editor tells of a part, read against 'library': a symbol
// or a footprint it names that the library lacks, and for each footprint
// the pin numbers of the symbol it has no pad for. Empty when every pin
// has a pad in every footprint.
QStringList partMismatches(const Part &part, const Library &library);
// The numbers of the pins of 'symbol' that 'footprint' has no pad for, in
// the order of the pins.
QStringList pinsWithoutPad(const Symbol &symbol, const Footprint &footprint);
// Why 'part' cannot be written into a library file: no symbol or no
// designator prefix; empty when it can.
QString partProblem(const Part &part);
// 'part' with the footprint at 'from' moved to 'to' in its list; the same
// for an index out of the list.
Part withFootprintMoved(const Part &part, int from, int to);

} // namespace ppcb
