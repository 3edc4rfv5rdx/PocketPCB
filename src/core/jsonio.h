#pragma once

#include "objectid.h"
#include "point.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace ppcb {

// Version of the file format this program writes and the newest it reads.
constexpr int FORMAT_VERSION = 1;

// Values of the "type" field: what a file is.
extern const char FILE_TYPE_PROJECT[];
extern const char FILE_TYPE_SCHEMATIC[];
extern const char FILE_TYPE_BOARD[];
extern const char FILE_TYPE_SYMBOL[];
extern const char FILE_TYPE_FOOTPRINT[];
extern const char FILE_TYPE_PART[];

// Writing: sizes go to the files in millimetres.
QJsonValue mmToJson(Coord nm);
QJsonArray pointToJson(const Point &point);
QJsonValue idToJson(const ObjectId &id);
// Adds the fields every file starts with: "type", "format" and "app" (the
// program version that wrote the file; for people, never read back).
void writeHeader(QJsonObject &root, const char *fileType);

// Reads values out of parsed JSON and remembers the first thing that was
// wrong. Every getter takes the object, the key and 'where': the path of the
// object inside the file ("sheets[2].wires[5]", empty for the root), used
// only for the message. After a failure getters return zero values, so the
// caller can read on and check ok() once at the end.
class JsonReader
{
    Q_DECLARE_TR_FUNCTIONS(ppcb::JsonReader)

public:
    bool ok() const { return m_error.isEmpty(); }
    // Empty while ok(); otherwise "<where>: <what is wrong>".
    QString error() const { return m_error; }

    // Records a failure found by the caller. Only the first one is kept.
    void fail(const QString &where, const QString &message);

    // Checks "type" and "format" of a file's root object.
    void header(const QJsonObject &root, const char *expectedFileType);

    // All of these require the key to be present and of the right kind.
    Coord mm(const QJsonObject &object, const char *key, const QString &where);
    Point point(const QJsonObject &object, const char *key, const QString &where);
    int integer(const QJsonObject &object, const char *key, const QString &where,
                int minimum, int maximum);
    QString string(const QJsonObject &object, const char *key, const QString &where);
    bool boolean(const QJsonObject &object, const char *key, const QString &where);
    ObjectId id(const QJsonObject &object, const char *key, const QString &where);
    QJsonArray array(const QJsonObject &object, const char *key, const QString &where);
    QJsonObject object(const QJsonObject &object, const char *key, const QString &where);

    // A size that must be greater than zero / must not be negative.
    Coord positiveMm(const QJsonObject &object, const char *key, const QString &where);
    Coord nonNegativeMm(const QJsonObject &object, const char *key, const QString &where);
    // Text that must not be empty.
    QString nonEmptyString(const QJsonObject &object, const char *key, const QString &where);
    // One of 'names'; returns its index in the list, or 0 after a failure.
    int choice(const QJsonObject &object, const char *key, const QString &where,
               const QStringList &names);
    // A list whose elements are all objects. The path of element i is
    // pathOf(pathOf(where, key), i).
    QList<QJsonObject> objectList(const QJsonObject &object, const char *key, const QString &where);

    // "<where>.<key>", or just the key at the root.
    static QString pathOf(const QString &where, const char *key);
    // "<where>[<index>]" for an element of a list.
    static QString pathOf(const QString &where, int index);

private:
    // Fetches a value; records "value is missing" when the key is absent.
    bool find(const QJsonObject &object, const char *key, const QString &where, QJsonValue &value);
    // A JSON number converted to nanometres; false when it is not a valid size.
    bool toNm(const QJsonValue &value, Coord &nm);

    QString m_error;
};

// Whole files. On failure they return false and put a message for the user
// into 'error'.
class JsonFile
{
    Q_DECLARE_TR_FUNCTIONS(ppcb::JsonFile)

public:
    static bool read(const QString &path, QJsonObject &root, QString &error);
    // Writes through a temporary file, so a failure leaves the old file intact.
    static bool write(const QString &path, const QJsonObject &root, QString &error);
};

} // namespace ppcb
