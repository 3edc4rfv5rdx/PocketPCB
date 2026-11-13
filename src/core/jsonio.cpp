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
const int RIGHT_ANGLE = 90;
const int LAST_RIGHT_ANGLE = 270;

} // namespace

QString withPlace(const QString &place, const QString &message)
{
    if (place.isEmpty())
        return message;
    return place + QLatin1String(": ") + message;
}

QString fileTypeOf(const QJsonObject &root)
{
    return root.value(QLatin1String(KEY_TYPE)).toString();
}

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
    if (fileTypeOf(root) != QLatin1String(expectedFileType)) {
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
    Point result;
    if (!toPoint(value, result)) {
        fail(pathOf(where, key), tr("a pair of coordinates in millimetres is expected"));
        return Point();
    }
    return result;
}

bool JsonReader::toPoint(const QJsonValue &value, Point &point)
{
    const QJsonArray pair = value.toArray();
    Point result;
    if (!value.isArray() || pair.size() != POINT_SIZE || !toNm(pair.at(0), result.x)
        || !toNm(pair.at(1), result.y)) {
        return false;
    }
    point = result;
    return true;
}

QList<Point> JsonReader::pointList(const QJsonObject &object, const char *key, const QString &where)
{
    const QJsonArray items = array(object, key, where);
    QList<Point> result;
    for (int i = 0; i < items.size(); ++i) {
        Point point;
        if (!toPoint(items.at(i), point)) {
            fail(pathOf(pathOf(where, key), i),
                 tr("a pair of coordinates in millimetres is expected"));
            return QList<Point>();
        }
        result.append(point);
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

ObjectId JsonReader::uniqueId(const QJsonObject &object, const char *key, const QString &where,
                              QSet<ObjectId> &seen)
{
    const ObjectId result = id(object, key, where);
    if (ok() && seen.contains(result))
        fail(pathOf(where, key), tr("the identifier is used twice"));
    if (!result.isNull())
        seen.insert(result);
    return result;
}

int JsonReader::rightAngle(const QJsonObject &object, const char *key, const QString &where)
{
    const int degrees = integer(object, key, where, 0, LAST_RIGHT_ANGLE);
    if (ok() && degrees % RIGHT_ANGLE != 0) {
        fail(pathOf(where, key), tr("one of the angles 0 90 180 270 is expected"));
        return 0;
    }
    return degrees;
}

QStringList JsonReader::nameList(const QJsonObject &object, const char *key, const QString &where)
{
    const QJsonArray items = array(object, key, where);
    QStringList result;
    for (int i = 0; i < items.size(); ++i) {
        const QString itemWhere = pathOf(pathOf(where, key), i);
        const QString name = items.at(i).toString();
        if (!items.at(i).isString())
            fail(itemWhere, tr("text is expected"));
        else if (name.isEmpty())
            fail(itemWhere, tr("the text must not be empty"));
        else if (result.contains(name))
            fail(itemWhere, tr("the name is used twice"));
        if (!ok())
            return QStringList();
        result.append(name);
    }
    return result;
}

Coord JsonReader::positiveMm(const QJsonObject &object, const char *key, const QString &where)
{
    const Coord nm = mm(object, key, where);
    if (ok() && nm <= 0)
        fail(pathOf(where, key), tr("a size greater than zero is expected"));
    return ok() ? nm : 0;
}

Coord JsonReader::nonNegativeMm(const QJsonObject &object, const char *key, const QString &where)
{
    const Coord nm = mm(object, key, where);
    if (ok() && nm < 0)
        fail(pathOf(where, key), tr("a size that is not negative is expected"));
    return ok() ? nm : 0;
}

QString JsonReader::nonEmptyString(const QJsonObject &object, const char *key, const QString &where)
{
    const QString text = string(object, key, where);
    if (ok() && text.isEmpty())
        fail(pathOf(where, key), tr("the text must not be empty"));
    return text;
}

int JsonReader::choice(const QJsonObject &object, const char *key, const QString &where,
                       const QStringList &names)
{
    const QString text = string(object, key, where);
    const int index = int(names.indexOf(text));
    if (ok() && index < 0) {
        fail(pathOf(where, key),
             tr("one of these is expected") + QLatin1String(": ") + names.join(QLatin1String(", ")));
    }
    return index < 0 ? 0 : index;
}

QList<QJsonObject> JsonReader::objectList(const QJsonObject &object, const char *key,
                                          const QString &where)
{
    const QJsonArray items = array(object, key, where);
    QList<QJsonObject> result;
    for (int i = 0; i < items.size(); ++i) {
        if (!items.at(i).isObject()) {
            fail(pathOf(pathOf(where, key), i), tr("an object is expected"));
            return QList<QJsonObject>();
        }
        result.append(items.at(i).toObject());
    }
    return result;
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
