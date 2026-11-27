#pragma once

#include "library.h"

namespace ppcb {

// The content of one library file. Only the member named by 'kind' is
// meaningful; the others keep their defaults.
struct LibraryItem
{
    LibraryKind kind = LibraryKind::Symbol;
    Symbol symbol;
    Footprint footprint;
    Part part;
};

// Library files on disk: one symbol, footprint or part per file. On failure
// both functions return false and put a message for the user into 'error'.
class LibraryFile
{
    Q_DECLARE_TR_FUNCTIONS(ppcb::LibraryFile)

public:
    // The kind is taken from the file itself. 'item' is changed only on success.
    static bool read(const QString &path, LibraryItem &item, QString &error);
    static bool write(const QString &path, const LibraryItem &item, QString &error);
};

} // namespace ppcb
