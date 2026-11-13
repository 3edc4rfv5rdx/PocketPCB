#include "footprint.h"

#include <QJsonDocument>
#include <QtTest>

using namespace ppcb;

namespace {

QJsonObject parse(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

// A 7.62 mm resistor as it would be written by hand: optional fields left out.
const char RESISTOR_JSON[] = R"({
    "pads": [
        {"number": "1", "at": [-3.81, 0], "shape": "circle", "size": [1.8, 1.8], "hole": 0.9},
        {"number": "2", "at": [3.81, 0], "shape": "circle", "size": [1.8, 1.8], "hole": 0.9}
    ],
    "graphics": [
        {"kind": "rect", "from": [-2.5, -1], "to": [2.5, 1]}
    ],
    "refAt": [0, -2],
    "valueAt": [0, 2]
})";

// Every field set to a non-default value.
Footprint fullFootprint()
{
    Footprint footprint;

    Pad through;
    through.number = QStringLiteral("1");
    through.at = Point{-1270000, 0};
    through.shape = PadShape::Oval;
    through.sizeX = 1600000;
    through.sizeY = 2400000;
    through.hole = 900000;
    through.layers = PadLayers::Both;
    footprint.pads.append(through);

    Pad singleSided = through;
    singleSided.number = QStringLiteral("2");
    singleSided.at = Point{1270000, 0};
    singleSided.shape = PadShape::Rect;
    singleSided.layers = PadLayers::Back;
    footprint.pads.append(singleSided);

    Pad surface;
    surface.at = Point{0, 2540000};
    surface.shape = PadShape::Circle;
    surface.sizeX = 1000000;
    surface.sizeY = 1000000;
    surface.hole = 0;
    surface.layers = PadLayers::Front;
    footprint.pads.append(surface);

    Graphic outline;
    outline.kind = GraphicKind::Circle;
    outline.radius = 3000000;
    outline.width = 200000;
    footprint.graphics.append(outline);

    footprint.refAt = Point{0, -4000000};
    footprint.valueAt = Point{500000, 4000000};
    return footprint;
}

} // namespace

class TestFootprint : public QObject
{
    Q_OBJECT

private:
    // Reads RESISTOR_JSON with pad 0 edited by 'change'; returns the error.
    template <typename Change>
    static QString errorAfterPadChange(Change change)
    {
        QJsonObject object = parse(RESISTOR_JSON);
        QJsonArray pads = object.value(QStringLiteral("pads")).toArray();
        QJsonObject pad = pads.at(0).toObject();
        change(pad);
        pads.replace(0, pad);
        object.insert(QStringLiteral("pads"), pads);

        JsonReader reader;
        footprintFromJson(object, QStringLiteral("fp"), reader);
        return reader.error();
    }

private slots:
    void handWrittenResistorIsRead()
    {
        JsonReader reader;
        const Footprint footprint = footprintFromJson(parse(RESISTOR_JSON), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));

        QCOMPARE(footprint.pads.size(), 2);
        const Pad &pad = footprint.pads.at(0);
        QCOMPARE(pad.number, QStringLiteral("1"));
        QVERIFY(pad.at == (Point{-3810000, 0}));
        QVERIFY(pad.shape == PadShape::Circle);
        QCOMPARE(pad.sizeX, Coord(1800000));
        QCOMPARE(pad.sizeY, Coord(1800000));
        QCOMPARE(pad.hole, Coord(900000));
        QVERIFY(pad.layers == PadLayers::Both); // a hole: through the board
        QCOMPARE(footprint.graphics.size(), 1);
        QVERIFY(footprint.refAt == (Point{0, -2000000}));
        QVERIFY(footprint.valueAt == (Point{0, 2000000}));
    }

    void padWithoutAHoleDefaultsToTheFrontSide()
    {
        JsonReader reader;
        const Footprint footprint = footprintFromJson(
            parse(R"({"pads":[{"at":[0,0],"shape":"rect","size":[1,2]}],
                      "graphics":[],"refAt":[0,0],"valueAt":[0,0]})"),
            QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(footprint.pads.at(0).hole, Coord(0));
        QVERIFY(footprint.pads.at(0).layers == PadLayers::Front);
        QVERIFY(footprint.pads.at(0).number.isEmpty());
    }

    void everyFieldSurvivesTheFile()
    {
        const Footprint footprint = fullFootprint();
        const QByteArray text = QJsonDocument(footprintToJson(footprint)).toJson();
        JsonReader reader;
        const Footprint back = footprintFromJson(parse(text), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(back == footprint);
        QCOMPARE(QJsonDocument(footprintToJson(back)).toJson(), text);
    }

    void comparisonSeesEveryField()
    {
        const Footprint footprint = fullFootprint();
        Footprint other = footprint;
        QVERIFY(other == footprint);

        other = footprint; other.pads[0].number = QStringLiteral("9"); QVERIFY(other != footprint);
        other = footprint; other.pads[0].at.x += 1;                    QVERIFY(other != footprint);
        other = footprint; other.pads[0].shape = PadShape::Rect;       QVERIFY(other != footprint);
        other = footprint; other.pads[0].sizeX += 1;                   QVERIFY(other != footprint);
        other = footprint; other.pads[0].sizeY += 1;                   QVERIFY(other != footprint);
        other = footprint; other.pads[0].hole += 1;                    QVERIFY(other != footprint);
        other = footprint; other.pads[0].layers = PadLayers::Front;    QVERIFY(other != footprint);
        other = footprint; other.pads.removeLast();                    QVERIFY(other != footprint);
        other = footprint; other.graphics[0].radius += 1;              QVERIFY(other != footprint);
        other = footprint; other.refAt.y += 1;                         QVERIFY(other != footprint);
        other = footprint; other.valueAt.x += 1;                       QVERIFY(other != footprint);
    }

    void padsMayShareANumber()
    {
        const QString error = errorAfterPadChange(
            [](QJsonObject &pad) { pad.insert(QStringLiteral("number"), QStringLiteral("2")); });
        QCOMPARE(error, QString());
    }

    void padErrors()
    {
        QCOMPARE(errorAfterPadChange([](QJsonObject &) {}), QString());

        QString error = errorAfterPadChange([](QJsonObject &pad) { pad.remove(QStringLiteral("at")); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].at: ")), qPrintable(error));

        error = errorAfterPadChange(
            [](QJsonObject &pad) { pad.insert(QStringLiteral("shape"), QStringLiteral("star")); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].shape: ")), qPrintable(error));

        error = errorAfterPadChange([](QJsonObject &pad) { pad.remove(QStringLiteral("size")); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].size: ")), qPrintable(error));

        error = errorAfterPadChange(
            [](QJsonObject &pad) { pad.insert(QStringLiteral("size"), QJsonArray{0, 1.8}); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].size: ")), qPrintable(error));

        error = errorAfterPadChange(
            [](QJsonObject &pad) { pad.insert(QStringLiteral("size"), QJsonArray{1.8, -1.8}); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].size: ")), qPrintable(error));

        // A round pad that is not round.
        error = errorAfterPadChange(
            [](QJsonObject &pad) { pad.insert(QStringLiteral("size"), QJsonArray{1.8, 2.4}); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].size: ")), qPrintable(error));

        error = errorAfterPadChange([](QJsonObject &pad) { pad.insert(QStringLiteral("hole"), -0.9); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].hole: ")), qPrintable(error));

        // The hole leaves no copper: equal to the pad, and larger than it.
        error = errorAfterPadChange([](QJsonObject &pad) { pad.insert(QStringLiteral("hole"), 1.8); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].hole: ")), qPrintable(error));
        error = errorAfterPadChange([](QJsonObject &pad) { pad.insert(QStringLiteral("hole"), 5); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].hole: ")), qPrintable(error));

        error = errorAfterPadChange(
            [](QJsonObject &pad) { pad.insert(QStringLiteral("layers"), QStringLiteral("inner")); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].layers: ")), qPrintable(error));

        // No hole, yet on both layers.
        error = errorAfterPadChange([](QJsonObject &pad) {
            pad.remove(QStringLiteral("hole"));
            pad.insert(QStringLiteral("layers"), QStringLiteral("both"));
        });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].layers: ")), qPrintable(error));

        error = errorAfterPadChange([](QJsonObject &pad) { pad.insert(QStringLiteral("number"), 1); });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].number: ")), qPrintable(error));
    }

    // The smaller side of an oval pad limits the hole.
    void holeIsCheckedAgainstTheSmallerSide()
    {
        QString error = errorAfterPadChange([](QJsonObject &pad) {
            pad.insert(QStringLiteral("shape"), QStringLiteral("oval"));
            pad.insert(QStringLiteral("size"), QJsonArray{3.0, 1.0});
            pad.insert(QStringLiteral("hole"), 1.2);
        });
        QVERIFY2(error.startsWith(QStringLiteral("fp.pads[0].hole: ")), qPrintable(error));

        error = errorAfterPadChange([](QJsonObject &pad) {
            pad.insert(QStringLiteral("shape"), QStringLiteral("oval"));
            pad.insert(QStringLiteral("size"), QJsonArray{3.0, 1.5});
            pad.insert(QStringLiteral("hole"), 1.2);
        });
        QCOMPARE(error, QString());
    }

    void footprintErrors()
    {
        JsonReader noPads;
        footprintFromJson(parse(R"({"graphics":[],"refAt":[0,0],"valueAt":[0,0]})"),
                          QStringLiteral("fp"), noPads);
        QVERIFY2(noPads.error().startsWith(QStringLiteral("fp.pads: ")), qPrintable(noPads.error()));

        JsonReader badGraphic;
        footprintFromJson(
            parse(R"({"pads":[],"graphics":[{"kind":"circle","center":[0,0],"radius":0}],
                      "refAt":[0,0],"valueAt":[0,0]})"),
            QStringLiteral("fp"), badGraphic);
        QVERIFY2(badGraphic.error().startsWith(QStringLiteral("fp.graphics[0].radius: ")),
                 qPrintable(badGraphic.error()));

        JsonReader noValueAt;
        footprintFromJson(parse(R"({"pads":[],"graphics":[],"refAt":[0,0]})"),
                          QStringLiteral("fp"), noValueAt);
        QVERIFY2(noValueAt.error().startsWith(QStringLiteral("fp.valueAt: ")),
                 qPrintable(noValueAt.error()));
    }

    void emptyFootprintIsAllowed()
    {
        JsonReader reader;
        const Footprint footprint = footprintFromJson(
            parse(R"({"pads":[],"graphics":[],"refAt":[0,0],"valueAt":[0,0]})"), QString(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(footprint == Footprint());
    }
};

QTEST_APPLESS_MAIN(TestFootprint)
#include "test_footprint.moc"
