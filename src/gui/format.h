#pragma once

#include "units.h"

#include <QString>

namespace ppcb {

// Millimetres with exactly three decimals, in the current locale: "2.540".
// For values that change while the user watches (cursor position).
QString formatMmFixed(Coord nm);

// Millimetres with trailing zeros removed: "2.54", "10.16", "5".
// For values shown in lists (grid steps, widths).
QString formatMmShort(Coord nm);

// "mm", the unit after a length, in the current language.
QString mmUnit();
// "symbol" and "footprints", lower case, for what a part has.
QString symbolWord();
QString footprintsWord();

// The file of the project's output, named after the project with 'suffix'
// and 'extension' ("pdf"), in its output folder (DesignFiles::outputPath()),
// the folder made when missing. Empty, with 'problem' set to what to tell
// the user, when the project was never saved or the folder cannot be made.
QString projectOutputFile(const QString &projectPath, const QString &suffix,
                          const QString &extension, QString &problem);
// What to tell after writing the file 'path', or failing to.
QString writtenMessage(const QString &path);
QString cannotWriteMessage(const QString &path);

} // namespace ppcb
