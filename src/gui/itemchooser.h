#pragma once

#include "library.h"
#include "libraryfile.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QList>
#include <QString>

class QWidget;

namespace ppcb {

// Asks for an item of a library from a list of its names, with a search
// field over it and the drawing of the one chosen beside it: a footprint,
// a symbol, or a part with its symbol and first footprint.
class ItemChooser
{
    Q_DECLARE_TR_FUNCTIONS(ppcb::ItemChooser)

public:
    // The name chosen in a dialog titled 'title', whose button 'accept'
    // takes it; empty when none is. With several 'kinds' a switch above the
    // list goes between them, starting at 'kind', which is left at the one
    // shown last; with one, the list holds that kind. 'current' is chosen
    // at first when listed.
    static QString choose(QWidget *parent, const QString &title, const Library &library,
                          const QList<LibraryKind> &kinds, LibraryKind &kind,
                          QDialogButtonBox::StandardButton accept,
                          const QString &current = QString());
    // "Footprints", "Symbols", "Parts".
    static QString kindTitle(LibraryKind kind);
};

} // namespace ppcb
