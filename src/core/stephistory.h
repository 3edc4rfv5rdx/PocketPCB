#pragma once

#include "schematic.h"
#include "wiring.h"

#include <QString>

namespace ppcb {

// A record of the steps made, for finding out what went wrong in one: each
// step is written into a folder as a JSON file of its own, named
// "<number> <step name>.json", with the name of the step, the sheet it was
// made on, the project file, how wires were laid and the whole schematic
// before and after it. The numbers go on from the highest in the folder,
// and only the newest STEP_HISTORY_KEPT files are kept.
constexpr int STEP_HISTORY_KEPT = 100;

// Writes the step 'name' made on the sheet 'page' into 'folder', made when
// missing. False when the folder or the file cannot be written.
bool writeStep(const QString &folder, const QString &name, int page, const QString &projectPath,
               WireRouting routing, const Schematic &before, const Schematic &after);

} // namespace ppcb
