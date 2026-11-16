#pragma once

#include "footprint.h"
#include "part.h"
#include "symbol.h"

#include <QCoreApplication>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

namespace ppcb {

// The folders of a library — the built-in one, the user's or a project's —
// and the extension of its files. A symbol, footprint or part is known by the name of its file
// without the extension ("symbols/resistor.json" is the symbol "resistor").
extern const char LIBRARY_SYMBOLS_FOLDER[];
extern const char LIBRARY_FOOTPRINTS_FOLDER[];
extern const char LIBRARY_PARTS_FOLDER[];
extern const char LIBRARY_FILE_EXT[];

// The content of one library folder, by name. A project keeps one of its
// own (Design::library): what it uses, copied in so that it does not change
// when the user's library does; it may hold items nothing uses any more.
struct Library
{
    QMap<QString, Symbol> symbols;
    QMap<QString, Footprint> footprints;
    QMap<QString, Part> parts;

    // Null when the library lacks it. The pointers last until the library
    // is changed.
    const Part *findPart(const QString &name) const;
    const Footprint *findFootprint(const QString &name) const;
    const Symbol *findSymbol(const QString &name) const;
    // The symbol of the part 'partName'; null when the library lacks either.
    const Symbol *findSymbolOfPart(const QString &partName) const;
};

bool operator==(const Library &a, const Library &b);
inline bool operator!=(const Library &a, const Library &b) { return !(a == b); }

// The built-in library with the user's over it: a file of the user's
// replaces the built-in one of the same kind and name.
struct MergedLibrary
{
    Library items;
    // The names the user's library replaces, by kind.
    QSet<QString> replacedSymbols;
    QSet<QString> replacedFootprints;
    QSet<QString> replacedParts;
};

MergedLibrary mergeLibraries(const Library &builtIn, const Library &user);

// The files of a library folder: '<folder>/symbols/<name>.json' and so on.
// A project's library has the format of the user's, so files can be copied
// between them by hand.
class LibraryFolder
{
    Q_DECLARE_TR_FUNCTIONS(ppcb::LibraryFolder)

public:
    // Reads every library file in the folder; a missing folder is an empty
    // library. A file that cannot be read, or holds something else than its
    // folder is for, is left out, and a message for the user about it goes
    // into 'problems'.
    static Library read(const QString &folder, QStringList &problems);
    // Writes the items of 'library' that differ from those in 'saved', all
    // of them when 'everything', and those whose file is missing, making the
    // folders they need inside the folder that holds 'folder', which must be
    // there: it is the project's.
    // Every item written is put into 'saved'. No file is ever removed. On
    // failure returns false with a message for the user in 'error'.
    static bool write(const QString &folder, const Library &library, Library &saved,
                      bool everything, QString &error);
};

} // namespace ppcb
