#include "schematic.h"
#include "symbol.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

namespace {

// Fixed ids, so that failures can be read.
ObjectId id(quint64 value)
{
    return ObjectId{value};
}

// Two sheets with one object of every kind, every field non-default.
Schematic fullSchematic()
{
    Schematic schematic;

    Sheet first;
    first.id = id(0x10);
    first.title = QStringLiteral("Питание");
    first.paper = PaperSize::A3;

    Component component;
    component.id = id(0x11);
    component.part = QStringLiteral("resistor-part");
    component.footprint = QStringLiteral("resistor-7.62");
    component.designator = QStringLiteral("R1");
    component.value = QStringLiteral("10k");
    component.offBoard = true;
    component.at = Point{10 * PIN_GRID, 20 * PIN_GRID};
    component.rotation = 270;
    component.mirrored = true;
    component.refAt = Point{0, -PIN_GRID};
    component.valueAt = Point{635000, PIN_GRID};
    first.components.append(component);

    Wire wire;
    wire.id = id(0x12);
    wire.points = {Point{0, 0}, Point{PIN_GRID, 0}, Point{2 * PIN_GRID, PIN_GRID},
                   Point{2 * PIN_GRID, -PIN_GRID}, Point{PIN_GRID, -2 * PIN_GRID}};
    first.wires.append(wire);

    Bus bus;
    bus.id = id(0x17);
    bus.points = {Point{0, 8 * PIN_GRID}, Point{4 * PIN_GRID, 8 * PIN_GRID},
                  Point{5 * PIN_GRID, 9 * PIN_GRID}};
    bus.name = QStringLiteral("D[0..7]");
    bus.namesAt = {Point{PIN_GRID, -PIN_GRID}, Point{3 * PIN_GRID, -PIN_GRID}};
    first.buses.append(bus);

    Label label;
    label.id = id(0x13);
    label.at = Point{PIN_GRID, 0};
    label.text = QStringLiteral("CLK");
    label.textAt = Point{0, -2 * PIN_GRID};
    label.scope = LabelScope::Local;
    label.rotation = 90;
    label.netClass = QStringLiteral("Power");
    first.labels.append(label);

    PowerSymbol power;
    power.id = id(0x14);
    power.at = Point{0, 5 * PIN_GRID};
    power.name = QStringLiteral("+5V");
    power.rotation = 180;
    power.symbol = QStringLiteral("power-bar");
    first.powerSymbols.append(power);

    NoConnect mark;
    mark.id = id(0x15);
    mark.at = Point{-PIN_GRID, PIN_GRID};
    first.noConnects.append(mark);

    SheetGraphic frame;
    frame.id = id(0x16);
    frame.graphic.kind = GraphicKind::Rect;
    frame.graphic.a = Point{-1000000, -1000000};
    frame.graphic.b = Point{9000000, 5000000};
    frame.graphic.width = 250000;
    frame.graphic.dashed = true;
    first.graphics.append(frame);

    schematic.sheets.append(first);

    Sheet second;
    second.id = id(0x20);
    schematic.sheets.append(second);
    return schematic;
}

Schematic readBack(const QJsonObject &root, QString &error)
{
    JsonReader reader;
    const Schematic schematic = schematicFromJson(root, reader);
    error = reader.error();
    return schematic;
}

// Replaces one field of the first element of a list of the first sheet.
// An undefined value removes the field.
QJsonObject withChange(const char *listKey, const char *key, const QJsonValue &value)
{
    QJsonObject root = schematicToJson(fullSchematic());
    QJsonArray sheets = root.value(QStringLiteral("sheets")).toArray();
    QJsonObject sheet = sheets.at(0).toObject();
    QJsonArray list = sheet.value(QLatin1String(listKey)).toArray();
    QJsonObject item = list.at(0).toObject();
    if (value.isUndefined())
        item.remove(QLatin1String(key));
    else
        item.insert(QLatin1String(key), value);
    list.replace(0, item);
    sheet.insert(QLatin1String(listKey), list);
    sheets.replace(0, sheet);
    root.insert(QStringLiteral("sheets"), sheets);
    return root;
}

QString errorOfChange(const char *listKey, const char *key, const QJsonValue &value)
{
    QString error;
    readBack(withChange(listKey, key, value), error);
    return error;
}

QJsonArray wirePoints(std::initializer_list<Point> points)
{
    QJsonArray array;
    for (const Point &point : points)
        array.append(pointToJson(point));
    return array;
}

} // namespace

class TestSchematic : public QObject
{
    Q_OBJECT

private slots:
    void everyFieldSurvivesTheFile()
    {
        const Schematic schematic = fullSchematic();
        const QByteArray text = QJsonDocument(schematicToJson(schematic)).toJson();
        QString error;
        const Schematic back = readBack(QJsonDocument::fromJson(text).object(), error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(back == schematic);
        QCOMPARE(QJsonDocument(schematicToJson(back)).toJson(), text);
    }

    void comparisonSeesEveryField()
    {
        const Schematic schematic = fullSchematic();
        Schematic other = schematic;
        QVERIFY(other == schematic);

#define CHANGED(statement) \
    other = schematic; \
    statement; \
    QVERIFY2(other != schematic, #statement)

        CHANGED(other.sheets.removeLast());
        // other.sheets[0] is spelled out each time: CHANGED() replaces 'other',
        // so a reference into it would not survive.
        CHANGED(other.sheets[0].id = id(1));
        CHANGED(other.sheets[0].title.clear());
        CHANGED(other.sheets[0].paper = PaperSize::A4);

        CHANGED(other.sheets[0].components[0].id = id(1));
        CHANGED(other.sheets[0].components[0].part.clear());
        CHANGED(other.sheets[0].components[0].footprint.clear());
        CHANGED(other.sheets[0].components[0].designator = QStringLiteral("R2"));
        CHANGED(other.sheets[0].components[0].value.clear());
        CHANGED(other.sheets[0].components[0].offBoard = false);
        CHANGED(other.sheets[0].components[0].at.x += 1);
        CHANGED(other.sheets[0].components[0].rotation = 0);
        CHANGED(other.sheets[0].components[0].mirrored = false);
        CHANGED(other.sheets[0].components[0].refAt.y += 1);
        CHANGED(other.sheets[0].components[0].valueAt.x += 1);
        CHANGED(other.sheets[0].components.clear());

        CHANGED(other.sheets[0].wires[0].id = id(1));
        CHANGED(other.sheets[0].wires[0].points[1].y += 1);
        CHANGED(other.sheets[0].wires[0].points.removeLast());

        CHANGED(other.sheets[0].buses[0].id = id(1));
        CHANGED(other.sheets[0].buses[0].points[1].y += 1);
        CHANGED(other.sheets[0].buses[0].name = QStringLiteral("A[0..3]"));
        CHANGED(other.sheets[0].buses[0].namesAt[0].x += 1);
        CHANGED(other.sheets[0].buses.clear());

        CHANGED(other.sheets[0].labels[0].id = id(1));
        CHANGED(other.sheets[0].labels[0].at.x += 1);
        CHANGED(other.sheets[0].labels[0].text = QStringLiteral("DATA"));
        CHANGED(other.sheets[0].labels[0].textAt.y += 1);
        CHANGED(other.sheets[0].labels[0].scope = LabelScope::Global);
        CHANGED(other.sheets[0].labels[0].rotation = 0);
        CHANGED(other.sheets[0].labels[0].netClass.clear());

        CHANGED(other.sheets[0].powerSymbols[0].id = id(1));
        CHANGED(other.sheets[0].powerSymbols[0].at.y += 1);
        CHANGED(other.sheets[0].powerSymbols[0].name = QStringLiteral("GND"));
        CHANGED(other.sheets[0].powerSymbols[0].rotation = 0);
        CHANGED(other.sheets[0].powerSymbols[0].symbol = QStringLiteral("ground"));

        CHANGED(other.sheets[0].noConnects[0].id = id(1));
        CHANGED(other.sheets[0].noConnects[0].at.x += 1);

        CHANGED(other.sheets[0].graphics[0].id = id(1));
        CHANGED(other.sheets[0].graphics[0].graphic.width += 1);
        CHANGED(other.sheets[0].graphics[0].graphic.dashed = false);
#undef CHANGED
    }

    void fileRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("test.ppsch"));
        const Schematic schematic = fullSchematic();

        QString error;
        QVERIFY2(SchematicFile::write(path, schematic, error), qPrintable(error));
        Schematic back;
        QVERIFY2(SchematicFile::read(path, back, error), qPrintable(error));
        QVERIFY(back == schematic);

        QJsonObject root;
        QVERIFY(JsonFile::read(path, root, error));
        QCOMPARE(fileTypeOf(root), QString::fromLatin1(FILE_TYPE_SCHEMATIC));
    }

    void fileOfAnotherKindIsRejected()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("board.ppsch"));
        QJsonObject root = schematicToJson(fullSchematic());
        writeHeader(root, FILE_TYPE_BOARD);
        QString error;
        QVERIFY(JsonFile::write(path, root, error));

        Schematic schematic = fullSchematic();
        QVERIFY(!SchematicFile::read(path, schematic, error));
        QVERIFY2(error.startsWith(path), qPrintable(error));
        QVERIFY(schematic == fullSchematic()); // untouched
    }

    void aSheetIsEmptyWithNothingOnIt()
    {
        QVERIFY(isSheetEmpty(Sheet()));
        const Schematic schematic = fullSchematic();
        QVERIFY(!isSheetEmpty(schematic.sheets.at(0)));
        Sheet sheet;
        sheet.title = QStringLiteral("Title");
        sheet.paper = PaperSize::A3;
        QVERIFY(isSheetEmpty(sheet));
        sheet.graphics.append(SheetGraphic());
        QVERIFY(!isSheetEmpty(sheet));
        QCOMPARE(paperSizeName(PaperSize::A3), QStringLiteral("A3"));
    }

    void schematicNeedsASheet()
    {
        QJsonObject root = schematicToJson(fullSchematic());
        root.insert(QStringLiteral("sheets"), QJsonArray());
        QString error;
        readBack(root, error);
        QVERIFY2(error.startsWith(QStringLiteral("sheets: ")), qPrintable(error));

        root.remove(QStringLiteral("sheets"));
        readBack(root, error);
        QVERIFY2(error.startsWith(QStringLiteral("sheets: ")), qPrintable(error));
    }

    void idsAreUniqueAcrossSheets()
    {
        // The second sheet takes the id of a wire of the first one.
        QJsonObject root = schematicToJson(fullSchematic());
        QJsonArray sheets = root.value(QStringLiteral("sheets")).toArray();
        QJsonObject second = sheets.at(1).toObject();
        second.insert(QStringLiteral("id"), idToJson(id(0x12)));
        sheets.replace(1, second);
        root.insert(QStringLiteral("sheets"), sheets);

        QString error;
        readBack(root, error);
        QVERIFY2(error.startsWith(QStringLiteral("sheets[1].id: ")), qPrintable(error));

        // Two objects of different kinds on one sheet.
        error = errorOfChange("labels", "id", idToJson(id(0x11)));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].labels[0].id: ")), qPrintable(error));

        error = errorOfChange("noConnects", "id", QStringLiteral("R1"));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].noConnects[0].id: ")), qPrintable(error));

        error = errorOfChange("graphics", "id", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].graphics[0].id: ")), qPrintable(error));
    }

    void componentErrors()
    {
        // The part and the footprint are not checked here: they are looked
        // up in the project library, which may lack them.
        QString error = errorOfChange("components", "part", QStringLiteral("capacitor"));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        error = errorOfChange("components", "part", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].components[0].part: ")),
                 qPrintable(error));

        error = errorOfChange("components", "designator", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].components[0].designator: ")),
                 qPrintable(error));

        error = errorOfChange("components", "rotation", 45);
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].components[0].rotation: ")),
                 qPrintable(error));
        error = errorOfChange("components", "rotation", 360);
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].components[0].rotation: ")),
                 qPrintable(error));
        error = errorOfChange("components", "rotation", -90);
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].components[0].rotation: ")),
                 qPrintable(error));

        error = errorOfChange("components", "offBoard", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].components[0].offBoard: ")),
                 qPrintable(error));

        // An empty footprint and an empty value are fine.
        error = errorOfChange("components", "footprint", QStringLiteral(""));
        QCOMPARE(error, QString());
        error = errorOfChange("components", "value", QStringLiteral(""));
        QCOMPARE(error, QString());
        for (int rotation : {0, 90, 180, 270})
            QCOMPARE(errorOfChange("components", "rotation", rotation), QString());
    }

    void wireRules()
    {
        const Coord g = PIN_GRID;
        QString error = errorOfChange("wires", "points", wirePoints({Point{0, 0}}));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].wires[0].points: ")), qPrintable(error));

        error = errorOfChange("wires", "points", QJsonArray());
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].wires[0].points: ")), qPrintable(error));

        // The third point repeats the second.
        error = errorOfChange("wires", "points", wirePoints({Point{0, 0}, Point{g, 0}, Point{g, 0}}));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].wires[0].points[2]: ")),
                 qPrintable(error));

        // Neither straight nor at 45 degrees.
        error = errorOfChange("wires", "points", wirePoints({Point{0, 0}, Point{2 * g, g}}));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].wires[0].points[1]: ")),
                 qPrintable(error));

        // All eight allowed directions.
        const Point directions[] = {Point{g, 0},  Point{-g, 0}, Point{0, g},  Point{0, -g},
                                    Point{g, g},  Point{g, -g}, Point{-g, g}, Point{-g, -g}};
        for (const Point &direction : directions) {
            error = errorOfChange("wires", "points", wirePoints({Point{g, g}, Point{g, g} + direction}));
            QCOMPARE(error, QString());
        }
    }

    void busRules()
    {
        const Coord g = PIN_GRID;
        QString error = errorOfChange("buses", "name", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].buses[0].name: ")), qPrintable(error));

        error = errorOfChange("buses", "name", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].buses[0].name: ")), qPrintable(error));

        // A bus takes the id of the wire of the same sheet.
        error = errorOfChange("buses", "id", idToJson(id(0x12)));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].buses[0].id: ")), qPrintable(error));

        // The points follow the rules of a wire.
        error = errorOfChange("buses", "points", wirePoints({Point{0, 0}}));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].buses[0].points: ")), qPrintable(error));

        error = errorOfChange("buses", "points", wirePoints({Point{0, 0}, Point{0, 0}}));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].buses[0].points[1]: ")),
                 qPrintable(error));

        error = errorOfChange("buses", "points", wirePoints({Point{0, 0}, Point{g, 3 * g}}));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].buses[0].points[1]: ")),
                 qPrintable(error));

        error = errorOfChange("buses", "points", wirePoints({Point{0, 0}, Point{g, -g}}));
        QCOMPARE(error, QString());

        // A sheet written before buses existed is not read.
        QJsonObject root = schematicToJson(fullSchematic());
        QJsonArray sheets = root.value(QStringLiteral("sheets")).toArray();
        QJsonObject sheet = sheets.at(0).toObject();
        sheet.remove(QStringLiteral("buses"));
        sheets.replace(0, sheet);
        root.insert(QStringLiteral("sheets"), sheets);
        readBack(root, error);
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].buses: ")), qPrintable(error));
    }

    void labelAndPowerErrors()
    {
        QString error = errorOfChange("labels", "text", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].labels[0].text: ")), qPrintable(error));

        error = errorOfChange("labels", "scope", QStringLiteral("sheet"));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].labels[0].scope: ")), qPrintable(error));

        error = errorOfChange("power", "name", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].power[0].name: ")), qPrintable(error));

        error = errorOfChange("power", "rotation", 30);
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].power[0].rotation: ")), qPrintable(error));

        error = errorOfChange("power", "symbol", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].power[0].symbol: ")), qPrintable(error));

        error = errorOfChange("graphics", "kind", QStringLiteral("spline"));
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].graphics[0].kind: ")), qPrintable(error));
    }

    void sheetErrors()
    {
        QJsonObject root = schematicToJson(fullSchematic());
        QJsonArray sheets = root.value(QStringLiteral("sheets")).toArray();
        QJsonObject sheet = sheets.at(0).toObject();
        sheet.insert(QStringLiteral("paper"), QStringLiteral("Letter"));
        sheets.replace(0, sheet);
        root.insert(QStringLiteral("sheets"), sheets);
        QString error;
        readBack(root, error);
        QVERIFY2(error.startsWith(QStringLiteral("sheets[0].paper: ")), qPrintable(error));
    }
};

QTEST_GUILESS_MAIN(TestSchematic)
#include "test_schematic.moc"
