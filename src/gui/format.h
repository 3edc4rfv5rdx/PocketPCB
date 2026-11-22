#pragma once

#include "units.h"

#include <QString>

class QDoubleSpinBox;
class QWidget;

namespace ppcb {

// Millimetres with exactly three decimals, in the current locale: "2.540".
// For values that change while the user watches (cursor position).
QString formatMmFixed(Coord nm);

// Millimetres with trailing zeros removed: "2.54", "10.16", "5".
// For values shown in lists (grid steps, widths).
QString formatMmShort(Coord nm);
// The same with the unit: "3.2 mm".
QString formatMmShortUnit(Coord nm);

// "X 12.700   Y 5.080 mm": a point, as the status bar shows the cursor.
QString formatPosition(Coord x, Coord y);

// A length typed in millimetres, with a point or a comma before the
// decimals; false for anything else or a length out of range.
bool parseMm(const QString &text, Coord &nm);

// A box to type a length 'nm' into, in millimetres, from 'minMm' to 'maxMm',
// its arrows going by 'stepMm'; mmSizeOf() reads it.
QDoubleSpinBox *mmSizeBox(Coord nm, double minMm, double maxMm, double stepMm, QWidget *parent);
// The length in 'box': the one it was given while what it shows is that
// one rounded, so that a length finer than the decimals is not lost unseen.
Coord mmSizeOf(const QDoubleSpinBox *box);

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
// The name of the step that locks parts, or unlocks them, on the sheet
// and on the board; and what is said when a locked part would move.
QString lockPartsName(bool locked);
// "Lock" or "Unlock": the menu item that does it to what is chosen.
QString lockName(bool locked);
QString partLockedMessage();

// What to tell after writing the file 'path', or failing to.
QString writtenMessage(const QString &path);
QString cannotWriteMessage(const QString &path);

} // namespace ppcb
