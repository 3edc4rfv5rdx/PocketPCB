#include "board.h"

namespace ppcb {

BoardLayer copperLayer(BoardSide side)
{
    return side == BoardSide::Top ? BoardLayer::CopperTop : BoardLayer::CopperBottom;
}

BoardLayer maskLayer(BoardSide side)
{
    return side == BoardSide::Top ? BoardLayer::MaskTop : BoardLayer::MaskBottom;
}

BoardLayer silkscreenLayer(BoardSide side)
{
    return side == BoardSide::Top ? BoardLayer::SilkscreenTop : BoardLayer::SilkscreenBottom;
}

BoardLayer pasteLayer(BoardSide side)
{
    return side == BoardSide::Top ? BoardLayer::PasteTop : BoardLayer::PasteBottom;
}

namespace {

// Optional: files written before it have copper on both sides.
const char KEY_COPPER[] = "copper";
const char KEY_OUTLINE[] = "outline";
// Optional: files written before it have an unlocked outline.
const char KEY_OUTLINE_LOCKED[] = "outlineLocked";
const char KEY_PLACEMENTS[] = "placements";
const char KEY_TRACKS[] = "tracks";
const char KEY_VIAS[] = "vias";
const char KEY_LINKS[] = "wireLinks";
const char KEY_INSULATED[] = "insulated";
// Optional: a wire link's designator where it was laid.
const char KEY_TEXT_AT[] = "textAt";
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
const char KEY_LOCKED[] = "locked";
const char KEY_AUTOROUTED[] = "auto";
const char KEY_REF_LINE[] = "refLine";
const char KEY_VALUE_LINE[] = "valueLine";
const char KEY_NETS[] = "nets";

const char KEY_WIDTH[] = "width";
const char KEY_FROM[] = "from";
const char KEY_TO[] = "to";
const char KEY_DRILL[] = "drill";
const char KEY_SIZE[] = "size";
const char KEY_PLATED[] = "plated";
const char KEY_RING[] = "ring";
// Optional: files written before it have pads on both sides.
const char KEY_PAD_SIDES[] = "padSides";
const char KEY_CLEARANCE[] = "clearance";
const char KEY_POINTS[] = "points";

// An outline, of the board or of a zone.
const int MIN_OUTLINE_POINTS = 3;

// In the order of CopperSides.
const QStringList &copperSidesNames()
{
    static const QStringList names = {QStringLiteral("both"), QStringLiteral("top"),
                                      QStringLiteral("bottom")};
    return names;
}

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
        if (step == Point())
            reader.fail(stepWhere, JsonReader::tr("neighbouring points coincide"));
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
    object.insert(QLatin1String(KEY_REF_LINE), placement.refLine);
    object.insert(QLatin1String(KEY_VALUE_LINE), placement.valueLine);
    object.insert(QLatin1String(KEY_LOCKED), placement.locked);
    object.insert(QLatin1String(KEY_NETS), nets);
    return object;
}

// The nets of a placement's pads: an object of net names by pad number.
// The numbers are not checked against the footprint: it lies in the project
// library and may be missing or changed there.
QMap<QString, QString> padNetsFromJson(const QJsonObject &object, const QString &where,
                                       JsonReader &reader)
{
    const QMap<QString, QString> padNets = reader.nameMap(object, KEY_NETS, where);
    const QString netsWhere = JsonReader::pathOf(where, KEY_NETS);
    // A pad without a number is tied to no pin, so it cannot be named here.
    if (padNets.contains(QString())) {
        reader.fail(JsonReader::pathOf(netsWhere, QString()),
                    JsonReader::tr("a pad number is expected"));
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
    placement.designator = reader.nonEmptyString(object, KEY_DESIGNATOR, where);
    placement.value = reader.string(object, KEY_VALUE, where);
    placement.at = reader.point(object, KEY_AT, where);
    placement.rotation = reader.rightAngle(object, KEY_ROTATION, where);
    placement.side = sideFromJson(object, where, reader);
    placement.refAt = reader.point(object, KEY_REF_AT, where);
    placement.valueAt = reader.point(object, KEY_VALUE_AT, where);
    placement.showRef = reader.boolean(object, KEY_SHOW_REF, where);
    placement.showValue = reader.boolean(object, KEY_SHOW_VALUE, where);
    if (object.contains(QLatin1String(KEY_LOCKED)))
        placement.locked = reader.boolean(object, KEY_LOCKED, where);
    // Optional: files written before them have no lines.
    if (object.contains(QLatin1String(KEY_REF_LINE)))
        placement.refLine = reader.boolean(object, KEY_REF_LINE, where);
    if (object.contains(QLatin1String(KEY_VALUE_LINE)))
        placement.valueLine = reader.boolean(object, KEY_VALUE_LINE, where);
    placement.padNets = padNetsFromJson(object, where, reader);
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
    if (track.autorouted)
        object.insert(QLatin1String(KEY_AUTOROUTED), true);
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
    // Optional: laid by hand when it is not there.
    if (object.contains(QLatin1String(KEY_AUTOROUTED)))
        track.autorouted = reader.boolean(object, KEY_AUTOROUTED, where);
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
    if (via.autorouted)
        object.insert(QLatin1String(KEY_AUTOROUTED), true);
    return object;
}

QJsonObject linkToJson(const WireLink &link)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(link.id));
    object.insert(QLatin1String(KEY_NET), link.net);
    object.insert(QLatin1String(KEY_FROM), pointToJson(link.from));
    object.insert(QLatin1String(KEY_TO), pointToJson(link.to));
    if (!link.designator.isEmpty())
        object.insert(QLatin1String(KEY_DESIGNATOR), link.designator);
    if (link.textAt != Point())
        object.insert(QLatin1String(KEY_TEXT_AT), pointToJson(link.textAt));
    object.insert(QLatin1String(KEY_DRILL), mmToJson(link.drill));
    object.insert(QLatin1String(KEY_SIZE), mmToJson(link.size));
    if (link.insulated)
        object.insert(QLatin1String(KEY_INSULATED), true);
    if (link.autorouted)
        object.insert(QLatin1String(KEY_AUTOROUTED), true);
    return object;
}

WireLink linkFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    WireLink link;
    link.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    link.net = reader.string(object, KEY_NET, where);
    link.from = reader.point(object, KEY_FROM, where);
    link.to = reader.point(object, KEY_TO, where);
    if (object.contains(QLatin1String(KEY_DESIGNATOR)))
        link.designator = reader.string(object, KEY_DESIGNATOR, where);
    if (object.contains(QLatin1String(KEY_TEXT_AT)))
        link.textAt = reader.point(object, KEY_TEXT_AT, where);
    link.drill = reader.positiveMm(object, KEY_DRILL, where);
    link.size = reader.positiveMm(object, KEY_SIZE, where);
    if (object.contains(QLatin1String(KEY_INSULATED)))
        link.insulated = reader.boolean(object, KEY_INSULATED, where);
    if (object.contains(QLatin1String(KEY_AUTOROUTED)))
        link.autorouted = reader.boolean(object, KEY_AUTOROUTED, where);
    if (reader.ok() && link.from == link.to)
        reader.fail(JsonReader::pathOf(where, KEY_TO),
                    JsonReader::tr("the ends of a wire link coincide"));
    if (reader.ok() && link.drill >= link.size)
        reader.fail(JsonReader::pathOf(where, KEY_DRILL),
                    JsonReader::tr("the hole must be smaller than the pad"));
    return link;
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
    if (object.contains(QLatin1String(KEY_AUTOROUTED)))
        via.autorouted = reader.boolean(object, KEY_AUTOROUTED, where);
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
    object.insert(QLatin1String(KEY_PAD_SIDES), copperSidesNames().at(int(hole.padSides)));
    object.insert(QLatin1String(KEY_NET), hole.net);
    object.insert(QLatin1String(KEY_LOCKED), hole.locked);
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
    if (object.contains(QLatin1String(KEY_PAD_SIDES)))
        hole.padSides =
            CopperSides(reader.choice(object, KEY_PAD_SIDES, where, copperSidesNames()));
    // Optional: files written before it have unlocked holes.
    if (object.contains(QLatin1String(KEY_LOCKED)))
        hole.locked = reader.boolean(object, KEY_LOCKED, where);
    if (!reader.ok())
        return hole;

    if (hole.plated && hole.ring == 0) {
        reader.fail(JsonReader::pathOf(where, KEY_RING),
                    JsonReader::tr("a plated hole needs a copper ring"));
    } else if (hole.plated && hole.padSides != CopperSides::Both) {
        reader.fail(JsonReader::pathOf(where, KEY_PAD_SIDES),
                    JsonReader::tr("a plated hole has its pad on both sides"));
    } else if (hole.ring == 0 && !hole.net.isEmpty()) {
        reader.fail(JsonReader::pathOf(where, KEY_NET),
                    JsonReader::tr("a hole without a copper ring has no net"));
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
           && a.refLine == b.refLine && a.valueLine == b.valueLine && a.locked == b.locked
           && a.padNets == b.padNets;
}

bool operator==(const Track &a, const Track &b)
{
    return a.id == b.id && a.side == b.side && a.net == b.net && a.width == b.width
           && a.from == b.from && a.to == b.to && a.autorouted == b.autorouted;
}

bool operator==(const WireLink &a, const WireLink &b)
{
    return a.id == b.id && a.from == b.from && a.to == b.to && a.net == b.net
           && a.designator == b.designator && a.textAt == b.textAt && a.drill == b.drill
           && a.size == b.size
           && a.insulated == b.insulated && a.autorouted == b.autorouted;
}

bool operator==(const Via &a, const Via &b)
{
    return a.id == b.id && a.at == b.at && a.net == b.net && a.drill == b.drill
           && a.size == b.size && a.autorouted == b.autorouted;
}

Coord holeOuterDiameter(const MountingHole &hole)
{
    return hole.drill + 2 * hole.ring;
}

bool copperOn(CopperSides sides, BoardSide side)
{
    return sides == CopperSides::Both
           || (sides == CopperSides::Top) == (side == BoardSide::Top);
}

CopperSides padCopperSides(PadLayers layers, BoardSide partSide)
{
    switch (layers) {
    case PadLayers::Both:
        return CopperSides::Both;
    case PadLayers::Front:
        return partSide == BoardSide::Top ? CopperSides::Top : CopperSides::Bottom;
    case PadLayers::Back:
        return partSide == BoardSide::Top ? CopperSides::Bottom : CopperSides::Top;
    }
    return CopperSides::Both;
}

bool holePadOn(const Board &board, const MountingHole &hole, BoardSide side)
{
    if (hole.ring == 0 || !copperOn(board.copper, side))
        return false;
    return board.copper != CopperSides::Both || copperOn(hole.padSides, side);
}

bool operator==(const MountingHole &a, const MountingHole &b)
{
    return a.id == b.id && a.at == b.at && a.drill == b.drill && a.plated == b.plated
           && a.ring == b.ring && a.padSides == b.padSides && a.net == b.net
           && a.locked == b.locked;
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
    return a.copper == b.copper && a.outline == b.outline
           && a.outlineLocked == b.outlineLocked
           && a.placements == b.placements && a.tracks == b.tracks && a.vias == b.vias
           && a.links == b.links && a.holes == b.holes && a.graphics == b.graphics
           && a.zones == b.zones;
}

QJsonObject boardToJson(const Board &board)
{
    QJsonObject root;
    root.insert(QLatin1String(KEY_COPPER), copperSidesNames().at(int(board.copper)));
    root.insert(QLatin1String(KEY_OUTLINE), listToJson(board.outline, cornerToJson));
    root.insert(QLatin1String(KEY_OUTLINE_LOCKED), board.outlineLocked);
    root.insert(QLatin1String(KEY_PLACEMENTS), listToJson(board.placements, placementToJson));
    root.insert(QLatin1String(KEY_TRACKS), listToJson(board.tracks, trackToJson));
    root.insert(QLatin1String(KEY_VIAS), listToJson(board.vias, viaToJson));
    root.insert(QLatin1String(KEY_LINKS), listToJson(board.links, linkToJson));
    root.insert(QLatin1String(KEY_HOLES), listToJson(board.holes, holeToJson));
    root.insert(QLatin1String(KEY_GRAPHICS), listToJson(board.graphics, boardGraphicToJson));
    root.insert(QLatin1String(KEY_ZONES), listToJson(board.zones, zoneToJson));
    return root;
}

Board boardFromJson(const QJsonObject &root, JsonReader &reader)
{
    Board board;
    ReadContext context{reader, {}, {}};

    if (root.contains(QLatin1String(KEY_COPPER)))
        board.copper = CopperSides(reader.choice(root, KEY_COPPER, QString(), copperSidesNames()));

    board.outline = listFromJson<OutlineCorner>(root, KEY_OUTLINE, QString(), context,
                                                cornerFromJson);
    checkOutline(board.outline, reader);
    if (root.contains(QLatin1String(KEY_OUTLINE_LOCKED)))
        board.outlineLocked = reader.boolean(root, KEY_OUTLINE_LOCKED, QString());
    board.placements = listFromJson<Placement>(root, KEY_PLACEMENTS, QString(), context,
                                               placementFromJson);
    board.tracks = listFromJson<Track>(root, KEY_TRACKS, QString(), context, trackFromJson);
    board.vias = listFromJson<Via>(root, KEY_VIAS, QString(), context, viaFromJson);
    // Optional: files written before them have none.
    if (root.contains(QLatin1String(KEY_LINKS)))
        board.links = listFromJson<WireLink>(root, KEY_LINKS, QString(), context, linkFromJson);
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
