#include "symbol.h"

#include <QJsonDocument>
#include <QtTest>

using namespace ppcb;

namespace {

QJsonObject parse(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

// A resistor as it would be written by hand: optional fields left out.
const char RESISTOR_JSON[] = R"({
    "graphics": [
        {"kind": "rect", "from": [-2.54, -1.016], "to": [2.54, 1.016]}
    ],
    "pins": [
        {"number": "1", "at": [-5.08, 0], "side": "left"},
        {"number": "2", "at": [5.08, 0], "side": "right"}
    ],
    "refAt": [0, -2.54],
    "valueAt": [0, 2.54],
    "showPinNumbers": false,
    "showPinNames": false
})";

// One graphic of every kind, with every field set to a non-default value.
Symbol fullSymbol()
{
    Symbol symbol;

    Graphic line;
    line.kind = GraphicKind::Line;
    line.a = Point{-1000000, 2000000};
    line.b = Point{3000000, -4000000};
    line.width = 250000;
    line.dashed = true;
    symbol.graphics.append(line);

    Graphic rect;
    rect.kind = GraphicKind::Rect;
    rect.a = Point{-2540000, -1016000};
    rect.b = Point{2540000, 1016000};
    rect.width = 300000;
    symbol.graphics.append(rect);

    Graphic circle;
    circle.kind = GraphicKind::Circle;
    circle.a = Point{635000, -635000};
    circle.radius = 1270000;
    circle.width = 200000;
    symbol.graphics.append(circle);

    Graphic arc;
    arc.kind = GraphicKind::Arc;
    arc.a = Point{0, 1270000};
    arc.radius = 1270000;
    arc.startAngle = 180;
    arc.spanAngle = 360;
    arc.width = 100000;
    symbol.graphics.append(arc);

    Graphic text;
    text.kind = GraphicKind::Text;
    text.a = Point{0, 0};
    text.text = QStringLiteral("=1 ґ");
    text.height = 1500000;
    text.align = TextAlign::Right;
    symbol.graphics.append(text);

    Graphic polygon;
    polygon.kind = GraphicKind::Polygon;
    polygon.points = {Point{0, 0}, Point{1270000, 635000}, Point{0, 1270000}};
    polygon.filled = true;
    polygon.width = 120000;
    symbol.graphics.append(polygon);

    Pin pin;
    pin.number = QStringLiteral("A1");
    pin.name = QStringLiteral("CLK");
    pin.at = Point{-3 * PIN_GRID, 2 * PIN_GRID};
    pin.side = PinSide::Down;
    pin.length = 3810000;
    symbol.pins.append(pin);

    Pin second;
    second.number = QStringLiteral("2");
    second.at = Point{PIN_GRID, 0};
    second.side = PinSide::Up;
    second.length = 0;
    symbol.pins.append(second);

    symbol.refAt = Point{0, -5080000};
    symbol.valueAt = Point{1270000, 5080000};
    symbol.showPinNumbers = false;
    symbol.showPinNames = true;
    return symbol;
}

} // namespace

class TestSymbol : public QObject
{
    Q_OBJECT

private:
    // Reads RESISTOR_JSON after 'change' has edited it; returns the error.
    template <typename Change>
    static QString errorAfter(Change change)
    {
        QJsonObject object = parse(RESISTOR_JSON);
        change(object);
        JsonReader reader;
        symbolFromJson(object, QStringLiteral("symbol"), reader);
        return reader.error();
    }

    // Replaces one field of element 'index' of the list 'listKey'.
    static void setInList(QJsonObject &object, const char *listKey, int index, const char *key,
                          const QJsonValue &value)
    {
        QJsonArray list = object.value(QLatin1String(listKey)).toArray();
        QJsonObject item = list.at(index).toObject();
        if (value.isUndefined())
            item.remove(QLatin1String(key));
        else
            item.insert(QLatin1String(key), value);
        list.replace(index, item);
        object.insert(QLatin1String(listKey), list);
    }

private slots:
    void handWrittenResistorIsRead()
    {
        JsonReader reader;
        const Symbol symbol = symbolFromJson(parse(RESISTOR_JSON), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));

        QCOMPARE(symbol.graphics.size(), 1);
        QVERIFY(symbol.graphics.at(0).kind == GraphicKind::Rect);
        QVERIFY(symbol.graphics.at(0).a == (Point{-2540000, -1016000}));
        QVERIFY(symbol.graphics.at(0).b == (Point{2540000, 1016000}));
        QCOMPARE(symbol.graphics.at(0).width, DEFAULT_LINE_WIDTH);

        QCOMPARE(symbol.pins.size(), 2);
        QCOMPARE(symbol.pins.at(0).number, QStringLiteral("1"));
        QVERIFY(symbol.pins.at(0).name.isEmpty());
        QVERIFY(symbol.pins.at(0).at == (Point{-5080000, 0}));
        QVERIFY(symbol.pins.at(0).side == PinSide::Left);
        QCOMPARE(symbol.pins.at(0).length, DEFAULT_PIN_LENGTH);
        QVERIFY(symbol.pins.at(1).side == PinSide::Right);

        QVERIFY(symbol.refAt == (Point{0, -2540000}));
        QVERIFY(symbol.valueAt == (Point{0, 2540000}));
        QCOMPARE(symbol.showPinNumbers, false);
        QCOMPARE(symbol.showPinNames, false);
    }

    void optionalFlagsDefaultToShown()
    {
        QJsonObject object = parse(RESISTOR_JSON);
        object.remove(QStringLiteral("showPinNumbers"));
        object.remove(QStringLiteral("showPinNames"));
        JsonReader reader;
        const Symbol symbol = symbolFromJson(object, QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(symbol.showPinNumbers, true);
        QCOMPARE(symbol.showPinNames, true);
    }

    // Everything written must come back equal, through real JSON text.
    void everyFieldSurvivesTheFile()
    {
        const Symbol symbol = fullSymbol();
        const QByteArray text = QJsonDocument(symbolToJson(symbol)).toJson();
        JsonReader reader;
        const Symbol back = symbolFromJson(parse(text), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(back == symbol);
        // Written again, the text is the same: the form is stable.
        QCOMPARE(QJsonDocument(symbolToJson(back)).toJson(), text);
    }

    void comparisonSeesEveryField()
    {
        const Symbol symbol = fullSymbol();
        Symbol other = symbol;
        QVERIFY(other == symbol);

        other = symbol; other.graphics[0].b.x += 1;       QVERIFY(other != symbol);
        other = symbol; other.graphics[0].width += 1;     QVERIFY(other != symbol);
        other = symbol; other.graphics[0].dashed = false; QVERIFY(other != symbol);
        other = symbol; other.graphics[2].radius += 1;    QVERIFY(other != symbol);
        other = symbol; other.graphics[3].startAngle = 0; QVERIFY(other != symbol);
        other = symbol; other.graphics[3].spanAngle = 90; QVERIFY(other != symbol);
        other = symbol; other.graphics[4].text += QLatin1Char('!'); QVERIFY(other != symbol);
        other = symbol; other.graphics[4].height += 1;    QVERIFY(other != symbol);
        other = symbol; other.graphics[4].align = TextAlign::Left; QVERIFY(other != symbol);
        other = symbol; other.graphics[4].kind = GraphicKind::Line; QVERIFY(other != symbol);
        other = symbol; other.graphics[5].points[1].x += 1; QVERIFY(other != symbol);
        other = symbol; other.graphics[5].points.removeLast(); QVERIFY(other != symbol);
        other = symbol; other.graphics[5].filled = false; QVERIFY(other != symbol);
        other = symbol; other.graphics.removeLast();      QVERIFY(other != symbol);
        other = symbol; other.pins[0].number = QStringLiteral("B"); QVERIFY(other != symbol);
        other = symbol; other.pins[0].name.clear();       QVERIFY(other != symbol);
        other = symbol; other.pins[0].at.y += PIN_GRID;   QVERIFY(other != symbol);
        other = symbol; other.pins[0].side = PinSide::Left; QVERIFY(other != symbol);
        other = symbol; other.pins[0].length += 1;        QVERIFY(other != symbol);
        other = symbol; other.refAt.x += 1;               QVERIFY(other != symbol);
        other = symbol; other.valueAt.y += 1;             QVERIFY(other != symbol);
        other = symbol; other.showPinNumbers = true;      QVERIFY(other != symbol);
        other = symbol; other.showPinNames = false;       QVERIFY(other != symbol);
    }

    void emptySymbolIsAllowed()
    {
        JsonReader reader;
        const Symbol symbol = symbolFromJson(
            parse(R"({"graphics":[],"pins":[],"refAt":[0,0],"valueAt":[0,0]})"), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(symbol == Symbol());
    }

    void errorsNameThePlace()
    {
        QCOMPARE(errorAfter([](QJsonObject &) {}), QString());

        QString error = errorAfter([](QJsonObject &o) { o.remove(QStringLiteral("pins")); });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins: ")), qPrintable(error));

        error = errorAfter([](QJsonObject &o) { o.remove(QStringLiteral("refAt")); });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.refAt: ")), qPrintable(error));

        error = errorAfter([](QJsonObject &o) { o.insert(QStringLiteral("showPinNames"), 1); });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.showPinNames: ")), qPrintable(error));

        error = errorAfter([](QJsonObject &o) {
            o.insert(QStringLiteral("graphics"), QJsonArray{1});
        });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.graphics[0]: ")), qPrintable(error));
    }

    void pinErrors()
    {
        QString error = errorAfter([](QJsonObject &o) { setInList(o, "pins", 1, "number", "1"); });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins[1].number: ")), qPrintable(error));

        error = errorAfter([](QJsonObject &o) { setInList(o, "pins", 0, "number", ""); });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins[0].number: ")), qPrintable(error));

        error = errorAfter([](QJsonObject &o) {
            setInList(o, "pins", 0, "number", QJsonValue(QJsonValue::Undefined));
        });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins[0].number: ")), qPrintable(error));

        // Off the pin grid, in either coordinate.
        error = errorAfter([](QJsonObject &o) {
            setInList(o, "pins", 0, "at", QJsonArray{-5.0, 0});
        });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins[0].at: ")), qPrintable(error));
        error = errorAfter([](QJsonObject &o) {
            setInList(o, "pins", 1, "at", QJsonArray{5.08, -1.27});
        });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins[1].at: ")), qPrintable(error));

        error = errorAfter([](QJsonObject &o) { setInList(o, "pins", 0, "side", "west"); });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins[0].side: ")), qPrintable(error));
        QVERIFY2(error.contains(QStringLiteral("left, right, up, down")), qPrintable(error));

        error = errorAfter([](QJsonObject &o) { setInList(o, "pins", 0, "length", -2.54); });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins[0].length: ")), qPrintable(error));

        error = errorAfter([](QJsonObject &o) { setInList(o, "pins", 0, "name", 5); });
        QVERIFY2(error.startsWith(QStringLiteral("symbol.pins[0].name: ")), qPrintable(error));

        // A pin of zero length and a pin on a negative grid node are fine.
        error = errorAfter([](QJsonObject &o) {
            setInList(o, "pins", 0, "length", 0);
            setInList(o, "pins", 0, "at", QJsonArray{-7.62, -2.54});
        });
        QCOMPARE(error, QString());
    }

    void graphicErrors_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("key");

        QTest::newRow("no kind") << QByteArray(R"({"from":[0,0],"to":[1,1]})") << "kind";
        QTest::newRow("unknown kind") << QByteArray(R"({"kind":"spline"})") << "kind";
        QTest::newRow("polygon no points") << QByteArray(R"({"kind":"polygon"})") << "points";
        QTest::newRow("polygon two points")
            << QByteArray(R"({"kind":"polygon","points":[[0,0],[1,1]]})") << "points";
        QTest::newRow("polygon bad point")
            << QByteArray(R"({"kind":"polygon","points":[[0,0],[1,1],[2]]})") << "points[2]";
        QTest::newRow("polygon filled is not a flag")
            << QByteArray(R"({"kind":"polygon","points":[[0,0],[1,1],[2,0]],"filled":"yes"})")
            << "filled";
        QTest::newRow("line no to") << QByteArray(R"({"kind":"line","from":[0,0]})") << "to";
        QTest::newRow("rect no from") << QByteArray(R"({"kind":"rect","to":[0,0]})") << "from";
        QTest::newRow("line zero width")
            << QByteArray(R"({"kind":"line","from":[0,0],"to":[1,1],"width":0})") << "width";
        QTest::newRow("line negative width")
            << QByteArray(R"({"kind":"line","from":[0,0],"to":[1,1],"width":-0.1})") << "width";
        QTest::newRow("line dashed is not a flag")
            << QByteArray(R"({"kind":"line","from":[0,0],"to":[1,1],"dashed":1})") << "dashed";
        QTest::newRow("circle no radius") << QByteArray(R"({"kind":"circle","center":[0,0]})") << "radius";
        QTest::newRow("circle zero radius")
            << QByteArray(R"({"kind":"circle","center":[0,0],"radius":0})") << "radius";
        QTest::newRow("arc no span")
            << QByteArray(R"({"kind":"arc","center":[0,0],"radius":1,"start":0})") << "span";
        QTest::newRow("arc zero span")
            << QByteArray(R"({"kind":"arc","center":[0,0],"radius":1,"start":0,"span":0})") << "span";
        QTest::newRow("arc span over a turn")
            << QByteArray(R"({"kind":"arc","center":[0,0],"radius":1,"start":0,"span":361})") << "span";
        QTest::newRow("arc start 360")
            << QByteArray(R"({"kind":"arc","center":[0,0],"radius":1,"start":360,"span":90})") << "start";
        QTest::newRow("arc negative start")
            << QByteArray(R"({"kind":"arc","center":[0,0],"radius":1,"start":-90,"span":90})") << "start";
        QTest::newRow("text empty")
            << QByteArray(R"({"kind":"text","at":[0,0],"text":"","height":1})") << "text";
        QTest::newRow("text no height")
            << QByteArray(R"({"kind":"text","at":[0,0],"text":"&"})") << "height";
        QTest::newRow("text zero height")
            << QByteArray(R"({"kind":"text","at":[0,0],"text":"&","height":0})") << "height";
        QTest::newRow("text unknown align")
            << QByteArray(R"({"kind":"text","at":[0,0],"text":"&","height":1,"align":"top"})") << "align";
    }

    void graphicErrors()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, key);
        const QJsonObject object = parse(json);
        QVERIFY(!object.isEmpty());

        JsonReader reader;
        graphicFromJson(object, QStringLiteral("g"), reader);
        QVERIFY(!reader.ok());
        const QString expected = QStringLiteral("g.") + key + QStringLiteral(": ");
        QVERIFY2(reader.error().startsWith(expected), qPrintable(reader.error()));
    }

    void graphicDefaults()
    {
        JsonReader reader;
        const Graphic text = graphicFromJson(
            parse(R"({"kind":"text","at":[1,2],"text":"&","height":1.5})"), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(text.align == TextAlign::Center);

        const Graphic arc = graphicFromJson(
            parse(R"({"kind":"arc","center":[0,0],"radius":1.27,"start":359,"span":360})"),
            QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(arc.startAngle, 359);
        QCOMPARE(arc.spanAngle, 360);
        QCOMPARE(arc.width, DEFAULT_LINE_WIDTH);
        QCOMPARE(arc.dashed, false);

        const Graphic rect = graphicFromJson(
            parse(R"({"kind":"rect","from":[0,0],"to":[5,5],"dashed":true})"), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(rect.dashed, true);

        // Text has no line: the field is not read, not even to complain.
        const Graphic note = graphicFromJson(
            parse(R"({"kind":"text","at":[0,0],"text":"&","height":1,"dashed":true})"), QString(),
            reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(note.dashed, false);

        const Graphic polygon = graphicFromJson(
            parse(R"({"kind":"polygon","points":[[0,0],[1.27,0.635],[0,1.27]]})"), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(polygon.points.size(), 3);
        QVERIFY(polygon.points.at(1) == (Point{1270000, 635000}));
        QCOMPARE(polygon.filled, false);
        QCOMPARE(polygon.width, DEFAULT_LINE_WIDTH);
    }

    // A written graphic carries only the fields of its kind.
    void graphicIsWrittenWithItsOwnFields()
    {
        Graphic circle;
        circle.kind = GraphicKind::Circle;
        circle.a = Point{1270000, 0};
        circle.radius = 635000;
        QCOMPARE(QJsonDocument(graphicToJson(circle)).toJson(QJsonDocument::Compact),
                 QByteArray(R"({"center":[1.27,0],"dashed":false,"kind":"circle","radius":0.635,"width":0.15})"));

        Graphic text;
        text.kind = GraphicKind::Text;
        text.text = QStringLiteral("&");
        text.height = 1500000;
        QCOMPARE(QJsonDocument(graphicToJson(text)).toJson(QJsonDocument::Compact),
                 QByteArray(R"({"align":"center","at":[0,0],"height":1.5,"kind":"text","text":"&"})"));

        Graphic polygon;
        polygon.kind = GraphicKind::Polygon;
        polygon.points = {Point{0, 0}, Point{1270000, 635000}, Point{0, 1270000}};
        polygon.filled = true;
        QCOMPARE(QJsonDocument(graphicToJson(polygon)).toJson(QJsonDocument::Compact),
                 QByteArray(R"({"dashed":false,"filled":true,"kind":"polygon","points":[[0,0],[1.27,0.635],[0,1.27]],"width":0.15})"));
    }
};

QTEST_APPLESS_MAIN(TestSymbol)
#include "test_symbol.moc"
