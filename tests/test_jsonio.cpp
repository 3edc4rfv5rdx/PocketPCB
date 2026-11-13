#include "jsonio.h"
#include "version.h"

#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

namespace {

QJsonObject parse(const char *json)
{
    return QJsonDocument::fromJson(QByteArray(json)).object();
}

} // namespace

class TestJsonIo : public QObject
{
    Q_OBJECT

private slots:
    void idTextForm()
    {
        const ObjectId id = newObjectId();
        QVERIFY(!id.isNull());
        QVERIFY(newObjectId() != id);
        QVERIFY(ObjectId().isNull());

        const QString text = idToString(id);
        QCOMPARE(text.size(), OBJECT_ID_LENGTH);
        QCOMPARE(text, text.toLower());
        QVERIFY(idFromString(text) == id);

        // Leading zeros are kept, so the length never changes.
        QCOMPARE(idToString(ObjectId{0x2a}), QStringLiteral("00000000002a"));
        QVERIFY(idFromString(QStringLiteral("00000000002a")) == (ObjectId{0x2a}));
        QCOMPARE(idToString(ObjectId{0xffffffffffffULL}), QStringLiteral("ffffffffffff"));
        QVERIFY(idFromString(QStringLiteral("ffffffffffff")) == (ObjectId{0xffffffffffffULL}));
    }

    void idRejectsOtherForms()
    {
        QVERIFY(idFromString(QString()).isNull());
        QVERIFY(idFromString(QStringLiteral("R1")).isNull());
        QVERIFY(idFromString(QStringLiteral("a3f09c21b7e")).isNull());   // 11 digits
        QVERIFY(idFromString(QStringLiteral("a3f09c21b7e4a")).isNull()); // 13 digits
        QVERIFY(idFromString(QStringLiteral("A3F09C21B7E4")).isNull());  // upper case
        QVERIFY(idFromString(QStringLiteral("a3f09c21b7eg")).isNull());  // not a hex digit
        QVERIFY(idFromString(QStringLiteral("a3f09c21 7e4")).isNull());
        QVERIFY(idFromString(QStringLiteral("-3f09c21b7e4")).isNull());
        QVERIFY(idFromString(QStringLiteral("000000000000")).isNull()); // "no object"
        QVERIFY(!idFromString(QStringLiteral("a3f09c21b7e4")).isNull());
    }

    // Ids must not repeat within anything the size of a project.
    void idsDoNotRepeat()
    {
        QSet<ObjectId> seen;
        const int count = 20000;
        for (int i = 0; i < count; ++i)
            seen.insert(newObjectId());
        QCOMPARE(seen.size(), count);
    }

    void pointArithmetic()
    {
        const Point a{10, -20};
        const Point b{3, 4};
        QVERIFY(a == (Point{10, -20}));
        QVERIFY(a != b);
        QVERIFY(a + b == (Point{13, -16}));
        QVERIFY(a - b == (Point{7, -24}));
        QVERIFY(Point() == (Point{0, 0}));
    }

    void straightAndDiagonalDirections()
    {
        const Coord distant = 900000000000; // squaring it would not fit 64 bits
        QVERIFY(isStraightOrDiagonal(Point{5, 0}));
        QVERIFY(isStraightOrDiagonal(Point{0, -5}));
        QVERIFY(isStraightOrDiagonal(Point{5, 5}));
        QVERIFY(isStraightOrDiagonal(Point{-5, 5}));
        QVERIFY(isStraightOrDiagonal(Point{5, -5}));
        QVERIFY(isStraightOrDiagonal(Point{-distant, -distant}));
        QVERIFY(isStraightOrDiagonal(Point{0, 0}));
        QVERIFY(!isStraightOrDiagonal(Point{5, 4}));
        QVERIFY(!isStraightOrDiagonal(Point{-1, 2}));
        QVERIFY(!isStraightOrDiagonal(Point{distant, distant - 1}));
    }

    void sizesAreWrittenInReadableMillimetres()
    {
        QJsonObject object;
        object.insert(QStringLiteral("pitch"), mmToJson(2540000));
        object.insert(QStringLiteral("fine"), mmToJson(635000));
        object.insert(QStringLiteral("whole"), mmToJson(5000000));
        object.insert(QStringLiteral("at"), pointToJson(Point{-1270000, 10160000}));
        const QByteArray text = QJsonDocument(object).toJson(QJsonDocument::Compact);
        QCOMPARE(text, QByteArray(R"({"at":[-1.27,10.16],"fine":0.635,"pitch":2.54,"whole":5})"));
    }

    // What is written must come back as the same nanometres.
    void sizesSurviveTheTextForm()
    {
        const Coord samples[] = {0, 1, -1, 999, 25400, 635000, 2540000, 123456789, -987654321};
        for (Coord nm : samples) {
            QJsonObject object;
            object.insert(QStringLiteral("v"), mmToJson(nm));
            object.insert(QStringLiteral("p"), pointToJson(Point{nm, -nm}));
            const QByteArray text = QJsonDocument(object).toJson();
            const QJsonObject back = QJsonDocument::fromJson(text).object();

            JsonReader reader;
            QCOMPARE(reader.mm(back, "v", QString()), nm);
            QVERIFY(reader.point(back, "p", QString()) == (Point{nm, -nm}));
            QVERIFY2(reader.ok(), qPrintable(reader.error()));
        }
    }

    void readsEveryKind()
    {
        const ObjectId id = newObjectId();
        QJsonObject object = parse(R"({"w":0.5,"at":[1,2.54],"n":90,"name":"R1","on":true,
                                       "list":[1,2],"sub":{"k":1}})");
        object.insert(QStringLiteral("id"), idToJson(id));

        JsonReader reader;
        QCOMPARE(reader.mm(object, "w", QString()), Coord(500000));
        QVERIFY(reader.point(object, "at", QString()) == (Point{1000000, 2540000}));
        QCOMPARE(reader.integer(object, "n", QString(), 0, 270), 90);
        QCOMPARE(reader.string(object, "name", QString()), QStringLiteral("R1"));
        QCOMPARE(reader.boolean(object, "on", QString()), true);
        QVERIFY(reader.id(object, "id", QString()) == id);
        QCOMPARE(reader.array(object, "list", QString()).size(), 2);
        QCOMPARE(reader.object(object, "sub", QString()).size(), 1);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(reader.error().isEmpty());
    }

    void missingValueIsNamedWithItsPath()
    {
        JsonReader reader;
        QCOMPARE(reader.mm(QJsonObject(), "width", QStringLiteral("tracks[3]")), Coord(0));
        QVERIFY(!reader.ok());
        QVERIFY2(reader.error().startsWith(QStringLiteral("tracks[3].width: ")),
                 qPrintable(reader.error()));
    }

    void pathForms()
    {
        QCOMPARE(JsonReader::pathOf(QString(), "sheets"), QStringLiteral("sheets"));
        QCOMPARE(JsonReader::pathOf(QStringLiteral("sheets"), 2), QStringLiteral("sheets[2]"));
        QCOMPARE(JsonReader::pathOf(QStringLiteral("sheets[2]"), "wires"),
                 QStringLiteral("sheets[2].wires"));
        // A name from the file, not only Latin letters.
        QCOMPARE(JsonReader::pathOf(QStringLiteral("symbols"), QStringLiteral("резистор")),
                 QStringLiteral("symbols.резистор"));
        QCOMPARE(JsonReader::pathOf(QString(), QStringLiteral("symbols")), QStringLiteral("symbols"));
    }

    void tableOfObjectsByName()
    {
        const QJsonObject object = parse(R"({"table":{"b":{"k":2},"a":{"k":1}},"empty":{},
                                             "mixed":{"a":{},"b":5},"list":[]})");
        JsonReader reader;
        const QMap<QString, QJsonObject> table = reader.objectMap(object, "table", QString());
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(table.keys(), QStringList({QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(table.value(QStringLiteral("b")).value(QStringLiteral("k")).toInt(), 2);
        QVERIFY(reader.objectMap(object, "empty", QString()).isEmpty());
        QVERIFY2(reader.ok(), qPrintable(reader.error()));

        // One member that is not an object spoils the whole table.
        QVERIFY(reader.objectMap(object, "mixed", QStringLiteral("root")).isEmpty());
        QVERIFY2(reader.error().startsWith(QStringLiteral("root.mixed.b: ")),
                 qPrintable(reader.error()));

        JsonReader second;
        QVERIFY(second.objectMap(object, "list", QString()).isEmpty());
        QVERIFY2(second.error().startsWith(QStringLiteral("list: ")), qPrintable(second.error()));

        JsonReader third;
        QVERIFY(third.objectMap(object, "missing", QString()).isEmpty());
        QVERIFY2(third.error().startsWith(QStringLiteral("missing: ")), qPrintable(third.error()));
    }

    void onlyTheFirstErrorIsKept()
    {
        JsonReader reader;
        reader.string(QJsonObject(), "first", QString());
        const QString first = reader.error();
        reader.string(QJsonObject(), "second", QString());
        reader.fail(QStringLiteral("third"), QStringLiteral("x"));
        QCOMPARE(reader.error(), first);
        QVERIFY(first.startsWith(QStringLiteral("first: ")));
    }

    void wrongKinds_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("kind");

        QTest::newRow("mm text") << QByteArray(R"({"k":"2.54"})") << "mm";
        QTest::newRow("mm null") << QByteArray(R"({"k":null})") << "mm";
        QTest::newRow("mm too large") << QByteArray(R"({"k":1e9})") << "mm";
        QTest::newRow("point number") << QByteArray(R"({"k":5})") << "point";
        QTest::newRow("point one") << QByteArray(R"({"k":[1]})") << "point";
        QTest::newRow("point three") << QByteArray(R"({"k":[1,2,3]})") << "point";
        QTest::newRow("point text inside") << QByteArray(R"({"k":[1,"2"]})") << "point";
        QTest::newRow("point too large") << QByteArray(R"({"k":[1,1e9]})") << "point";
        QTest::newRow("integer fraction") << QByteArray(R"({"k":90.5})") << "integer";
        QTest::newRow("integer below") << QByteArray(R"({"k":-90})") << "integer";
        QTest::newRow("integer above") << QByteArray(R"({"k":360})") << "integer";
        QTest::newRow("integer huge") << QByteArray(R"({"k":1e300})") << "integer";
        QTest::newRow("integer text") << QByteArray(R"({"k":"90"})") << "integer";
        QTest::newRow("string number") << QByteArray(R"({"k":1})") << "string";
        QTest::newRow("boolean number") << QByteArray(R"({"k":1})") << "boolean";
        QTest::newRow("boolean text") << QByteArray(R"({"k":"true"})") << "boolean";
        QTest::newRow("id text") << QByteArray(R"({"k":"R1"})") << "id";
        QTest::newRow("id number") << QByteArray(R"({"k":7})") << "id";
        QTest::newRow("array object") << QByteArray(R"({"k":{}})") << "array";
        QTest::newRow("object array") << QByteArray(R"({"k":[]})") << "object";
    }

    void wrongKinds()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, kind);
        const QJsonObject object = QJsonDocument::fromJson(json).object();
        QVERIFY(!object.isEmpty());

        JsonReader reader;
        if (kind == QLatin1String("mm"))
            QCOMPARE(reader.mm(object, "k", QString()), Coord(0));
        else if (kind == QLatin1String("point"))
            QVERIFY(reader.point(object, "k", QString()) == Point());
        else if (kind == QLatin1String("integer"))
            QCOMPARE(reader.integer(object, "k", QString(), 0, 270), 0);
        else if (kind == QLatin1String("string"))
            QVERIFY(reader.string(object, "k", QString()).isEmpty());
        else if (kind == QLatin1String("boolean"))
            QCOMPARE(reader.boolean(object, "k", QString()), false);
        else if (kind == QLatin1String("id"))
            QVERIFY(reader.id(object, "k", QString()).isNull());
        else if (kind == QLatin1String("array"))
            QVERIFY(reader.array(object, "k", QString()).isEmpty());
        else if (kind == QLatin1String("object"))
            QVERIFY(reader.object(object, "k", QString()).isEmpty());
        else
            QFAIL("unknown kind in the test data");

        QVERIFY(!reader.ok());
        QVERIFY2(reader.error().startsWith(QStringLiteral("k: ")), qPrintable(reader.error()));
    }

    void headerIsWrittenAndAccepted()
    {
        QJsonObject root;
        writeHeader(root, FILE_TYPE_BOARD);
        QCOMPARE(root.value(QStringLiteral("type")).toString(), QStringLiteral("pocketpcb-board"));
        QCOMPARE(root.value(QStringLiteral("format")).toInt(), FORMAT_VERSION);
        QVERIFY(!QString::fromLatin1(appVersion()).isEmpty());
        QCOMPARE(root.value(QStringLiteral("app")).toString(), QString::fromLatin1(appVersion()));

        JsonReader reader;
        reader.header(root, FILE_TYPE_BOARD);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
    }

    void headerRejectsOtherFiles()
    {
        QJsonObject board;
        writeHeader(board, FILE_TYPE_BOARD);

        JsonReader wrongKind;
        wrongKind.header(board, FILE_TYPE_SCHEMATIC);
        QVERIFY(!wrongKind.ok());

        JsonReader noType;
        noType.header(parse(R"({"format":1})"), FILE_TYPE_BOARD);
        QVERIFY(!noType.ok());

        JsonReader noFormat;
        noFormat.header(parse(R"({"type":"pocketpcb-board"})"), FILE_TYPE_BOARD);
        QVERIFY(!noFormat.ok());

        JsonReader zeroFormat;
        zeroFormat.header(parse(R"({"type":"pocketpcb-board","format":0})"), FILE_TYPE_BOARD);
        QVERIFY(!zeroFormat.ok());
    }

    void headerRejectsANewerFormat()
    {
        QJsonObject root;
        writeHeader(root, FILE_TYPE_PROJECT);
        root.insert(QStringLiteral("format"), FORMAT_VERSION + 1);

        JsonReader reader;
        reader.header(root, FILE_TYPE_PROJECT);
        QVERIFY(!reader.ok());
    }

    void fileRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("test.ppbrd"));

        QJsonObject root;
        writeHeader(root, FILE_TYPE_BOARD);
        root.insert(QStringLiteral("name"), QStringLiteral("Плата ґ"));
        root.insert(QStringLiteral("at"), pointToJson(Point{2540000, -635000}));

        QString error;
        QVERIFY2(JsonFile::write(path, root, error), qPrintable(error));
        // Written twice: replacing an existing file must work too.
        QVERIFY2(JsonFile::write(path, root, error), qPrintable(error));

        QJsonObject back;
        QVERIFY2(JsonFile::read(path, back, error), qPrintable(error));
        QVERIFY(back == root);
        QVERIFY(error.isEmpty());
    }

    void readingAMissingFileFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("absent.ppcb"));
        QJsonObject root;
        QString error;
        QVERIFY(!JsonFile::read(path, root, error));
        QVERIFY2(error.startsWith(path), qPrintable(error));
    }

    void readingDamagedJsonNamesTheLine()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("damaged.ppcb"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{\n  \"type\": \"pocketpcb-project\",\n  \"format\": 1,,\n}\n");
        file.close();

        QJsonObject root;
        QString error;
        QVERIFY(!JsonFile::read(path, root, error));
        QVERIFY2(error.contains(QStringLiteral("line 3")), qPrintable(error));
    }

    void readingJsonThatIsNotAnObjectFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("list.ppcb"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[1, 2, 3]\n");
        file.close();

        QJsonObject root;
        QString error;
        QVERIFY(!JsonFile::read(path, root, error));
        QVERIFY(!error.isEmpty());
    }

    void writingToAMissingFolderFails()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("no-such-folder/test.ppcb"));
        QString error;
        QVERIFY(!JsonFile::write(path, QJsonObject(), error));
        QVERIFY2(error.startsWith(path), qPrintable(error));
    }
};

QTEST_GUILESS_MAIN(TestJsonIo)
#include "test_jsonio.moc"
