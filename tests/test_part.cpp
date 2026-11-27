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
    "connector": false,
    "name": {"en": "Diode", "uk": "Діод", "ru": "Диод"}
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
        QCOMPARE(part.name.size(), 3);
        QCOMPARE(part.name.value(QStringLiteral("uk")), QStringLiteral("Діод"));
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
        QCOMPARE(part.noValue, false);
        QCOMPARE(part.powerMw, 0);
        QVERIFY(part.group.isEmpty());
        QVERIFY(part.name.isEmpty());
    }

    // The marks of 5 W and more are Roman numerals.
    void powersAboveFiveWattsAreRoman()
    {
        QCOMPARE(romanNumeral(5), QStringLiteral("V"));
        QCOMPARE(romanNumeral(20), QStringLiteral("XX"));
        QCOMPARE(romanNumeral(25), QStringLiteral("XXV"));
        QCOMPARE(romanNumeral(1994), QStringLiteral("MCMXCIV"));
        QVERIFY(romanNumeral(0).isEmpty());
        QVERIFY(standardPowersMw().contains(125));
    }

    // The first unit is the part's symbol, the others its units; a part
    // without them has one.
    void unitsNameTheirSymbols()
    {
        Part part;
        part.symbol = QStringLiteral("opamp-a");
        QCOMPARE(part.unitCount(), 1);
        QCOMPARE(part.unitSymbol(0), QStringLiteral("opamp-a"));
        QVERIFY(part.unitSymbol(1).isEmpty());
        part.units = QStringList({"opamp-b", "opamp-power"});
        QCOMPARE(part.unitCount(), 3);
        QCOMPARE(part.unitSymbol(1), QStringLiteral("opamp-b"));
        QCOMPARE(part.unitSymbol(2), QStringLiteral("opamp-power"));
        QVERIFY(part.unitSymbol(3).isEmpty());
        QVERIFY(part.unitSymbol(-1).isEmpty());
        // A part of one unit writes none.
        part.units.clear();
        QVERIFY(!partToJson(part).contains(QStringLiteral("units")));
    }

    void displayNameFallsBack()
    {
        Part part;
        part.name.insert(QStringLiteral("en"), QStringLiteral("Diode"));
        part.name.insert(QStringLiteral("ru"), QStringLiteral("Диод"));
        const QString file = QStringLiteral("diode");
        QCOMPARE(part.displayName({"ru", "en"}, file), QStringLiteral("Диод"));
        QCOMPARE(part.displayName({"uk", "en"}, file), QStringLiteral("Diode"));
        QCOMPARE(part.displayName({"uk"}, file), file);
        part.name.clear();
        QCOMPARE(part.displayName({"ru", "en"}, file), file);
    }

    void everyFieldSurvivesTheFile()
    {
        Part part;
        part.symbol = QStringLiteral("header-1x3");
        part.units = QStringList({"opamp-b", "opamp-power"});
        part.footprints = QStringList({"header-1x3", "header-1x3-angled"});
        part.prefix = QStringLiteral("J");
        part.prefixGost = QStringLiteral("X");
        part.value = QStringLiteral("Питание ґ");
        part.connector = true;
        part.noValue = true;
        part.powerMw = 125;
        part.group = QStringLiteral("connectors");
        part.name.insert(QStringLiteral("en"), QStringLiteral("Pin header"));
        part.name.insert(QStringLiteral("uk"), QStringLiteral("Штирьовий роз'єм"));

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
        other = part; other.units.append(QStringLiteral("b")); QVERIFY(other != part);
        other = part; other.footprints.removeLast();        QVERIFY(other != part);
        other = part; other.footprints.swapItemsAt(0, 1);   QVERIFY(other != part); // order matters
        other = part; other.prefix = QStringLiteral("VD");  QVERIFY(other != part);
        other = part; other.prefixGost = QStringLiteral("D"); QVERIFY(other != part);
        other = part; other.value.clear();                  QVERIFY(other != part);
        other = part; other.connector = true;               QVERIFY(other != part);
        other = part; other.noValue = true;                 QVERIFY(other != part);
        other = part; other.powerMw = 250;                  QVERIFY(other != part);
        other = part; other.group = QStringLiteral("x");    QVERIFY(other != part);
        other = part; other.name.remove(QStringLiteral("ru")); QVERIFY(other != part);
        other = part; other.name[QStringLiteral("en")] += QLatin1Char('x'); QVERIFY(other != part);
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

        error = errorOf(R"({"symbol":"diode","prefix":"D","prefixGost":"VD","name":"Diode"})");
        QVERIFY2(error.startsWith(QStringLiteral("part.name: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"diode","prefix":"D","prefixGost":"VD","name":{"en":5}})");
        QVERIFY2(error.startsWith(QStringLiteral("part.name.en: ")), qPrintable(error));

        error = errorOf(R"({"symbol":"diode","prefix":"D","prefixGost":"VD","name":{"ru":""}})");
        QVERIFY2(error.startsWith(QStringLiteral("part.name.ru: ")), qPrintable(error));
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
