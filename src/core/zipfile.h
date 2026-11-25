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

} // namespace ppcb
