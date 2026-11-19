#include "part.h"

namespace ppcb {

namespace {

const char KEY_SYMBOL[] = "symbol";
const char KEY_FOOTPRINTS[] = "footprints";
const char KEY_PREFIX[] = "prefix";
const char KEY_PREFIX_GOST[] = "prefixGost";
const char KEY_VALUE[] = "value";
const char KEY_CONNECTOR[] = "connector";
const char KEY_NO_VALUE[] = "noValue";
const char KEY_POWER_MW[] = "powerMw";
const char KEY_NAME[] = "name";

} // namespace

bool operator==(const Part &a, const Part &b)
{
    return a.symbol == b.symbol && a.footprints == b.footprints && a.prefix == b.prefix
           && a.prefixGost == b.prefixGost && a.value == b.value && a.connector == b.connector && a.noValue == b.noValue
           && a.powerMw == b.powerMw && a.name == b.name;
}

QList<int> standardPowersMw()
{
    return {50, 125, 250, 500, 1000, 2000, 5000, 10000, 20000, 25000, 50000, 100000};
}

QString romanNumeral(int number)
{
    if (number < 1 || number > 3999)
        return QString();
    static const QList<QPair<int, const char *>> digits = {
        {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"}, {50, "L"},
        {40, "XL"},  {10, "X"},   {9, "IX"},  {5, "V"},    {4, "IV"},  {1, "I"}};
    QString text;
    for (const auto &[value, letters] : digits) {
        while (number >= value) {
            text += QLatin1String(letters);
            number -= value;
        }
    }
    return text;
}

QString Part::displayName(const QStringList &languages, const QString &fallback) const
{
    return localizedName(name, languages, fallback);
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
    object.insert(QLatin1String(KEY_NO_VALUE), part.noValue);
    if (part.powerMw > 0)
        object.insert(QLatin1String(KEY_POWER_MW), part.powerMw);
    QJsonObject names;
    for (auto it = part.name.constBegin(); it != part.name.constEnd(); ++it)
        names.insert(it.key(), it.value());
    object.insert(QLatin1String(KEY_NAME), names);
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
    // Optional: no value, not a connector, a value asked for and no name
    // when absent.
    if (object.contains(QLatin1String(KEY_VALUE)))
        part.value = reader.string(object, KEY_VALUE, where);
    if (object.contains(QLatin1String(KEY_CONNECTOR)))
        part.connector = reader.boolean(object, KEY_CONNECTOR, where);
    if (object.contains(QLatin1String(KEY_NO_VALUE)))
        part.noValue = reader.boolean(object, KEY_NO_VALUE, where);
    if (object.contains(QLatin1String(KEY_POWER_MW)))
        part.powerMw = reader.integer(object, KEY_POWER_MW, where, 0, MAX_POWER_MW);
    if (object.contains(QLatin1String(KEY_NAME)))
        part.name = reader.nameMap(object, KEY_NAME, where);
    return part;
}

} // namespace ppcb
