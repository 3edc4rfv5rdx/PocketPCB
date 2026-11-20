#pragma once

#include "boardupdate.h"

#include <QDialog>

namespace ppcb {

// What updating the board from the schematic would change, a line for
// each change; OK makes them all as one step.
class UpdateBoardDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateBoardDialog(const QList<BoardChange> &changes, QWidget *parent = nullptr);

    // The name of the dialog, of its command and of the step it makes.
    static QString title();
    // Said instead of the dialog when nothing would change.
    static QString nothingToUpdate();
    // "R1: value 1k → 2k2": a change as the list shows it.
    static QString changeText(const BoardChange &change);
};

} // namespace ppcb
