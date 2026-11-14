#pragma once

#include "footprint.h"
#include "part.h"
#include "symbol.h"

#include <QCoreApplication>
#include <QMap>
#include <QString>
#include <QStringList>

namespace ppcb {

// The folders of a library, the user's or a project's, and the extension of
// its files. A symbol, footprint or part is known by the name of its file
// without the extension ("symbols/resistor.json" is the symbol "resistor").
extern const char LIBRARY_SYMBOLS_FOLDER[];
extern const char LIBRARY_FOOTPRINTS_FOLDER[];
extern const char LIBRARY_PARTS_FOLDER[];
extern const char LIBRARY_FILE_EXT[];

// What a project uses from the library, copied into the project by name,
// so the project does not change when the library does. It may hold items
// nothing uses any more.
struct ProjectLibrary
{
    QMap<QString, Symbol> symbols;
    QMap<QString, Footprint> footprints;
    QMap<QString, Part> parts;

    // Null when the library lacks it. The pointers last until the library
    // is changed.
    const Part *findPart(const QString &name) const;
    const Footprint *findFootprint(const QString &name) const;
    // The symbol of the part 'partName'; null when the library lacks either.
    const Symbol *findSymbolOfPart(const QString &partName) const;
};

bool operator==(const ProjectLibrary &a, const ProjectLibrary &b);
inline bool operator!=(const ProjectLibrary &a, const ProjectLibrary &b) { return !(a == b); }

// The files of a project library: '<folder>/symbols/<name>.json' and so
// on, in the format of the user's library, so they can be copied by hand.
class ProjectLibraryFiles
{
    Q_DECLARE_TR_FUNCTIONS(ppcb::ProjectLibraryFiles)

public:
    // Reads every library file in the folder; a missing folder is an empty
    // library. A file that cannot be read, or holds something else than its
    // folder is for, is left out, and a message for the user about it goes
    // into 'problems'.
    static ProjectLibrary read(const QString &folder, QStringList &problems);
    // Writes the items of 'library' that differ from those in 'saved', all
    // of them when 'everything', and those whose file is missing, making the
    // folders they need inside the project's folder, which must be there.
    // Every item written is put into 'saved'. No file is ever removed. On
    // failure returns false with a message for the user in 'error'.
    static bool write(const QString &folder, const ProjectLibrary &library, ProjectLibrary &saved,
                      bool everything, QString &error);
};

} // namespace ppcb
