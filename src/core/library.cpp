#include "library.h"

#include "libraryfile.h"

#include <QDir>
#include <QFileInfo>

namespace ppcb {

const char LIBRARY_SYMBOLS_FOLDER[] = "symbols";
const char LIBRARY_FOOTPRINTS_FOLDER[] = "footprints";
const char LIBRARY_PARTS_FOLDER[] = "parts";
const char LIBRARY_FILE_EXT[] = ".json";

namespace {

// Between the content of a library file and the item of one kind.
LibraryItem toLibraryItem(const Symbol &symbol)
{
    LibraryItem item;
    item.kind = LibraryKind::Symbol;
    item.symbol = symbol;
    return item;
}

LibraryItem toLibraryItem(const Footprint &footprint)
{
    LibraryItem item;
    item.kind = LibraryKind::Footprint;
    item.footprint = footprint;
    return item;
}

LibraryItem toLibraryItem(const Part &part)
{
    LibraryItem item;
    item.kind = LibraryKind::Part;
    item.part = part;
    return item;
}

void fromLibraryItem(const LibraryItem &item, Symbol &symbol)
{
    symbol = item.symbol;
}

void fromLibraryItem(const LibraryItem &item, Footprint &footprint)
{
    footprint = item.footprint;
}

void fromLibraryItem(const LibraryItem &item, Part &part)
{
    part = item.part;
}

// The items of one kind: the files of 'subfolder' whose content is 'kind'.
template <typename Item>
QMap<QString, Item> readItems(const QString &folder, const char *subfolder, LibraryKind kind,
                              QStringList &problems)
{
    QMap<QString, Item> items;
    const QDir dir(QDir(folder).filePath(QLatin1String(subfolder)));
    const QStringList names = dir.entryList(
        {QStringLiteral("*") + QLatin1String(LIBRARY_FILE_EXT)}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        const QString path = dir.filePath(name);
        LibraryItem content;
        QString error;
        if (!LibraryFile::read(path, content, error)) {
            problems.append(error);
            continue;
        }
        if (content.kind != kind) {
            problems.append(withPlace(path, LibraryFolder::tr(
                                                "the file does not belong in this folder")));
            continue;
        }
        Item item;
        fromLibraryItem(content, item);
        items.insert(name.chopped(int(qstrlen(LIBRARY_FILE_EXT))), item);
    }
    return items;
}

template <typename Item>
bool writeItems(const QString &folder, const char *subfolder, const QMap<QString, Item> &items,
                QMap<QString, Item> &saved, bool everything, QString &error)
{
    const QString dir = QDir(folder).filePath(QLatin1String(subfolder));
    for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
        const QString path = QDir(dir).filePath(it.key() + QLatin1String(LIBRARY_FILE_EXT));
        const auto savedItem = saved.constFind(it.key());
        if (!everything && savedItem != saved.constEnd() && *savedItem == it.value()
            && QFileInfo::exists(path)) {
            continue;
        }
        // Only the library's own folders are made: the project's folder must
        // already be there.
        if (!QFileInfo(folder).dir().exists()) {
            error = withPlace(dir, LibraryFolder::tr("the project folder is missing"));
            return false;
        }
        if (!JsonFile::makeFolder(dir, error))
            return false;
        if (!LibraryFile::write(path, toLibraryItem(it.value()), error))
            return false;
        saved.insert(it.key(), it.value());
    }
    return true;
}

} // namespace

const Part *Library::findPart(const QString &name) const
{
    const auto it = parts.constFind(name);
    return it == parts.constEnd() ? nullptr : &*it;
}

const Footprint *Library::findFootprint(const QString &name) const
{
    const auto it = footprints.constFind(name);
    return it == footprints.constEnd() ? nullptr : &*it;
}

const Symbol *Library::findSymbol(const QString &name) const
{
    const auto it = symbols.constFind(name);
    return it == symbols.constEnd() ? nullptr : &*it;
}

const Symbol *Library::findSymbolOfPart(const QString &partName) const
{
    const Part *part = findPart(partName);
    return part == nullptr ? nullptr : findSymbol(part->symbol);
}

namespace {

// Puts 'user' over 'items' and notes the names it replaces.
template <typename Item>
void mergeInto(QMap<QString, Item> &items, const QMap<QString, Item> &user,
               QSet<QString> &replaced)
{
    for (auto it = user.constBegin(); it != user.constEnd(); ++it) {
        if (items.contains(it.key()))
            replaced.insert(it.key());
        items.insert(it.key(), it.value());
    }
}

} // namespace

MergedLibrary mergeLibraries(const Library &builtIn, const Library &user)
{
    MergedLibrary merged;
    merged.items = builtIn;
    mergeInto(merged.items.symbols, user.symbols, merged.replacedSymbols);
    mergeInto(merged.items.footprints, user.footprints, merged.replacedFootprints);
    mergeInto(merged.items.parts, user.parts, merged.replacedParts);
    return merged;
}

bool operator==(const Library &a, const Library &b)
{
    return a.symbols == b.symbols && a.footprints == b.footprints && a.parts == b.parts;
}

Library LibraryFolder::read(const QString &folder, QStringList &problems)
{
    Library library;
    library.symbols = readItems<Symbol>(folder, LIBRARY_SYMBOLS_FOLDER, LibraryKind::Symbol,
                                        problems);
    library.footprints = readItems<Footprint>(folder, LIBRARY_FOOTPRINTS_FOLDER,
                                              LibraryKind::Footprint, problems);
    library.parts = readItems<Part>(folder, LIBRARY_PARTS_FOLDER, LibraryKind::Part, problems);
    return library;
}

bool LibraryFolder::write(const QString &folder, const Library &library,
                                Library &saved, bool everything, QString &error)
{
    return writeItems(folder, LIBRARY_SYMBOLS_FOLDER, library.symbols, saved.symbols, everything,
                      error)
           && writeItems(folder, LIBRARY_FOOTPRINTS_FOLDER, library.footprints, saved.footprints,
                         everything, error)
           && writeItems(folder, LIBRARY_PARTS_FOLDER, library.parts, saved.parts, everything,
                         error);
}

} // namespace ppcb
