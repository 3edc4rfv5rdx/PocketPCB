#pragma once

#include "sheetedit.h"

#include <QDialog>

class QListWidget;

namespace ppcb {

// The parts of the project that the library has otherwise, each with what
// differs and a box to tick for it to be updated; all ticked at first.
class UpdatePartsDialog : public QDialog
{
    Q_OBJECT

public:
    // 'library' gives the names the parts are shown by.
    UpdatePartsDialog(const QList<PartDifference> &differences, const Library &library,
                      QWidget *parent = nullptr);

    // The name of the dialog, of its command and of the step it makes.
    static QString title();
    // Said instead of the dialog when no part differs.
    static QString nothingToUpdate();

    // The library names of the parts ticked.
    QStringList chosenParts() const;

private:
    QListWidget *m_list = nullptr;
};

} // namespace ppcb
