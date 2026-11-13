#include "board.h"

namespace ppcb {

namespace {

const char KEY_FOOTPRINTS[] = "footprints";
const char KEY_OUTLINE[] = "outline";
const char KEY_PLACEMENTS[] = "placements";
const char KEY_TRACKS[] = "tracks";
const char KEY_VIAS[] = "vias";
const char KEY_HOLES[] = "holes";
const char KEY_GRAPHICS[] = "graphics";
const char KEY_ZONES[] = "zones";

const char KEY_ID[] = "id";
const char KEY_AT[] = "at";
const char KEY_SIDE[] = "side";
const char KEY_NET[] = "net";
const char KEY_RADIUS[] = "radius";

const char KEY_COMPONENT[] = "component";
const char KEY_FOOTPRINT[] = "footprint";
const char KEY_DESIGNATOR[] = "designator";
const char KEY_VALUE[] = "value";
const char KEY_ROTATION[] = "rotation";
const char KEY_REF_AT[] = "refAt";
const char KEY_VALUE_AT[] = "valueAt";
const char KEY_SHOW_REF[] = "showRef";
const char KEY_SHOW_VALUE[] = "showValue";
const char KEY_NETS[] = "nets";

const char KEY_WIDTH[] = "width";
const char KEY_FROM[] = "from";
const char KEY_TO[] = "to";
const char KEY_DRILL[] = "drill";
const char KEY_SIZE[] = "size";
const char KEY_PLATED[] = "plated";
const char KEY_RING[] = "ring";
const char KEY_CLEARANCE[] = "clearance";
const char KEY_POINTS[] = "points";

// An outline, of the board or of a zone.
const int MIN_OUTLINE_POINTS = 3;

// In the order of BoardSide.
const QStringList &sideNames()
{
    static const QStringList names = {QStringLiteral("top"), QStringLiteral("bottom")};
    return names;
}

// What reading needs to know about the file as a whole.
struct ReadContext
{
    JsonReader &reader;
    // Every id met so far: ids are unique in the whole board.
    QSet<ObjectId> ids;
    // Schematic components that already have a placement.
    QSet<ObjectId> components;
    // Pad numbers of every footprint in the table, by footprint name.
    QMap<QString, QStringList> padNumbers;
};

QJsonValue sideToJson(BoardSide side)
{
    return sideNames().at(int(side));
}

BoardSide sideFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    return BoardSide(reader.choice(object, KEY_SIDE, where, sideNames()));
}

QJsonObject cornerToJson(const OutlineCorner &corner)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_AT), pointToJson(corner.at));
    object.insert(QLatin1String(KEY_RADIUS), mmToJson(corner.radius));
    return object;
}

OutlineCorner cornerFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    OutlineCorner corner;
    corner.at = context.reader.point(object, KEY_AT, where);
    corner.radius = context.reader.nonNegativeMm(object, KEY_RADIUS, where);
    return corner;
}

// The outline is closed, so the side that ends at corner 0 starts at the
// last corner.
void checkOutline(const QList<OutlineCorner> &outline, JsonReader &reader)
{
    const QString where = QLatin1String(KEY_OUTLINE);
    if (!reader.ok() || outline.isEmpty())
        return;
    if (outline.size() < MIN_OUTLINE_POINTS) {
        reader.fail(where, JsonReader::tr("at least three points are expected"));
        return;
    }
    for (int i = 0; i < outline.size(); ++i) {
        const int previous = (i + outline.size() - 1) % outline.size();
        const Point step = outline.at(i).at - outline.at(previous).at;
        const QString stepWhere = JsonReader::pathOf(where, i);
        if (step == Point()) {
            reader.fail(stepWhere, JsonReader::tr("neighbouring points coincide"));
        } else if (!isStraightOrDiagonal(step)) {
            reader.fail(stepWhere, JsonReader::tr("a segment is not at 0 45 or 90 degrees"));
        }
    }
}

QJsonObject placementToJson(const Placement &placement)
{
    QJsonObject nets;
    for (auto it = placement.padNets.constBegin(); it != placement.padNets.constEnd(); ++it)
        nets.insert(it.key(), it.value());

    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(placement.id));
    object.insert(QLatin1String(KEY_COMPONENT), idToJson(placement.component));
    object.insert(QLatin1String(KEY_FOOTPRINT), placement.footprint);
    object.insert(QLatin1String(KEY_DESIGNATOR), placement.designator);
    object.insert(QLatin1String(KEY_VALUE), placement.value);
    object.insert(QLatin1String(KEY_AT), pointToJson(placement.at));
    object.insert(QLatin1String(KEY_ROTATION), placement.rotation);
    object.insert(QLatin1String(KEY_SIDE), sideToJson(placement.side));
    object.insert(QLatin1String(KEY_REF_AT), pointToJson(placement.refAt));
    object.insert(QLatin1String(KEY_VALUE_AT), pointToJson(placement.valueAt));
    object.insert(QLatin1String(KEY_SHOW_REF), placement.showRef);
    object.insert(QLatin1String(KEY_SHOW_VALUE), placement.showValue);
    object.insert(QLatin1String(KEY_NETS), nets);
    return object;
}

// The nets of a placement's pads: an object of net names by pad number.
QMap<QString, QString> padNetsFromJson(const QJsonObject &object, const QString &where,
                                       const QStringList &padNumbers, JsonReader &reader)
{
    const QMap<QString, QString> padNets = reader.nameMap(object, KEY_NETS, where);
    const QString netsWhere = JsonReader::pathOf(where, KEY_NETS);
    for (auto it = padNets.constBegin(); it != padNets.constEnd(); ++it) {
        // A pad without a number is tied to no pin, so it cannot be named here.
        if (it.key().isEmpty() || !padNumbers.contains(it.key())) {
            reader.fail(JsonReader::pathOf(netsWhere, it.key()),
                        JsonReader::tr("the footprint has no pad with this number"));
        }
    }
    return padNets;
}

Placement placementFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    Placement placement;
    placement.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    // One placement for a component.
    placement.component = reader.uniqueId(object, KEY_COMPONENT, where, context.components);
    placement.footprint = reader.nonEmptyString(object, KEY_FOOTPRINT, where);
    if (reader.ok() && !context.padNumbers.contains(placement.footprint)) {
        reader.fail(JsonReader::pathOf(where, KEY_FOOTPRINT),
                    JsonReader::tr("the footprint is not in the list of footprints"));
    }
    placement.designator = reader.nonEmptyString(object, KEY_DESIGNATOR, where);
    placement.value = reader.string(object, KEY_VALUE, where);
    placement.at = reader.point(object, KEY_AT, where);
    placement.rotation = reader.rightAngle(object, KEY_ROTATION, where);
    placement.side = sideFromJson(object, where, reader);
    placement.refAt = reader.point(object, KEY_REF_AT, where);
    placement.valueAt = reader.point(object, KEY_VALUE_AT, where);
    placement.showRef = reader.boolean(object, KEY_SHOW_REF, where);
    placement.showValue = reader.boolean(object, KEY_SHOW_VALUE, where);
    // After a failure the footprint may be unknown, and so its pads.
    if (reader.ok()) {
        placement.padNets = padNetsFromJson(object, where,
                                            context.padNumbers.value(placement.footprint), reader);
    }
    return placement;
}

QJsonObject trackToJson(const Track &track)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(track.id));
    object.insert(QLatin1String(KEY_SIDE), sideToJson(track.side));
    object.insert(QLatin1String(KEY_NET), track.net);
    object.insert(QLatin1String(KEY_WIDTH), mmToJson(track.width));
    object.insert(QLatin1String(KEY_FROM), pointToJson(track.from));
    object.insert(QLatin1String(KEY_TO), pointToJson(track.to));
    return object;
}

Track trackFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    Track track;
    track.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    track.side = sideFromJson(object, where, reader);
    track.net = reader.string(object, KEY_NET, where);
    track.width = reader.positiveMm(object, KEY_WIDTH, where);
    track.from = reader.point(object, KEY_FROM, where);
    track.to = reader.point(object, KEY_TO, where);
    if (reader.ok() && track.from == track.to)
        reader.fail(JsonReader::pathOf(where, KEY_TO), JsonReader::tr("the ends of a track coincide"));
    return track;
}

QJsonObject viaToJson(const Via &via)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(via.id));
    object.insert(QLatin1String(KEY_AT), pointToJson(via.at));
    object.insert(QLatin1String(KEY_NET), via.net);
    object.insert(QLatin1String(KEY_DRILL), mmToJson(via.drill));
    object.insert(QLatin1String(KEY_SIZE), mmToJson(via.size));
    return object;
}

Via viaFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    Via via;
    via.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    via.at = reader.point(object, KEY_AT, where);
    via.net = reader.string(object, KEY_NET, where);
    via.drill = reader.positiveMm(object, KEY_DRILL, where);
    via.size = reader.positiveMm(object, KEY_SIZE, where);
    if (reader.ok() && via.drill >= via.size) {
        reader.fail(JsonReader::pathOf(where, KEY_DRILL),
                    JsonReader::tr("the hole must be smaller than the via"));
    }
    return via;
}

QJsonObject holeToJson(const MountingHole &hole)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(hole.id));
    object.insert(QLatin1String(KEY_AT), pointToJson(hole.at));
    object.insert(QLatin1String(KEY_DRILL), mmToJson(hole.drill));
    object.insert(QLatin1String(KEY_PLATED), hole.plated);
    object.insert(QLatin1String(KEY_RING), mmToJson(hole.ring));
    object.insert(QLatin1String(KEY_NET), hole.net);
    return object;
}

MountingHole holeFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    MountingHole hole;
    hole.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    hole.at = reader.point(object, KEY_AT, where);
    hole.drill = reader.positiveMm(object, KEY_DRILL, where);
    hole.plated = reader.boolean(object, KEY_PLATED, where);
    hole.ring = reader.nonNegativeMm(object, KEY_RING, where);
    hole.net = reader.string(object, KEY_NET, where);
    if (!reader.ok())
        return hole;

    if (hole.plated && hole.ring == 0) {
        reader.fail(JsonReader::pathOf(where, KEY_RING),
                    JsonReader::tr("a plated hole needs a copper ring"));
    } else if (!hole.plated && hole.ring != 0) {
        reader.fail(JsonReader::pathOf(where, KEY_RING),
                    JsonReader::tr("a hole without plating has no copper ring"));
    } else if (!hole.plated && !hole.net.isEmpty()) {
        reader.fail(JsonReader::pathOf(where, KEY_NET),
                    JsonReader::tr("a hole without plating has no net"));
    }
    return hole;
}

// A graphic's own fields with the id and the side beside them, in one object.
QJsonObject boardGraphicToJson(const BoardGraphic &item)
{
    QJsonObject object = graphicToJson(item.graphic);
    object.insert(QLatin1String(KEY_ID), idToJson(item.id));
    object.insert(QLatin1String(KEY_SIDE), sideToJson(item.side));
    return object;
}

BoardGraphic boardGraphicFromJson(const QJsonObject &object, const QString &where,
                                  ReadContext &context)
{
    BoardGraphic item;
    item.id = context.reader.uniqueId(object, KEY_ID, where, context.ids);
    item.side = sideFromJson(object, where, context.reader);
    item.graphic = graphicFromJson(object, where, context.reader);
    return item;
}

QJsonObject zoneToJson(const Zone &zone)
{
    QJsonArray points;
    for (const Point &point : zone.points)
        points.append(pointToJson(point));

    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(zone.id));
    object.insert(QLatin1String(KEY_SIDE), sideToJson(zone.side));
    object.insert(QLatin1String(KEY_NET), zone.net);
    object.insert(QLatin1String(KEY_CLEARANCE), mmToJson(zone.clearance));
    object.insert(QLatin1String(KEY_POINTS), points);
    return object;
}

Zone zoneFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    Zone zone;
    zone.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    zone.side = sideFromJson(object, where, reader);
    zone.net = reader.string(object, KEY_NET, where);
    zone.clearance = reader.nonNegativeMm(object, KEY_CLEARANCE, where);
    zone.points = reader.pointList(object, KEY_POINTS, where);
    if (reader.ok() && zone.points.size() < MIN_OUTLINE_POINTS) {
        reader.fail(JsonReader::pathOf(where, KEY_POINTS),
                    JsonReader::tr("at least three points are expected"));
    }
    return zone;
}

} // namespace

bool operator==(const OutlineCorner &a, const OutlineCorner &b)
{
    return a.at == b.at && a.radius == b.radius;
}

bool operator==(const Placement &a, const Placement &b)
{
    return a.id == b.id && a.component == b.component && a.footprint == b.footprint
           && a.designator == b.designator && a.value == b.value && a.at == b.at
           && a.rotation == b.rotation && a.side == b.side && a.refAt == b.refAt
           && a.valueAt == b.valueAt && a.showRef == b.showRef && a.showValue == b.showValue
           && a.padNets == b.padNets;
}

bool operator==(const Track &a, const Track &b)
{
    return a.id == b.id && a.side == b.side && a.net == b.net && a.width == b.width
           && a.from == b.from && a.to == b.to;
}

bool operator==(const Via &a, const Via &b)
{
    return a.id == b.id && a.at == b.at && a.net == b.net && a.drill == b.drill
           && a.size == b.size;
}

bool operator==(const MountingHole &a, const MountingHole &b)
{
    return a.id == b.id && a.at == b.at && a.drill == b.drill && a.plated == b.plated
           && a.ring == b.ring && a.net == b.net;
}

bool operator==(const BoardGraphic &a, const BoardGraphic &b)
{
    return a.id == b.id && a.side == b.side && a.graphic == b.graphic;
}

bool operator==(const Zone &a, const Zone &b)
{
    return a.id == b.id && a.side == b.side && a.net == b.net && a.clearance == b.clearance
           && a.points == b.points;
}

bool operator==(const Board &a, const Board &b)
{
    return a.footprints == b.footprints && a.outline == b.outline
           && a.placements == b.placements && a.tracks == b.tracks && a.vias == b.vias
           && a.holes == b.holes && a.graphics == b.graphics && a.zones == b.zones;
}

QJsonObject boardToJson(const Board &board)
{
    QJsonObject footprints;
    for (auto it = board.footprints.constBegin(); it != board.footprints.constEnd(); ++it)
        footprints.insert(it.key(), footprintToJson(it.value()));

    QJsonObject root;
    root.insert(QLatin1String(KEY_FOOTPRINTS), footprints);
    root.insert(QLatin1String(KEY_OUTLINE), listToJson(board.outline, cornerToJson));
    root.insert(QLatin1String(KEY_PLACEMENTS), listToJson(board.placements, placementToJson));
    root.insert(QLatin1String(KEY_TRACKS), listToJson(board.tracks, trackToJson));
    root.insert(QLatin1String(KEY_VIAS), listToJson(board.vias, viaToJson));
    root.insert(QLatin1String(KEY_HOLES), listToJson(board.holes, holeToJson));
    root.insert(QLatin1String(KEY_GRAPHICS), listToJson(board.graphics, boardGraphicToJson));
    root.insert(QLatin1String(KEY_ZONES), listToJson(board.zones, zoneToJson));
    return root;
}

Board boardFromJson(const QJsonObject &root, JsonReader &reader)
{
    Board board;
    ReadContext context{reader, {}, {}, {}};

    // Footprints first: placements are checked against their names and pads.
    const QMap<QString, QJsonObject> footprints = reader.objectMap(root, KEY_FOOTPRINTS, QString());
    for (auto it = footprints.constBegin(); it != footprints.constEnd(); ++it) {
        const QString where = JsonReader::pathOf(QLatin1String(KEY_FOOTPRINTS), it.key());
        const Footprint footprint = footprintFromJson(it.value(), where, reader);
        board.footprints.insert(it.key(), footprint);

        QStringList numbers;
        for (const Pad &pad : footprint.pads)
            numbers.append(pad.number);
        context.padNumbers.insert(it.key(), numbers);
    }

    board.outline = listFromJson<OutlineCorner>(root, KEY_OUTLINE, QString(), context,
                                                cornerFromJson);
    checkOutline(board.outline, reader);
    board.placements = listFromJson<Placement>(root, KEY_PLACEMENTS, QString(), context,
                                               placementFromJson);
    board.tracks = listFromJson<Track>(root, KEY_TRACKS, QString(), context, trackFromJson);
    board.vias = listFromJson<Via>(root, KEY_VIAS, QString(), context, viaFromJson);
    board.holes = listFromJson<MountingHole>(root, KEY_HOLES, QString(), context, holeFromJson);
    board.graphics = listFromJson<BoardGraphic>(root, KEY_GRAPHICS, QString(), context,
                                                boardGraphicFromJson);
    board.zones = listFromJson<Zone>(root, KEY_ZONES, QString(), context, zoneFromJson);
    return board;
}

bool BoardFile::read(const QString &path, Board &board, QString &error)
{
    QJsonObject root;
    if (!JsonFile::read(path, root, error))
        return false;

    JsonReader reader;
    reader.header(root, FILE_TYPE_BOARD);
    const Board result = boardFromJson(root, reader);
    if (!reader.ok()) {
        error = withPlace(path, reader.error());
        return false;
    }
    board = result;
    return true;
}

bool BoardFile::write(const QString &path, const Board &board, QString &error)
{
    QJsonObject root = boardToJson(board);
    writeHeader(root, FILE_TYPE_BOARD);
    return JsonFile::write(path, root, error);
}

} // namespace ppcb
