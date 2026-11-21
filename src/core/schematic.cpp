#include "schematic.h"

#include "part.h"

namespace ppcb {

namespace {

const char KEY_SHEETS[] = "sheets";

const char KEY_ID[] = "id";
const char KEY_AT[] = "at";
const char KEY_ROTATION[] = "rotation";

const char KEY_TITLE[] = "title";
const char KEY_PAPER[] = "paper";
const char KEY_PORTRAIT[] = "portrait";
const char KEY_COMPONENTS[] = "components";
const char KEY_WIRES[] = "wires";
const char KEY_BUSES[] = "buses";
const char KEY_LABELS[] = "labels";
const char KEY_POWER[] = "power";
const char KEY_NO_CONNECTS[] = "noConnects";
const char KEY_GRAPHICS[] = "graphics";

const char KEY_PART[] = "part";
const char KEY_FOOTPRINT[] = "footprint";
const char KEY_DESIGNATOR[] = "designator";
const char KEY_VALUE[] = "value";
const char KEY_OFF_BOARD[] = "offBoard";
const char KEY_POWER_MW[] = "powerMw";
const char KEY_COMMENT[] = "comment";
const char KEY_MIRRORED[] = "mirrored";
const char KEY_LOCKED[] = "locked";
const char KEY_REF_AT[] = "refAt";
const char KEY_VALUE_AT[] = "valueAt";

const char KEY_POINTS[] = "points";
const char KEY_TEXT[] = "text";
const char KEY_TEXT_AT[] = "textAt";
const char KEY_SCOPE[] = "scope";
const char KEY_NET_CLASS[] = "netClass";
const char KEY_NAME[] = "name";
const char KEY_NAME_AT[] = "nameAt";
const char KEY_NAMES_AT[] = "namesAt";
const char KEY_SYMBOL[] = "symbol";

// A wire or a bus.
const int MIN_CHAIN_POINTS = 2;

// In the order of LabelScope.
const QStringList &scopeNames()
{
    static const QStringList names = {QStringLiteral("global"), QStringLiteral("local")};
    return names;
}

// In the order of PaperSize.
const QStringList &paperNames()
{
    static const QStringList names = {QStringLiteral("A4"), QStringLiteral("A3")};
    return names;
}

// What reading needs to know about the file as a whole.
struct ReadContext
{
    JsonReader &reader;
    // Every id met so far: ids are unique in the whole schematic.
    QSet<ObjectId> ids;
};

QJsonObject componentToJson(const Component &component)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(component.id));
    object.insert(QLatin1String(KEY_PART), component.part);
    object.insert(QLatin1String(KEY_FOOTPRINT), component.footprint);
    object.insert(QLatin1String(KEY_DESIGNATOR), component.designator);
    object.insert(QLatin1String(KEY_VALUE), component.value);
    object.insert(QLatin1String(KEY_OFF_BOARD), component.offBoard);
    if (component.powerMw > 0)
        object.insert(QLatin1String(KEY_POWER_MW), component.powerMw);
    if (!component.comment.isEmpty())
        object.insert(QLatin1String(KEY_COMMENT), component.comment);
    object.insert(QLatin1String(KEY_AT), pointToJson(component.at));
    object.insert(QLatin1String(KEY_ROTATION), component.rotation);
    object.insert(QLatin1String(KEY_MIRRORED), component.mirrored);
    object.insert(QLatin1String(KEY_LOCKED), component.locked);
    object.insert(QLatin1String(KEY_REF_AT), pointToJson(component.refAt));
    object.insert(QLatin1String(KEY_VALUE_AT), pointToJson(component.valueAt));
    return object;
}

Component componentFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    Component component;
    component.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    component.part = reader.nonEmptyString(object, KEY_PART, where);
    component.footprint = reader.string(object, KEY_FOOTPRINT, where);
    component.designator = reader.nonEmptyString(object, KEY_DESIGNATOR, where);
    component.value = reader.string(object, KEY_VALUE, where);
    component.offBoard = reader.boolean(object, KEY_OFF_BOARD, where);
    // Optional: no rated power when absent.
    if (object.contains(QLatin1String(KEY_POWER_MW)))
        component.powerMw = reader.integer(object, KEY_POWER_MW, where, 0, MAX_POWER_MW);
    // Optional: no comment when absent.
    if (object.contains(QLatin1String(KEY_COMMENT)))
        component.comment = reader.string(object, KEY_COMMENT, where);
    component.at = reader.point(object, KEY_AT, where);
    component.rotation = reader.rightAngle(object, KEY_ROTATION, where);
    component.mirrored = reader.boolean(object, KEY_MIRRORED, where);
    component.refAt = reader.point(object, KEY_REF_AT, where);
    component.valueAt = reader.point(object, KEY_VALUE_AT, where);
    // Optional: files written before locking have it off.
    if (object.contains(QLatin1String(KEY_LOCKED)))
        component.locked = reader.boolean(object, KEY_LOCKED, where);
    return component;
}

QJsonArray chainToJson(const QList<Point> &chain)
{
    QJsonArray points;
    for (const Point &point : chain)
        points.append(pointToJson(point));
    return points;
}

// The points of a wire or of a bus: at least two, every segment horizontal,
// vertical or at 45 degrees.
QList<Point> chainFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    const QList<Point> chain = reader.pointList(object, KEY_POINTS, where);
    if (!reader.ok())
        return chain;

    const QString pointsWhere = JsonReader::pathOf(where, KEY_POINTS);
    if (chain.size() < MIN_CHAIN_POINTS) {
        reader.fail(pointsWhere, JsonReader::tr("at least two points are expected"));
        return chain;
    }
    for (int i = 1; i < chain.size(); ++i) {
        const Point step = chain.at(i) - chain.at(i - 1);
        const QString stepWhere = JsonReader::pathOf(pointsWhere, i);
        if (step.x == 0 && step.y == 0) {
            reader.fail(stepWhere, JsonReader::tr("neighbouring points coincide"));
        } else if (!isStraightOrDiagonal(step)) {
            reader.fail(stepWhere, JsonReader::tr("a segment is not at 0 45 or 90 degrees"));
        }
    }
    return chain;
}

QJsonObject wireToJson(const Wire &wire)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(wire.id));
    object.insert(QLatin1String(KEY_POINTS), chainToJson(wire.points));
    return object;
}

Wire wireFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    Wire wire;
    wire.id = context.reader.uniqueId(object, KEY_ID, where, context.ids);
    wire.points = chainFromJson(object, where, context.reader);
    return wire;
}

QJsonObject busToJson(const Bus &bus)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(bus.id));
    object.insert(QLatin1String(KEY_POINTS), chainToJson(bus.points));
    object.insert(QLatin1String(KEY_NAME), bus.name);
    object.insert(QLatin1String(KEY_NAMES_AT), chainToJson(bus.namesAt));
    return object;
}

Bus busFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    Bus bus;
    bus.id = context.reader.uniqueId(object, KEY_ID, where, context.ids);
    bus.points = chainFromJson(object, where, context.reader);
    bus.name = context.reader.nonEmptyString(object, KEY_NAME, where);
    // Buses written before their name could be shown more than once have
    // one place for it, and those written before it could be moved have it
    // above their start.
    if (object.contains(QLatin1String(KEY_NAMES_AT)))
        bus.namesAt = context.reader.pointList(object, KEY_NAMES_AT, where);
    else if (object.contains(QLatin1String(KEY_NAME_AT)))
        bus.namesAt = {context.reader.point(object, KEY_NAME_AT, where)};
    return bus;
}

QJsonObject labelToJson(const Label &label)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(label.id));
    object.insert(QLatin1String(KEY_AT), pointToJson(label.at));
    object.insert(QLatin1String(KEY_TEXT), label.text);
    object.insert(QLatin1String(KEY_TEXT_AT), pointToJson(label.textAt));
    object.insert(QLatin1String(KEY_SCOPE), scopeNames().at(int(label.scope)));
    object.insert(QLatin1String(KEY_ROTATION), label.rotation);
    object.insert(QLatin1String(KEY_NET_CLASS), label.netClass);
    return object;
}

Label labelFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    Label label;
    label.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    label.at = reader.point(object, KEY_AT, where);
    label.text = reader.nonEmptyString(object, KEY_TEXT, where);
    // Labels written before their text could be moved have it at the point.
    if (object.contains(QLatin1String(KEY_TEXT_AT)))
        label.textAt = reader.point(object, KEY_TEXT_AT, where);
    label.scope = LabelScope(reader.choice(object, KEY_SCOPE, where, scopeNames()));
    label.rotation = reader.rightAngle(object, KEY_ROTATION, where);
    label.netClass = reader.string(object, KEY_NET_CLASS, where);
    return label;
}

QJsonObject powerSymbolToJson(const PowerSymbol &power)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(power.id));
    object.insert(QLatin1String(KEY_AT), pointToJson(power.at));
    object.insert(QLatin1String(KEY_NAME), power.name);
    object.insert(QLatin1String(KEY_ROTATION), power.rotation);
    object.insert(QLatin1String(KEY_SYMBOL), power.symbol);
    object.insert(QLatin1String(KEY_LOCKED), power.locked);
    return object;
}

PowerSymbol powerSymbolFromJson(const QJsonObject &object, const QString &where,
                                ReadContext &context)
{
    JsonReader &reader = context.reader;
    PowerSymbol power;
    power.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    power.at = reader.point(object, KEY_AT, where);
    power.name = reader.nonEmptyString(object, KEY_NAME, where);
    power.rotation = reader.rightAngle(object, KEY_ROTATION, where);
    power.symbol = reader.nonEmptyString(object, KEY_SYMBOL, where);
    if (object.contains(QLatin1String(KEY_LOCKED)))
        power.locked = reader.boolean(object, KEY_LOCKED, where);
    return power;
}

QJsonObject noConnectToJson(const NoConnect &mark)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(mark.id));
    object.insert(QLatin1String(KEY_AT), pointToJson(mark.at));
    return object;
}

NoConnect noConnectFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    NoConnect mark;
    mark.id = context.reader.uniqueId(object, KEY_ID, where, context.ids);
    mark.at = context.reader.point(object, KEY_AT, where);
    return mark;
}

// A graphic's own fields with the id beside them, in one object.
QJsonObject sheetGraphicToJson(const SheetGraphic &item)
{
    QJsonObject object = graphicToJson(item.graphic);
    object.insert(QLatin1String(KEY_ID), idToJson(item.id));
    return object;
}

SheetGraphic sheetGraphicFromJson(const QJsonObject &object, const QString &where,
                                  ReadContext &context)
{
    SheetGraphic item;
    item.id = context.reader.uniqueId(object, KEY_ID, where, context.ids);
    item.graphic = graphicFromJson(object, where, context.reader);
    return item;
}

QJsonObject sheetToJson(const Sheet &sheet)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(sheet.id));
    object.insert(QLatin1String(KEY_TITLE), sheet.title);
    object.insert(QLatin1String(KEY_PAPER), paperNames().at(int(sheet.paper)));
    object.insert(QLatin1String(KEY_PORTRAIT), sheet.portrait);
    object.insert(QLatin1String(KEY_COMPONENTS), listToJson(sheet.components, componentToJson));
    object.insert(QLatin1String(KEY_WIRES), listToJson(sheet.wires, wireToJson));
    object.insert(QLatin1String(KEY_BUSES), listToJson(sheet.buses, busToJson));
    object.insert(QLatin1String(KEY_LABELS), listToJson(sheet.labels, labelToJson));
    object.insert(QLatin1String(KEY_POWER), listToJson(sheet.powerSymbols, powerSymbolToJson));
    object.insert(QLatin1String(KEY_NO_CONNECTS), listToJson(sheet.noConnects, noConnectToJson));
    object.insert(QLatin1String(KEY_GRAPHICS), listToJson(sheet.graphics, sheetGraphicToJson));
    return object;
}

Sheet sheetFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    Sheet sheet;
    sheet.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    sheet.title = reader.string(object, KEY_TITLE, where);
    sheet.paper = PaperSize(reader.choice(object, KEY_PAPER, where, paperNames()));
    // Optional: lying paper when absent, as in files written before it.
    if (object.contains(QLatin1String(KEY_PORTRAIT)))
        sheet.portrait = reader.boolean(object, KEY_PORTRAIT, where);
    sheet.components = listFromJson<Component>(object, KEY_COMPONENTS, where, context,
                                               componentFromJson);
    sheet.wires = listFromJson<Wire>(object, KEY_WIRES, where, context, wireFromJson);
    sheet.buses = listFromJson<Bus>(object, KEY_BUSES, where, context, busFromJson);
    sheet.labels = listFromJson<Label>(object, KEY_LABELS, where, context, labelFromJson);
    sheet.powerSymbols = listFromJson<PowerSymbol>(object, KEY_POWER, where, context,
                                                   powerSymbolFromJson);
    sheet.noConnects = listFromJson<NoConnect>(object, KEY_NO_CONNECTS, where, context,
                                               noConnectFromJson);
    sheet.graphics = listFromJson<SheetGraphic>(object, KEY_GRAPHICS, where, context,
                                                sheetGraphicFromJson);
    return sheet;
}

} // namespace

bool isSheetEmpty(const Sheet &sheet)
{
    return sheet.components.isEmpty() && sheet.wires.isEmpty() && sheet.buses.isEmpty()
           && sheet.labels.isEmpty() && sheet.powerSymbols.isEmpty()
           && sheet.noConnects.isEmpty() && sheet.graphics.isEmpty();
}

QString paperSizeName(PaperSize paper)
{
    return paperNames().at(int(paper));
}

bool operator==(const Component &a, const Component &b)
{
    return a.id == b.id && a.part == b.part && a.footprint == b.footprint
           && a.designator == b.designator && a.value == b.value && a.offBoard == b.offBoard && a.powerMw == b.powerMw && a.comment == b.comment && a.at == b.at && a.rotation == b.rotation
           && a.mirrored == b.mirrored && a.refAt == b.refAt && a.valueAt == b.valueAt
           && a.locked == b.locked;
}

bool operator==(const Wire &a, const Wire &b)
{
    return a.id == b.id && a.points == b.points;
}

bool operator==(const Bus &a, const Bus &b)
{
    return a.id == b.id && a.points == b.points && a.name == b.name && a.namesAt == b.namesAt;
}

bool operator==(const Label &a, const Label &b)
{
    return a.id == b.id && a.at == b.at && a.text == b.text && a.textAt == b.textAt
           && a.scope == b.scope
           && a.rotation == b.rotation && a.netClass == b.netClass;
}

bool operator==(const PowerSymbol &a, const PowerSymbol &b)
{
    return a.id == b.id && a.at == b.at && a.name == b.name && a.rotation == b.rotation
           && a.symbol == b.symbol && a.locked == b.locked;
}

bool operator==(const NoConnect &a, const NoConnect &b)
{
    return a.id == b.id && a.at == b.at;
}

bool operator==(const SheetGraphic &a, const SheetGraphic &b)
{
    return a.id == b.id && a.graphic == b.graphic;
}

bool operator==(const Sheet &a, const Sheet &b)
{
    return a.id == b.id && a.title == b.title && a.paper == b.paper && a.portrait == b.portrait
           && a.components == b.components && a.wires == b.wires && a.buses == b.buses
           && a.labels == b.labels
           && a.powerSymbols == b.powerSymbols && a.noConnects == b.noConnects
           && a.graphics == b.graphics;
}

bool operator==(const Schematic &a, const Schematic &b)
{
    return a.sheets == b.sheets;
}

QJsonObject schematicToJson(const Schematic &schematic)
{
    QJsonObject root;
    root.insert(QLatin1String(KEY_SHEETS), listToJson(schematic.sheets, sheetToJson));
    return root;
}

Schematic schematicFromJson(const QJsonObject &root, JsonReader &reader)
{
    Schematic schematic;
    ReadContext context{reader, {}};
    schematic.sheets = listFromJson<Sheet>(root, KEY_SHEETS, QString(), context, sheetFromJson);
    if (reader.ok() && schematic.sheets.isEmpty()) {
        reader.fail(QLatin1String(KEY_SHEETS), JsonReader::tr("a schematic needs at least one sheet"));
    }
    return schematic;
}

bool SchematicFile::read(const QString &path, Schematic &schematic, QString &error)
{
    QJsonObject root;
    if (!JsonFile::read(path, root, error))
        return false;

    JsonReader reader;
    reader.header(root, FILE_TYPE_SCHEMATIC);
    const Schematic result = schematicFromJson(root, reader);
    if (!reader.ok()) {
        error = withPlace(path, reader.error());
        return false;
    }
    schematic = result;
    return true;
}

bool SchematicFile::write(const QString &path, const Schematic &schematic, QString &error)
{
    QJsonObject root = schematicToJson(schematic);
    writeHeader(root, FILE_TYPE_SCHEMATIC);
    return JsonFile::write(path, root, error);
}

} // namespace ppcb
