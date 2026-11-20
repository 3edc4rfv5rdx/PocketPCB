#include "part.h"

#include <QCoreApplication>
#include <QLocale>

#include <iterator>

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
const char KEY_GROUP[] = "group";

const char GROUP_CONTEXT[] = "ppcb::Part";
// The part groups the program names, by key, in the order of the library
// panel: the common ones first, the rare ones (special semiconductors, RF
// and microwave, vacuum tubes) last.
const struct
{
    const char *key;
    const char *name;
} PART_GROUPS[] = {
    {"resistors", QT_TRANSLATE_NOOP("ppcb::Part", "Resistors")},
    {"capacitors", QT_TRANSLATE_NOOP("ppcb::Part", "Capacitors")},
    {"inductors", QT_TRANSLATE_NOOP("ppcb::Part", "Inductors")},
    {"transformers", QT_TRANSLATE_NOOP("ppcb::Part", "Transformers")},
    {"diodes", QT_TRANSLATE_NOOP("ppcb::Part", "Diodes")},
    {"leds", QT_TRANSLATE_NOOP("ppcb::Part", "LEDs")},
    {"optoelectronics", QT_TRANSLATE_NOOP("ppcb::Part", "Optoelectronics")},
    {"transistors", QT_TRANSLATE_NOOP("ppcb::Part", "Transistors")},
    {"thyristors", QT_TRANSLATE_NOOP("ppcb::Part", "Thyristors")},
    {"chips", QT_TRANSLATE_NOOP("ppcb::Part", "Chips")},
    {"crystals", QT_TRANSLATE_NOOP("ppcb::Part", "Crystals")},
    {"relays", QT_TRANSLATE_NOOP("ppcb::Part", "Relays")},
    {"switches", QT_TRANSLATE_NOOP("ppcb::Part", "Switches and buttons")},
    {"connectors", QT_TRANSLATE_NOOP("ppcb::Part", "Connectors")},
    {"fuses", QT_TRANSLATE_NOOP("ppcb::Part", "Fuses and protection")},
    {"indicators", QT_TRANSLATE_NOOP("ppcb::Part", "Lamps and indicators")},
    {"sound", QT_TRANSLATE_NOOP("ppcb::Part", "Sound devices")},
    {"sensors", QT_TRANSLATE_NOOP("ppcb::Part", "Sensors")},
    {"electromechanics", QT_TRANSLATE_NOOP("ppcb::Part", "Motors and electromechanics")},
    {"batteries", QT_TRANSLATE_NOOP("ppcb::Part", "Batteries")},
    {"special", QT_TRANSLATE_NOOP("ppcb::Part", "Special semiconductors")},
    {"rf", QT_TRANSLATE_NOOP("ppcb::Part", "RF and microwave")},
    {"tubes", QT_TRANSLATE_NOOP("ppcb::Part", "Vacuum tubes")},
};
const char OTHER_GROUP_NAME[] = QT_TRANSLATE_NOOP("ppcb::Part", "Other");

} // namespace

bool operator==(const Part &a, const Part &b)
{
    return a.symbol == b.symbol && a.footprints == b.footprints && a.prefix == b.prefix
           && a.prefixGost == b.prefixGost && a.value == b.value && a.connector == b.connector && a.noValue == b.noValue
           && a.powerMw == b.powerMw && a.group == b.group && a.name == b.name;
}

QString partGroupName(const QString &key)
{
    if (key.isEmpty())
        return QCoreApplication::translate(GROUP_CONTEXT, OTHER_GROUP_NAME);
    for (const auto &group : PART_GROUPS) {
        if (key == QLatin1String(group.key))
            return QCoreApplication::translate(GROUP_CONTEXT, group.name);
    }
    return key;
}

int partGroupRank(const QString &key)
{
    const int known = int(std::size(PART_GROUPS));
    if (key.isEmpty())
        return known + 1;
    for (int i = 0; i < known; ++i) {
        if (key == QLatin1String(PART_GROUPS[i].key))
            return i;
    }
    return known;
}

QList<int> standardPowersMw()
{
    return {50, 125, 250, 500, 1000, 2000, 5000, 10000, 20000, 25000, 50000, 100000};
}

QString formatPower(int powerMw)
{
    if (powerMw <= 0)
        return QString();
    const int milliwattsPerWatt = 1000;
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    const QString watts = locale.toString(double(powerMw) / milliwattsPerWatt, 'g', 6);
    return QCoreApplication::translate("ppcb::Format", "%1 W").arg(watts);
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
    if (!part.group.isEmpty())
        object.insert(QLatin1String(KEY_GROUP), part.group);
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
    // Optional: no value, not a connector, a value asked for, no group and
    // no name when absent.
    if (object.contains(QLatin1String(KEY_VALUE)))
        part.value = reader.string(object, KEY_VALUE, where);
    if (object.contains(QLatin1String(KEY_CONNECTOR)))
        part.connector = reader.boolean(object, KEY_CONNECTOR, where);
    if (object.contains(QLatin1String(KEY_NO_VALUE)))
        part.noValue = reader.boolean(object, KEY_NO_VALUE, where);
    if (object.contains(QLatin1String(KEY_POWER_MW)))
        part.powerMw = reader.integer(object, KEY_POWER_MW, where, 0, MAX_POWER_MW);
    if (object.contains(QLatin1String(KEY_GROUP)))
        part.group = reader.string(object, KEY_GROUP, where);
    if (object.contains(QLatin1String(KEY_NAME)))
        part.name = reader.nameMap(object, KEY_NAME, where);
    return part;
}

} // namespace ppcb
