#include "part.h"

#include <QJsonDocument>
#include <QtTest>

using namespace ppcb;

namespace {

QJsonObject parse(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

const char DIODE_JSON[] = R"({
    "symbol": "diode",
    "footprints": ["diode-7.62", "diode-10.16"],
    "prefix": "D",
    "prefixGost": "VD",
    "value": "1N4148",
    "connector": false
})";

QString errorOf(const QByteArray &json)
{
    JsonReader reader;
    partFromJson(parse(json), QStringLiteral("part"), reader);
    return reader.error();
}

} // namespace

class TestPart : public QObject
{
    Q_OBJECT

private slots:
    void handWrittenDiodeIsRead()
    {
        JsonReader reader;
        const Part part = partFromJson(parse(DIODE_JSON), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(part.symbol, QStringLiteral("diode"));
        QCOMPARE(part.footprints, QStringList({"diode-7.62", "diode-10.16"}));
        QCOMPARE(part.prefix, QStringLiteral("D"));
        QCOMPARE(part.prefixGost, QStringLiteral("VD"));
        QCOMPARE(part.value, QStringLiteral("1N4148"));
        QCOMPARE(part.connector, false);
    }

    // A panel-mount part: no footprints, no value.
    void optionalFieldsHaveDefaults()
    {
        JsonReader reader;
        const Part part = partFromJson(
            parse(R"({"symbol":"battery","prefix":"BT","prefixGost":"GB"})"), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(part.footprints.isEmpty());
        QVERIFY(part.value.isEmpty());
        QCOMPARE(part.connector, false);
    }

    void everyFieldSurvivesTheFile()
    {
        Part part;
        part.symbol = QStringLiteral("header-1x3");
        part.footprints = QStringList({"header-1x3", "header-1x3-angled"});
        part.prefix = QStringLiteral("J");
        part.prefixGost = QStringLiteral("X");
        part.value = QStringLiteral("Питание ґ");
        part.connector = true;

        const QByteArray text = QJsonDocument(partToJson(part)).toJson();
        JsonReader reader;
        const Part back = partFromJson(parse(text), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(back == part);
        QCOMPARE(QJsonDocument(partToJson(back)).toJson(), text);

        // A part without footprints survives too.
        part.footprints.clear();
        JsonReader second;
        QVERIFY(partFromJson(partToJson(part), QString(), second) == part);
        QVERIFY2(second.ok(), qPrintable(second.error()));
    }

    void comparisonSeesEveryField()
    {
        JsonReader reader;
        const Part part = partFromJson(parse(DIODE_JSON), QString(), reader);
        Part other = part;
        QVERIFY(other == part);

        other = part; other.symbol += QLatin1Char('x');     QVERIFY(other != part);
        other = part; other.footprints.removeLast();        QVERIFY(other != part);
        other = part; other.footprints.swapItemsAt(0, 1);   QVERIFY(other != part); // order matters
        other = part; other.prefix = QStringLiteral("VD");  QVERIFY(other != part);
        other = part; other.prefixGost = QStringLiteral("D"); QVERIFY(other != part);
        other = part; other.value.clear();                  QVERIFY(other != part);
        other = part; other.connector = true;               QVERIFY(other != part);
    }

    void errors()
    {
        QString error = errorOf(R"({"prefix":"D","prefixGost":"VD"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.symbol: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"","prefix":"D","prefixGost":"VD"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.symbol: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"diode","prefixGost":"VD"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.prefix: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"diode","prefix":"D","prefixGost":""})");
        QVERIFY2(error.startsWith(QStringLiteral("part.prefixGost: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"diode","prefix":"D","prefixGost":"VD","value":5})");
        QVERIFY2(error.startsWith(QStringLiteral("part.value: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"diode","prefix":"D","prefixGost":"VD","connector":"yes"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.connector: ")), qPrintable(error));
    }

    void footprintListErrors()
    {
        QString error = errorOf(R"({"symbol":"d","footprints":"a","prefix":"D","prefixGost":"VD"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.footprints: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"d","footprints":["a",7],"prefix":"D","prefixGost":"VD"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.footprints[1]: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"d","footprints":["a",""],"prefix":"D","prefixGost":"VD"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.footprints[1]: ")), qPrintable(error));

        // The same footprint listed twice.
        error = errorOf(R"({"symbol":"d","footprints":["a","b","a"],"prefix":"D","prefixGost":"VD"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.footprints[2]: ")), qPrintable(error));

        // An empty list is a part without footprints.
        error = errorOf(R"({"symbol":"d","footprints":[],"prefix":"D","prefixGost":"VD"})");
        QCOMPARE(error, QString());
    }
};

QTEST_APPLESS_MAIN(TestPart)
#include "test_part.moc"
