#include "part.h"

namespace ppcb {

namespace {

const char KEY_SYMBOL[] = "symbol";
const char KEY_FOOTPRINTS[] = "footprints";
const char KEY_PREFIX[] = "prefix";
const char KEY_PREFIX_GOST[] = "prefixGost";
const char KEY_VALUE[] = "value";
const char KEY_CONNECTOR[] = "connector";

} // namespace

bool operator==(const Part &a, const Part &b)
{
    return a.symbol == b.symbol && a.footprints == b.footprints && a.prefix == b.prefix
           && a.prefixGost == b.prefixGost && a.value == b.value && a.connector == b.connector;
}

QJsonObject partToJson(const Part &part)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_SYMBOL), part.symbol);
    object.insert(QLatin1String(KEY_FOOTPRINTS), QJsonArray::fromStringList(part.footprints));
    object.insert(QLatin1String(KEY_PREFIX), part.prefix);
    object.insert(QLatin1String(KEY_PREFIX_GOST), part.prefixGost);
    object.insert(QLatin1String(KEY_VALUE), part.value);
    object.insert(QLatin1String(KEY_CONNECTOR), part.connector);
    return object;
}

Part partFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    Part part;
    part.symbol = reader.nonEmptyString(object, KEY_SYMBOL, where);
    // Optional: a part without footprints when absent.
    if (object.contains(QLatin1String(KEY_FOOTPRINTS)))
        part.footprints = reader.nameList(object, KEY_FOOTPRINTS, where);
    part.prefix = reader.nonEmptyString(object, KEY_PREFIX, where);
    part.prefixGost = reader.nonEmptyString(object, KEY_PREFIX_GOST, where);
    // Optional: no value and not a connector when absent.
    if (object.contains(QLatin1String(KEY_VALUE)))
        part.value = reader.string(object, KEY_VALUE, where);
    if (object.contains(QLatin1String(KEY_CONNECTOR)))
        part.connector = reader.boolean(object, KEY_CONNECTOR, where);
    return part;
}

} // namespace ppcb
