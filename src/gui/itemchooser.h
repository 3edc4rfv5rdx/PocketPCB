#pragma once

#include "library.h"
#include "libraryfile.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QList>
#include <QString>

#include <functional>

class QWidget;

namespace ppcb {

// Asks for an item of a library from a list of its names, with a search
// field over it and the drawing of the one chosen beside it: a footprint,
// a symbol, or a part with its symbol and first footprint.
class ItemChooser
{
    Q_DECLARE_TR_FUNCTIONS(ppcb::ItemChooser)

public:
    // Which library the list shows: the one given, the built-in one with
    // the user's over it, or either alone.
    enum class Source { All, BuiltIn, User };
    // What a chooser may offer beyond the list.
    struct Options
    {
        // With both, a switch over the list between Source::All and these.
        const Library *builtIn = nullptr;
        const Library *user = nullptr;
        // Shown at first, and the one shown last afterwards.
        Source source = Source::All;
        // With it, Delete under the list, while the user's library is
        // shown: it is called with the kind and the name chosen, and true
        // when it deleted that, the lists having been read again.
        std::function<bool(LibraryKind, const QString &)> remove;
    };

    // The name chosen in a dialog titled 'title', whose button 'accept'
    // takes it; empty when none is. With several 'kinds' a switch above the
    // list goes between them, starting at 'kind', which is left at the one
    // shown last; with one, the list holds that kind. 'current' is chosen
    // at first when listed. 'options' may add a switch of the library and
    // Delete, and tells which library the name is of.
    static QString choose(QWidget *parent, const QString &title, const Library &library,
                          const QList<LibraryKind> &kinds, LibraryKind &kind,
                          QDialogButtonBox::StandardButton accept,
                          const QString &current = QString(), Options *options = nullptr);
    // "All", "Built-in", "User".
    static QString sourceTitle(Source source);
    // "Footprints", "Symbols", "Parts".
    static QString kindTitle(LibraryKind kind);
};

} // namespace ppcb
