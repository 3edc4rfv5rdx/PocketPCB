#include "board.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

// Fixed ids, so that failures can be read.
ObjectId id(quint64 value)
{
    return ObjectId{value};
}

// One object of every kind (two placements), every field non-default.
Board fullBoard()
{
    Board board;

    // A rectangle of 50 x 30 mm with one corner cut off and one rounded.
    board.outline = {OutlineCorner{Point{0, 0}, 0}, OutlineCorner{Point{50 * MM, 0}, 2 * MM},
                     OutlineCorner{Point{50 * MM, 30 * MM}, 0},
                     OutlineCorner{Point{10 * MM, 30 * MM}, 0},
                     OutlineCorner{Point{0, 20 * MM}, 0}};

    Placement placement;
    placement.id = id(0x11);
    placement.component = id(0xC1);
    placement.footprint = QStringLiteral("resistor-7.62");
    placement.designator = QStringLiteral("R1");
    placement.value = QStringLiteral("10k");
    placement.at = Point{20 * MM, 10 * MM};
    placement.rotation = 270;
    placement.side = BoardSide::Bottom;
    placement.refAt = Point{0, -2 * MM};
    placement.valueAt = Point{635000, 2 * MM};
    placement.showRef = false;
    placement.showValue = true;
    placement.padNets.insert(QStringLiteral("1"), QStringLiteral("GND"));
    placement.padNets.insert(QStringLiteral("2"), QStringLiteral("Вход"));
    board.placements.append(placement);

    Placement second;
    second.id = id(0x12);
    second.component = id(0xC2);
    second.footprint = QStringLiteral("empty");
    second.designator = QStringLiteral("X1");
    board.placements.append(second);

    Track track;
    track.id = id(0x21);
    track.side = BoardSide::Bottom;
    track.net = QStringLiteral("GND");
    track.width = 500000;
    track.from = Point{5 * MM, 5 * MM};
    track.to = Point{12 * MM, 8 * MM}; // any angle is allowed
    board.tracks.append(track);

    Via via;
    via.id = id(0x31);
    via.at = Point{12 * MM, 8 * MM};
    via.net = QStringLiteral("GND");
    via.drill = 600000;
    via.size = 1200000;
    board.vias.append(via);

    MountingHole hole;
    hole.id = id(0x41);
    hole.at = Point{3 * MM, 3 * MM};
    hole.drill = 3200000;
    hole.plated = true;
    hole.ring = 1500000;
    hole.net = QStringLiteral("GND");
    board.holes.append(hole);

    BoardGraphic text;
    text.id = id(0x51);
    text.side = BoardSide::Bottom;
    text.graphic.kind = GraphicKind::Text;
    text.graphic.a = Point{25 * MM, 25 * MM};
    text.graphic.text = QStringLiteral("Плата v1");
    text.graphic.height = 1500000;
    text.graphic.align = TextAlign::Left;
    board.graphics.append(text);

    Zone zone;
    zone.id = id(0x61);
    zone.side = BoardSide::Bottom;
    zone.net = QStringLiteral("GND");
    zone.clearance = 400000;
    zone.points = {Point{MM, MM}, Point{40 * MM, MM}, Point{33 * MM, 17 * MM}};
    board.zones.append(zone);
    return board;
}

Board readBack(const QJsonObject &root, QString &error)
{
    JsonReader reader;
    const Board board = boardFromJson(root, reader);
    error = reader.error();
    return board;
}

QString errorOf(const QJsonObject &root)
{
    QString error;
    readBack(root, error);
    return error;
}

// Replaces one field of element 'index' of a list of the board and reads
// the result; returns the error. An undefined value removes the field.
QString errorOfChange(const char *listKey, int index, const char *key, const QJsonValue &value)
{
    QJsonObject root = boardToJson(fullBoard());
    QJsonArray list = root.value(QLatin1String(listKey)).toArray();
    QJsonObject item = list.at(index).toObject();
    if (value.isUndefined())
        item.remove(QLatin1String(key));
    else
        item.insert(QLatin1String(key), value);
    list.replace(index, item);
    root.insert(QLatin1String(listKey), list);
    return errorOf(root);
}

QString errorOfChange(const char *listKey, const char *key, const QJsonValue &value)
{
    return errorOfChange(listKey, 0, key, value);
}

// Reads the full board with another outline; returns the error.
QString errorOfOutline(std::initializer_list<OutlineCorner> corners)
{
    Board board = fullBoard();
    board.outline = corners;
    return errorOf(boardToJson(board));
}

QJsonObject netsObject(const QString &pad, const QJsonValue &net)
{
    QJsonObject nets;
    nets.insert(pad, net);
    return nets;
}

QJsonArray pointArray(std::initializer_list<Point> points)
{
    QJsonArray array;
    for (const Point &point : points)
        array.append(pointToJson(point));
    return array;
}

} // namespace

class TestBoard : public QObject
{
    Q_OBJECT

private slots:
    void everyFieldSurvivesTheFile()
    {
        const Board board = fullBoard();
        const QByteArray text = QJsonDocument(boardToJson(board)).toJson();
        QString error;
        const Board back = readBack(QJsonDocument::fromJson(text).object(), error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(back == board);
        QCOMPARE(QJsonDocument(boardToJson(back)).toJson(), text);
    }

    void comparisonSeesEveryField()
    {
        const Board board = fullBoard();
        Board other = board;
        QVERIFY(other == board);

#define CHANGED(statement) \
    other = board; \
    statement; \
    QVERIFY2(other != board, #statement)


        CHANGED(other.outline[1].at.x += 1);
        CHANGED(other.outline[1].radius = 0);
        CHANGED(other.outline.removeLast());

        // other.placements[0] is spelled out each time: CHANGED() replaces
        // 'other', so a reference into it would not survive.
        CHANGED(other.placements[0].id = id(1));
        CHANGED(other.placements[0].component = id(1));
        CHANGED(other.placements[0].footprint = QStringLiteral("empty"));
        CHANGED(other.placements[0].designator = QStringLiteral("R2"));
        CHANGED(other.placements[0].value.clear());
        CHANGED(other.placements[0].at.y += 1);
        CHANGED(other.placements[0].rotation = 0);
        CHANGED(other.placements[0].side = BoardSide::Top);
        CHANGED(other.placements[0].refAt.x += 1);
        CHANGED(other.placements[0].valueAt.y += 1);
        CHANGED(other.placements[0].showRef = true);
        CHANGED(other.placements[0].showValue = false);
        CHANGED(other.placements[0].padNets[QStringLiteral("1")] = QStringLiteral("+5V"));
        CHANGED(other.placements[0].padNets.remove(QStringLiteral("2")));
        CHANGED(other.placements.removeLast());

        CHANGED(other.tracks[0].id = id(1));
        CHANGED(other.tracks[0].side = BoardSide::Top);
        CHANGED(other.tracks[0].net.clear());
        CHANGED(other.tracks[0].width += 1);
        CHANGED(other.tracks[0].from.x += 1);
        CHANGED(other.tracks[0].to.y += 1);
        CHANGED(other.tracks.clear());

        CHANGED(other.vias[0].id = id(1));
        CHANGED(other.vias[0].at.x += 1);
        CHANGED(other.vias[0].net.clear());
        CHANGED(other.vias[0].drill += 1);
        CHANGED(other.vias[0].size += 1);
        CHANGED(other.vias.clear());

        CHANGED(other.holes[0].id = id(1));
        CHANGED(other.holes[0].at.y += 1);
        CHANGED(other.holes[0].drill += 1);
        CHANGED(other.holes[0].plated = false);
        CHANGED(other.holes[0].ring += 1);
        CHANGED(other.holes[0].net.clear());
        CHANGED(other.holes.clear());

        CHANGED(other.graphics[0].id = id(1));
        CHANGED(other.graphics[0].side = BoardSide::Top);
        CHANGED(other.graphics[0].graphic.height += 1);
        CHANGED(other.graphics.clear());

        CHANGED(other.zones[0].id = id(1));
        CHANGED(other.zones[0].side = BoardSide::Top);
        CHANGED(other.zones[0].net.clear());
        CHANGED(other.zones[0].clearance += 1);
        CHANGED(other.zones[0].points[1].x += 1);
        CHANGED(other.zones.clear());
#undef CHANGED
    }

    void fileRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("test.ppbrd"));
        const Board board = fullBoard();

        QString error;
        QVERIFY2(BoardFile::write(path, board, error), qPrintable(error));
        Board back;
        QVERIFY2(BoardFile::read(path, back, error), qPrintable(error));
        QVERIFY(back == board);

        QJsonObject root;
        QVERIFY(JsonFile::read(path, root, error));
        QCOMPARE(fileTypeOf(root), QString::fromLatin1(FILE_TYPE_BOARD));
    }

    void fileOfAnotherKindIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("schematic.ppbrd"));
        QJsonObject root = boardToJson(fullBoard());
        writeHeader(root, FILE_TYPE_SCHEMATIC);
        QString error;
        QVERIFY(JsonFile::write(path, root, error));

        Board board = fullBoard();
        QVERIFY(!BoardFile::read(path, board, error));
        QVERIFY2(error.startsWith(path), qPrintable(error));
        QVERIFY(board == fullBoard()); // untouched
    }

    // A new board: nothing on it, not even an outline.
    void emptyBoardIsAllowed()
    {
        const QJsonObject root = boardToJson(Board());
        QString error;
        const Board back = readBack(root, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(back == Board());
    }

    void everyListMustBePresent()
    {
        const char *const keys[] = {"outline", "placements", "tracks", "vias",
                                    "holes",   "graphics",   "zones"};
        for (const char *key : keys) {
            QJsonObject root = boardToJson(fullBoard());
            root.remove(QLatin1String(key));
            const QString error = errorOf(root);
            QVERIFY2(error.startsWith(QLatin1String(key) + QStringLiteral(": ")), qPrintable(error));
        }
    }

    void idsAreUniqueOnTheBoard()
    {
        // Objects of different kinds.
        QString error = errorOfChange("vias", "id", idToJson(id(0x21)));
        QVERIFY2(error.startsWith(QStringLiteral("vias[0].id: ")), qPrintable(error));

        error = errorOfChange("zones", "id", idToJson(id(0x11)));
        QVERIFY2(error.startsWith(QStringLiteral("zones[0].id: ")), qPrintable(error));

        error = errorOfChange("holes", "id", QStringLiteral("H1"));
        QVERIFY2(error.startsWith(QStringLiteral("holes[0].id: ")), qPrintable(error));

        error = errorOfChange("graphics", "id", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("graphics[0].id: ")), qPrintable(error));

        // Two placements of one component.
        error = errorOfChange("placements", 1, "component", idToJson(id(0xC1)));
        QVERIFY2(error.startsWith(QStringLiteral("placements[1].component: ")), qPrintable(error));

        // A placement's id and its component's id are different things: the
        // same number in both is fine.
        error = errorOfChange("placements", 1, "component", idToJson(id(0x12)));
        QCOMPARE(error, QString());
    }

    void outlineRules()
    {
        QString error = errorOfOutline({OutlineCorner{Point{0, 0}, 0}, OutlineCorner{Point{MM, 0}, 0}});
        QVERIFY2(error.startsWith(QStringLiteral("outline: ")), qPrintable(error));

        // The third corner repeats the second.
        error = errorOfOutline({OutlineCorner{Point{0, 0}, 0}, OutlineCorner{Point{MM, 0}, 0},
                                OutlineCorner{Point{MM, 0}, 0}, OutlineCorner{Point{MM, MM}, 0}});
        QVERIFY2(error.startsWith(QStringLiteral("outline[2]: ")), qPrintable(error));

        // The last corner repeats the first: the closing side has no length.
        error = errorOfOutline({OutlineCorner{Point{0, 0}, 0}, OutlineCorner{Point{MM, 0}, 0},
                                OutlineCorner{Point{MM, MM}, 0}, OutlineCorner{Point{0, 0}, 0}});
        QVERIFY2(error.startsWith(QStringLiteral("outline[0]: ")), qPrintable(error));

        // A triangle with a side at 45 degrees is the smallest outline.
        error = errorOfOutline({OutlineCorner{Point{0, 0}, 0}, OutlineCorner{Point{MM, 0}, 0},
                                OutlineCorner{Point{0, MM}, 0}});
        QCOMPARE(error, QString());

        // Sides run any way.
        error = errorOfOutline({OutlineCorner{Point{0, 0}, 0}, OutlineCorner{Point{4 * MM, 0}, 0},
                                OutlineCorner{Point{5 * MM, 3 * MM}, 0},
                                OutlineCorner{Point{0, 3 * MM}, 0}});
        QCOMPARE(error, QString());

        error = errorOfChange("outline", "radius", -0.1);
        QVERIFY2(error.startsWith(QStringLiteral("outline[0].radius: ")), qPrintable(error));
        error = errorOfChange("outline", "radius", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("outline[0].radius: ")), qPrintable(error));
        error = errorOfChange("outline", "at", QStringLiteral("0,0"));
        QVERIFY2(error.startsWith(QStringLiteral("outline[0].at: ")), qPrintable(error));
    }

    void placementErrors()
    {
        // The footprint is looked up in the project library, which may lack it.
        QString error = errorOfChange("placements", "footprint", QStringLiteral("dip-8"));
        QCOMPARE(error, QString());

        error = errorOfChange("placements", "footprint", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].footprint: ")), qPrintable(error));

        error = errorOfChange("placements", "component", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].component: ")), qPrintable(error));

        error = errorOfChange("placements", "designator", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].designator: ")), qPrintable(error));

        error = errorOfChange("placements", "rotation", 45);
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].rotation: ")), qPrintable(error));

        error = errorOfChange("placements", "side", QStringLiteral("left"));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].side: ")), qPrintable(error));

        error = errorOfChange("placements", "showValue", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].showValue: ")), qPrintable(error));

        // An empty value is fine.
        error = errorOfChange("placements", "value", QStringLiteral(""));
        QCOMPARE(error, QString());
        for (int rotation : {0, 90, 180, 270})
            QCOMPARE(errorOfChange("placements", "rotation", rotation), QString());
    }

    void padNetRules()
    {
        QString error = errorOfChange("placements", "nets", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].nets: ")), qPrintable(error));

        error = errorOfChange("placements", "nets", QJsonArray());
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].nets: ")), qPrintable(error));

        // Pad numbers are not checked against the footprint: it lies in the
        // project library, which may lack it or have it changed.
        error = errorOfChange("placements", "nets",
                              netsObject(QStringLiteral("3"), QStringLiteral("GND")));
        QCOMPARE(error, QString());

        // No number names no pad.
        error = errorOfChange("placements", "nets", netsObject(QString(), QStringLiteral("GND")));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].nets.: ")), qPrintable(error));

        error = errorOfChange("placements", "nets", netsObject(QStringLiteral("1"), 5));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].nets.1: ")), qPrintable(error));

        error = errorOfChange("placements", "nets",
                              netsObject(QStringLiteral("1"), QStringLiteral("")));
        QVERIFY2(error.startsWith(QStringLiteral("placements[0].nets.1: ")), qPrintable(error));

        // No pad has a net: fine.
        error = errorOfChange("placements", "nets", QJsonObject());
        QCOMPARE(error, QString());
    }

    void trackAndViaRules()
    {
        QString error = errorOfChange("tracks", "width", 0);
        QVERIFY2(error.startsWith(QStringLiteral("tracks[0].width: ")), qPrintable(error));

        error = errorOfChange("tracks", "side", QStringLiteral("both"));
        QVERIFY2(error.startsWith(QStringLiteral("tracks[0].side: ")), qPrintable(error));

        error = errorOfChange("tracks", "net", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("tracks[0].net: ")), qPrintable(error));

        // The track of the full board starts at 5, 5.
        error = errorOfChange("tracks", "to", pointToJson(Point{5 * MM, 5 * MM}));
        QVERIFY2(error.startsWith(QStringLiteral("tracks[0].to: ")), qPrintable(error));

        // A track that belongs to no net yet.
        error = errorOfChange("tracks", "net", QStringLiteral(""));
        QCOMPARE(error, QString());

        error = errorOfChange("vias", "drill", 0);
        QVERIFY2(error.startsWith(QStringLiteral("vias[0].drill: ")), qPrintable(error));

        // The via of the full board is 1.2 mm across.
        error = errorOfChange("vias", "drill", 1.2);
        QVERIFY2(error.startsWith(QStringLiteral("vias[0].drill: ")), qPrintable(error));
        error = errorOfChange("vias", "drill", 1.5);
        QVERIFY2(error.startsWith(QStringLiteral("vias[0].drill: ")), qPrintable(error));
        error = errorOfChange("vias", "drill", 1.1);
        QCOMPARE(error, QString());

        error = errorOfChange("vias", "size", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("vias[0].size: ")), qPrintable(error));
    }

    void mountingHoleRules()
    {
        // The hole of the full board is plated, with a ring and a net.
        QString error = errorOfChange("holes", "ring", 0);
        QVERIFY2(error.startsWith(QStringLiteral("holes[0].ring: ")), qPrintable(error));

        error = errorOfChange("holes", "ring", -1);
        QVERIFY2(error.startsWith(QStringLiteral("holes[0].ring: ")), qPrintable(error));

        error = errorOfChange("holes", "drill", 0);
        QVERIFY2(error.startsWith(QStringLiteral("holes[0].drill: ")), qPrintable(error));

        // Without plating the ring must go.
        error = errorOfChange("holes", "plated", false);
        QVERIFY2(error.startsWith(QStringLiteral("holes[0].ring: ")), qPrintable(error));

        // A plated hole may be left without a net.
        error = errorOfChange("holes", "net", QStringLiteral(""));
        QCOMPARE(error, QString());

        // Without plating: no ring, and then no net either.
        QJsonObject root = boardToJson(fullBoard());
        QJsonArray holes = root.value(QStringLiteral("holes")).toArray();
        QJsonObject hole = holes.at(0).toObject();
        hole.insert(QStringLiteral("plated"), false);
        hole.insert(QStringLiteral("ring"), 0);
        holes.replace(0, hole);
        root.insert(QStringLiteral("holes"), holes);
        error = errorOf(root);
        QVERIFY2(error.startsWith(QStringLiteral("holes[0].net: ")), qPrintable(error));

        hole.insert(QStringLiteral("net"), QStringLiteral(""));
        holes.replace(0, hole);
        root.insert(QStringLiteral("holes"), holes);
        QCOMPARE(errorOf(root), QString());
    }

    void graphicAndZoneRules()
    {
        QString error = errorOfChange("graphics", "kind", QStringLiteral("spline"));
        QVERIFY2(error.startsWith(QStringLiteral("graphics[0].kind: ")), qPrintable(error));

        error = errorOfChange("graphics", "side", QJsonValue(QJsonValue::Undefined));
        QVERIFY2(error.startsWith(QStringLiteral("graphics[0].side: ")), qPrintable(error));

        error = errorOfChange("graphics", "text", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("graphics[0].text: ")), qPrintable(error));

        error = errorOfChange("zones", "points", pointArray({Point{0, 0}, Point{MM, 0}}));
        QVERIFY2(error.startsWith(QStringLiteral("zones[0].points: ")), qPrintable(error));

        error = errorOfChange("zones", "clearance", -0.1);
        QVERIFY2(error.startsWith(QStringLiteral("zones[0].clearance: ")), qPrintable(error));

        error = errorOfChange("zones", "side", QStringLiteral("inner"));
        QVERIFY2(error.startsWith(QStringLiteral("zones[0].side: ")), qPrintable(error));

        // No clearance and no net are both fine.
        error = errorOfChange("zones", "clearance", 0);
        QCOMPARE(error, QString());
        error = errorOfChange("zones", "net", QStringLiteral(""));
        QCOMPARE(error, QString());
    }
};

QTEST_GUILESS_MAIN(TestBoard)
#include "test_board.moc"
