#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>

namespace ppcb {

// A file to write: its name and what it holds.
struct OutputFile
{
    QString name;
    QByteArray data;
};

// The CRC-32 of 'data', as zip files keep it.
quint32 crc32(const QByteArray &data);

// A zip archive of 'files', each compressed with deflate and dated 'when',
// its name in UTF-8.
QByteArray zipArchive(const QList<OutputFile> &files, const QDateTime &when);

// A file in a zip archive as its directory lists it: the name, the CRC-32
// and the size of what it holds unpacked, and where and how it is packed.
struct ZipEntry
{
    QString name;
    quint32 crc = 0;
    quint32 size = 0;
    quint16 method = 0;
    quint32 packedSize = 0;
    quint32 offset = 0;
};
// The files the directory of the zip archive 'archive' lists, in its
// order. False when 'archive' is not a zip archive or is cut short.
bool zipEntries(const QByteArray &archive, QList<ZipEntry> &entries);
// What 'entry' of 'archive' (zipEntries()) holds, unpacked: stored or
// deflated. False when it cannot be unpacked or its CRC-32 is wrong.
bool zipFileData(const QByteArray &archive, const ZipEntry &entry, QByteArray &data);

} // namespace ppcb
