#include "jsonio.h"

#include "version.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <climits>
#include <cmath>

namespace ppcb {

const char FILE_TYPE_PROJECT[] = "pocketpcb-project";
const char FILE_TYPE_SCHEMATIC[] = "pocketpcb-schematic";
const char FILE_TYPE_BOARD[] = "pocketpcb-board";
const char FILE_TYPE_SYMBOL[] = "pocketpcb-symbol";
const char FILE_TYPE_FOOTPRINT[] = "pocketpcb-footprint";
const char FILE_TYPE_PART[] = "pocketpcb-part";

namespace {

const char KEY_TYPE[] = "type";
const char KEY_FORMAT[] = "format";
const char KEY_APP[] = "app";

const int POINT_SIZE = 2; // [x, y]

// "<place>: <message>", or the message alone when there is no place.
QString withPlace(const QString &place, const QString &message)
{
    if (place.isEmpty())
        return message;
    return place + QLatin1String(": ") + message;
}

} // namespace

QJsonValue mmToJson(Coord nm)
{
    return QJsonValue(nmToMm(nm));
}

QJsonArray pointToJson(const Point &point)
{
    return QJsonArray{mmToJson(point.x), mmToJson(point.y)};
}

QJsonValue idToJson(const ObjectId &id)
{
    return QJsonValue(idToString(id));
}

void writeHeader(QJsonObject &root, const char *fileType)
{
    root.insert(QLatin1String(KEY_TYPE), QLatin1String(fileType));
    root.insert(QLatin1String(KEY_FORMAT), FORMAT_VERSION);
    root.insert(QLatin1String(KEY_APP), QLatin1String(appVersion()));
}

void JsonReader::fail(const QString &where, const QString &message)
{
    if (m_error.isEmpty())
        m_error = withPlace(where, message);
}

void JsonReader::header(const QJsonObject &root, const char *expectedFileType)
{
    // A missing or different type is the same problem for the user:
    // this is not the kind of file that was asked for.
    const QJsonValue type = root.value(QLatin1String(KEY_TYPE));
    if (!type.isString() || type.toString() != QLatin1String(expectedFileType)) {
        fail(QString(), tr("this is not a PocketPCB file of the expected kind"));
        return;
    }
    const int format = integer(root, KEY_FORMAT, QString(), 1, INT_MAX);
    if (ok() && format > FORMAT_VERSION)
        fail(QString(), tr("the file was made by a newer version of PocketPCB"));
}

QString JsonReader::pathOf(const QString &where, const char *key)
{
    if (where.isEmpty())
        return QLatin1String(key);
    return where + QLatin1Char('.') + QLatin1String(key);
}

QString JsonReader::pathOf(const QString &where, int index)
{
    return where + QLatin1Char('[') + QString::number(index) + QLatin1Char(']');
}

bool JsonReader::find(const QJsonObject &object, const char *key, const QString &where,
                      QJsonValue &value)
{
    const auto it = object.constFind(QLatin1String(key));
    if (it == object.constEnd()) {
        fail(pathOf(where, key), tr("the value is missing"));
        return false;
    }
    value = it.value();
    return true;
}

bool JsonReader::toNm(const QJsonValue &value, Coord &nm)
{
    return value.isDouble() && tryMmToNm(value.toDouble(), nm);
}

Coord JsonReader::mm(const QJsonObject &object, const char *key, const QString &where)
{
    QJsonValue value;
    if (!find(object, key, where, value))
        return 0;
    Coord nm = 0;
    if (!toNm(value, nm))
        fail(pathOf(where, key), tr("a size in millimetres is expected"));
    return nm;
}

Point JsonReader::point(const QJsonObject &object, const char *key, const QString &where)
{
    QJsonValue value;
    if (!find(object, key, where, value))
        return Point();
    const QJsonArray pair = value.toArray();
    Point result;
    if (!value.isArray() || pair.size() != POINT_SIZE || !toNm(pair.at(0), result.x)
        || !toNm(pair.at(1), result.y)) {
        fail(pathOf(where, key), tr("a pair of coordinates in millimetres is expected"));
        return Point();
    }
    return result;
}

int JsonReader::integer(const QJsonObject &object, const char *key, const QString &where,
                        int minimum, int maximum)
{
    QJsonValue value;
    if (!find(object, key, where, value))
        return 0;
    const double number = value.toDouble();
    // The range is checked on the double, before any conversion to int.
    if (!value.isDouble() || number != std::floor(number) || number < minimum || number > maximum) {
        fail(pathOf(where, key),
             tr("a whole number from %1 to %2 is expected").arg(minimum).arg(maximum));
        return 0;
    }
    return static_cast<int>(number);
}

QString JsonReader::string(const QJsonObject &object, const char *key, const QString &where)
{
    QJsonValue value;
    if (!find(object, key, where, value))
        return QString();
    if (!value.isString()) {
        fail(pathOf(where, key), tr("text is expected"));
        return QString();
    }
    return value.toString();
}

bool JsonReader::boolean(const QJsonObject &object, const char *key, const QString &where)
{
    QJsonValue value;
    if (!find(object, key, where, value))
        return false;
    if (!value.isBool()) {
        fail(pathOf(where, key), tr("true or false is expected"));
        return false;
    }
    return value.toBool();
}

ObjectId JsonReader::id(const QJsonObject &object, const char *key, const QString &where)
{
    QJsonValue value;
    if (!find(object, key, where, value))
        return ObjectId();
    const ObjectId result = idFromString(value.toString());
    if (result.isNull())
        fail(pathOf(where, key), tr("an object identifier is expected"));
    return result;
}

QJsonArray JsonReader::array(const QJsonObject &object, const char *key, const QString &where)
{
    QJsonValue value;
    if (!find(object, key, where, value))
        return QJsonArray();
    if (!value.isArray()) {
        fail(pathOf(where, key), tr("a list is expected"));
        return QJsonArray();
    }
    return value.toArray();
}

QJsonObject JsonReader::object(const QJsonObject &object, const char *key, const QString &where)
{
    QJsonValue value;
    if (!find(object, key, where, value))
        return QJsonObject();
    if (!value.isObject()) {
        fail(pathOf(where, key), tr("an object is expected"));
        return QJsonObject();
    }
    return value.toObject();
}

bool JsonFile::read(const QString &path, QJsonObject &root, QString &error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = withPlace(path, tr("cannot open the file") + QLatin1String(" (")
                                    + file.errorString() + QLatin1Char(')'));
        return false;
    }
    const QByteArray data = file.readAll();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        const int line = int(data.left(parseError.offset).count('\n')) + 1;
        error = withPlace(path, tr("the file is damaged") + QLatin1String(", ")
                                    + tr("line %1").arg(line) + QLatin1String(" (")
                                    + parseError.errorString() + QLatin1Char(')'));
        return false;
    }
    if (!document.isObject()) {
        error = withPlace(path, tr("this is not a PocketPCB file"));
        return false;
    }
    root = document.object();
    return true;
}

bool JsonFile::write(const QString &path, const QJsonObject &root, QString &error)
{
    QSaveFile file(path);
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = withPlace(path, tr("cannot write the file") + QLatin1String(" (")
                                    + file.errorString() + QLatin1Char(')'));
        return false;
    }
    return true;
}

} // namespace ppcb
