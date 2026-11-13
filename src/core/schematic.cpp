#include "schematic.h"

#include <cstdlib>

namespace ppcb {

namespace {

const char KEY_SYMBOLS[] = "symbols";
const char KEY_SHEETS[] = "sheets";

const char KEY_ID[] = "id";
const char KEY_AT[] = "at";
const char KEY_ROTATION[] = "rotation";

const char KEY_TITLE[] = "title";
const char KEY_PAPER[] = "paper";
const char KEY_COMPONENTS[] = "components";
const char KEY_WIRES[] = "wires";
const char KEY_LABELS[] = "labels";
const char KEY_POWER[] = "power";
const char KEY_NO_CONNECTS[] = "noConnects";
const char KEY_GRAPHICS[] = "graphics";

const char KEY_PART[] = "part";
const char KEY_SYMBOL[] = "symbol";
const char KEY_FOOTPRINT[] = "footprint";
const char KEY_DESIGNATOR[] = "designator";
const char KEY_VALUE[] = "value";
const char KEY_PREFIX[] = "prefix";
const char KEY_PREFIX_GOST[] = "prefixGost";
const char KEY_CONNECTOR[] = "connector";
const char KEY_OFF_BOARD[] = "offBoard";
const char KEY_MIRRORED[] = "mirrored";
const char KEY_REF_AT[] = "refAt";
const char KEY_VALUE_AT[] = "valueAt";

const char KEY_POINTS[] = "points";
const char KEY_TEXT[] = "text";
const char KEY_SCOPE[] = "scope";
const char KEY_NET_CLASS[] = "netClass";
const char KEY_NAME[] = "name";

const int MIN_WIRE_POINTS = 2;

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
    // Names in the table of symbols.
    QStringList symbolNames;
};

QJsonObject componentToJson(const Component &component)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(component.id));
    object.insert(QLatin1String(KEY_PART), component.part);
    object.insert(QLatin1String(KEY_SYMBOL), component.symbol);
    object.insert(QLatin1String(KEY_FOOTPRINT), component.footprint);
    object.insert(QLatin1String(KEY_DESIGNATOR), component.designator);
    object.insert(QLatin1String(KEY_VALUE), component.value);
    object.insert(QLatin1String(KEY_PREFIX), component.prefix);
    object.insert(QLatin1String(KEY_PREFIX_GOST), component.prefixGost);
    object.insert(QLatin1String(KEY_CONNECTOR), component.connector);
    object.insert(QLatin1String(KEY_OFF_BOARD), component.offBoard);
    object.insert(QLatin1String(KEY_AT), pointToJson(component.at));
    object.insert(QLatin1String(KEY_ROTATION), component.rotation);
    object.insert(QLatin1String(KEY_MIRRORED), component.mirrored);
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
    component.symbol = reader.nonEmptyString(object, KEY_SYMBOL, where);
    if (reader.ok() && !context.symbolNames.contains(component.symbol)) {
        reader.fail(JsonReader::pathOf(where, KEY_SYMBOL),
                    JsonReader::tr("the symbol is not in the list of symbols"));
    }
    component.footprint = reader.string(object, KEY_FOOTPRINT, where);
    component.designator = reader.nonEmptyString(object, KEY_DESIGNATOR, where);
    component.value = reader.string(object, KEY_VALUE, where);
    component.prefix = reader.nonEmptyString(object, KEY_PREFIX, where);
    component.prefixGost = reader.nonEmptyString(object, KEY_PREFIX_GOST, where);
    component.connector = reader.boolean(object, KEY_CONNECTOR, where);
    component.offBoard = reader.boolean(object, KEY_OFF_BOARD, where);
    component.at = reader.point(object, KEY_AT, where);
    component.rotation = reader.rightAngle(object, KEY_ROTATION, where);
    component.mirrored = reader.boolean(object, KEY_MIRRORED, where);
    component.refAt = reader.point(object, KEY_REF_AT, where);
    component.valueAt = reader.point(object, KEY_VALUE_AT, where);
    return component;
}

QJsonObject wireToJson(const Wire &wire)
{
    QJsonArray points;
    for (const Point &point : wire.points)
        points.append(pointToJson(point));
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(wire.id));
    object.insert(QLatin1String(KEY_POINTS), points);
    return object;
}

Wire wireFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    Wire wire;
    wire.id = reader.uniqueId(object, KEY_ID, where, context.ids);
    wire.points = reader.pointList(object, KEY_POINTS, where);
    if (!reader.ok())
        return wire;

    const QString pointsWhere = JsonReader::pathOf(where, KEY_POINTS);
    if (wire.points.size() < MIN_WIRE_POINTS) {
        reader.fail(pointsWhere, JsonReader::tr("a wire needs at least two points"));
        return wire;
    }
    for (int i = 1; i < wire.points.size(); ++i) {
        const Point step = wire.points.at(i) - wire.points.at(i - 1);
        const QString stepWhere = JsonReader::pathOf(pointsWhere, i);
        if (step.x == 0 && step.y == 0) {
            reader.fail(stepWhere, JsonReader::tr("neighbouring points of a wire coincide"));
        } else if (step.x != 0 && step.y != 0 && std::llabs(step.x) != std::llabs(step.y)) {
            reader.fail(stepWhere, JsonReader::tr("a wire segment is not at 0 45 or 90 degrees"));
        }
    }
    return wire;
}

QJsonObject labelToJson(const Label &label)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(label.id));
    object.insert(QLatin1String(KEY_AT), pointToJson(label.at));
    object.insert(QLatin1String(KEY_TEXT), label.text);
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

// Writes a list with the given function for one element.
template <typename Item, typename ToJson>
QJsonArray listToJson(const QList<Item> &items, ToJson toJson)
{
    QJsonArray array;
    for (const Item &item : items)
        array.append(toJson(item));
    return array;
}

// Reads the list 'key' of 'object' with the given function for one element.
template <typename Item, typename FromJson>
QList<Item> listFromJson(const QJsonObject &object, const char *key, const QString &where,
                         ReadContext &context, FromJson fromJson)
{
    const QList<QJsonObject> objects = context.reader.objectList(object, key, where);
    const QString listWhere = JsonReader::pathOf(where, key);
    QList<Item> items;
    for (int i = 0; i < objects.size(); ++i)
        items.append(fromJson(objects.at(i), JsonReader::pathOf(listWhere, i), context));
    return items;
}

QJsonObject sheetToJson(const Sheet &sheet)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_ID), idToJson(sheet.id));
    object.insert(QLatin1String(KEY_TITLE), sheet.title);
    object.insert(QLatin1String(KEY_PAPER), paperNames().at(int(sheet.paper)));
    object.insert(QLatin1String(KEY_COMPONENTS), listToJson(sheet.components, componentToJson));
    object.insert(QLatin1String(KEY_WIRES), listToJson(sheet.wires, wireToJson));
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
    sheet.components = listFromJson<Component>(object, KEY_COMPONENTS, where, context,
                                               componentFromJson);
    sheet.wires = listFromJson<Wire>(object, KEY_WIRES, where, context, wireFromJson);
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

bool operator==(const Component &a, const Component &b)
{
    return a.id == b.id && a.part == b.part && a.symbol == b.symbol && a.footprint == b.footprint
           && a.designator == b.designator && a.value == b.value && a.prefix == b.prefix
           && a.prefixGost == b.prefixGost && a.connector == b.connector
           && a.offBoard == b.offBoard && a.at == b.at && a.rotation == b.rotation
           && a.mirrored == b.mirrored && a.refAt == b.refAt && a.valueAt == b.valueAt;
}

bool operator==(const Wire &a, const Wire &b)
{
    return a.id == b.id && a.points == b.points;
}

bool operator==(const Label &a, const Label &b)
{
    return a.id == b.id && a.at == b.at && a.text == b.text && a.scope == b.scope
           && a.rotation == b.rotation && a.netClass == b.netClass;
}

bool operator==(const PowerSymbol &a, const PowerSymbol &b)
{
    return a.id == b.id && a.at == b.at && a.name == b.name && a.rotation == b.rotation;
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
    return a.id == b.id && a.title == b.title && a.paper == b.paper
           && a.components == b.components && a.wires == b.wires && a.labels == b.labels
           && a.powerSymbols == b.powerSymbols && a.noConnects == b.noConnects
           && a.graphics == b.graphics;
}

bool operator==(const Schematic &a, const Schematic &b)
{
    return a.symbols == b.symbols && a.sheets == b.sheets;
}

QJsonObject schematicToJson(const Schematic &schematic)
{
    QJsonObject symbols;
    for (auto it = schematic.symbols.constBegin(); it != schematic.symbols.constEnd(); ++it)
        symbols.insert(it.key(), symbolToJson(it.value()));

    QJsonObject root;
    root.insert(QLatin1String(KEY_SYMBOLS), symbols);
    root.insert(QLatin1String(KEY_SHEETS), listToJson(schematic.sheets, sheetToJson));
    return root;
}

Schematic schematicFromJson(const QJsonObject &root, JsonReader &reader)
{
    Schematic schematic;
    ReadContext context{reader, {}, {}};

    // Symbols first: components are checked against their names.
    const QJsonObject symbols = reader.object(root, KEY_SYMBOLS, QString());
    for (auto it = symbols.constBegin(); it != symbols.constEnd(); ++it) {
        const QString where = JsonReader::pathOf(QLatin1String(KEY_SYMBOLS),
                                                 it.key().toUtf8().constData());
        if (!it.value().isObject()) {
            reader.fail(where, JsonReader::tr("an object is expected"));
            break;
        }
        schematic.symbols.insert(it.key(), symbolFromJson(it.value().toObject(), where, reader));
    }
    context.symbolNames = schematic.symbols.keys();

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
