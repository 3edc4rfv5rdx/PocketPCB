#include "libraryfile.h"

namespace ppcb {

namespace {

// The "type" field of a library file of the given kind.
const char *fileTypeOfKind(LibraryKind kind)
{
    switch (kind) {
    case LibraryKind::Symbol:
        return FILE_TYPE_SYMBOL;
    case LibraryKind::Footprint:
        return FILE_TYPE_FOOTPRINT;
    case LibraryKind::Part:
        return FILE_TYPE_PART;
    }
    return FILE_TYPE_SYMBOL;
}

} // namespace

bool LibraryFile::read(const QString &path, LibraryItem &item, QString &error)
{
    QJsonObject root;
    if (!JsonFile::read(path, root, error))
        return false;

    const QString type = fileTypeOf(root);
    LibraryItem result;
    if (type == QLatin1String(FILE_TYPE_SYMBOL)) {
        result.kind = LibraryKind::Symbol;
    } else if (type == QLatin1String(FILE_TYPE_FOOTPRINT)) {
        result.kind = LibraryKind::Footprint;
    } else if (type == QLatin1String(FILE_TYPE_PART)) {
        result.kind = LibraryKind::Part;
    } else {
        error = withPlace(path, tr("this is not a PocketPCB library file"));
        return false;
    }

    JsonReader reader;
    reader.header(root, fileTypeOfKind(result.kind));
    switch (result.kind) {
    case LibraryKind::Symbol:
        result.symbol = symbolFromJson(root, QString(), reader);
        break;
    case LibraryKind::Footprint:
        result.footprint = footprintFromJson(root, QString(), reader);
        break;
    case LibraryKind::Part:
        result.part = partFromJson(root, QString(), reader);
        break;
    }
    if (!reader.ok()) {
        error = withPlace(path, reader.error());
        return false;
    }
    item = result;
    return true;
}

bool LibraryFile::write(const QString &path, const LibraryItem &item, QString &error)
{
    QJsonObject root;
    switch (item.kind) {
    case LibraryKind::Symbol:
        root = symbolToJson(item.symbol);
        break;
    case LibraryKind::Footprint:
        root = footprintToJson(item.footprint);
        break;
    case LibraryKind::Part:
        root = partToJson(item.part);
        break;
    }
    writeHeader(root, fileTypeOfKind(item.kind));
    return JsonFile::write(path, root, error);
}

} // namespace ppcb
