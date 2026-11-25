#include "zipfile.h"

#include <algorithm>
#include <array>

namespace ppcb {

namespace {

const quint32 LOCAL_HEADER = 0x04034b50;
const quint32 CENTRAL_HEADER = 0x02014b50;
const quint32 END_OF_DIRECTORY = 0x06054b50;
// Version 2.0, enough for deflate.
const quint16 ZIP_VERSION = 20;
const quint16 METHOD_DEFLATE = 8;
// The name is in UTF-8.
const quint16 FLAG_UTF8 = 0x0800;
const quint32 CRC_POLYNOMIAL = 0xedb88320u;
const int FIRST_DOS_YEAR = 1980;
// What qCompress() puts around a raw deflate stream: the length before it
// and the zlib header, then the zlib checksum after it.
const int QCOMPRESS_HEAD = 4 + 2;
const int QCOMPRESS_TAIL = 4;
const int BEST_COMPRESSION = 9;

void put16(QByteArray &out, quint16 value)
{
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
}

void put32(QByteArray &out, quint32 value)
{
    put16(out, quint16(value & 0xffff));
    put16(out, quint16(value >> 16));
}

quint16 dosTime(const QTime &time)
{
    return quint16((time.hour() << 11) | (time.minute() << 5) | (time.second() / 2));
}

quint16 dosDate(const QDate &date)
{
    const int year = std::max(date.year(), FIRST_DOS_YEAR) - FIRST_DOS_YEAR;
    return quint16((year << 9) | (date.month() << 5) | date.day());
}

// 'data' as a raw deflate stream.
QByteArray deflated(const QByteArray &data)
{
    const QByteArray compressed = qCompress(data, BEST_COMPRESSION);
    return compressed.mid(QCOMPRESS_HEAD, compressed.size() - QCOMPRESS_HEAD - QCOMPRESS_TAIL);
}

} // namespace

quint32 crc32(const QByteArray &data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> values{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? CRC_POLYNOMIAL ^ (c >> 1) : c >> 1;
            values[n] = c;
        }
        return values;
    }();
    quint32 crc = 0xffffffffu;
    for (const char byte : data)
        crc = table[(crc ^ quint8(byte)) & 0xff] ^ (crc >> 8);
    return crc ^ 0xffffffffu;
}

QByteArray zipArchive(const QList<OutputFile> &files, const QDateTime &when)
{
    const quint16 time = dosTime(when.time());
    const quint16 date = dosDate(when.date());
    QByteArray archive;
    QByteArray directory;
    for (const OutputFile &file : files) {
        const QByteArray name = file.name.toUtf8();
        const QByteArray body = deflated(file.data);
        const quint32 crc = crc32(file.data);
        const quint32 offset = quint32(archive.size());
        // The fields both headers share, from the version needed on.
        QByteArray common;
        put16(common, ZIP_VERSION);
        put16(common, FLAG_UTF8);
        put16(common, METHOD_DEFLATE);
        put16(common, time);
        put16(common, date);
        put32(common, crc);
        put32(common, quint32(body.size()));
        put32(common, quint32(file.data.size()));
        put16(common, quint16(name.size()));
        put16(common, 0); // no extra field

        put32(archive, LOCAL_HEADER);
        archive.append(common);
        archive.append(name);
        archive.append(body);

        put32(directory, CENTRAL_HEADER);
        put16(directory, ZIP_VERSION); // made by
        directory.append(common);
        put16(directory, 0); // no comment
        put16(directory, 0); // disk
        put16(directory, 0); // internal attributes
        put32(directory, 0); // external attributes
        put32(directory, offset);
        directory.append(name);
    }
    const quint32 directoryOffset = quint32(archive.size());
    archive.append(directory);
    put32(archive, END_OF_DIRECTORY);
    put16(archive, 0); // this disk
    put16(archive, 0); // the disk of the directory
    put16(archive, quint16(files.size()));
    put16(archive, quint16(files.size()));
    put32(archive, quint32(directory.size()));
    put32(archive, directoryOffset);
    put16(archive, 0); // no comment
    return archive;
}

} // namespace ppcb
