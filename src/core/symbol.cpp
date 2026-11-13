#include "symbol.h"

#include <QSet>

namespace ppcb {

namespace {

const char KEY_GRAPHICS[] = "graphics";
const char KEY_PINS[] = "pins";
const char KEY_REF_AT[] = "refAt";
const char KEY_VALUE_AT[] = "valueAt";
const char KEY_SHOW_PIN_NUMBERS[] = "showPinNumbers";
const char KEY_SHOW_PIN_NAMES[] = "showPinNames";

const char KEY_NUMBER[] = "number";
const char KEY_NAME[] = "name";
const char KEY_AT[] = "at";
const char KEY_SIDE[] = "side";
const char KEY_LENGTH[] = "length";

// In the order of PinSide.
const QStringList &sideNames()
{
    static const QStringList names = {
        QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("up"),
        QStringLiteral("down"),
    };
    return names;
}

QJsonObject pinToJson(const Pin &pin)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_NUMBER), pin.number);
    object.insert(QLatin1String(KEY_NAME), pin.name);
    object.insert(QLatin1String(KEY_AT), pointToJson(pin.at));
    object.insert(QLatin1String(KEY_SIDE), sideNames().at(int(pin.side)));
    object.insert(QLatin1String(KEY_LENGTH), mmToJson(pin.length));
    return object;
}

Pin pinFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    Pin pin;
    pin.number = reader.nonEmptyString(object, KEY_NUMBER, where);
    // Optional: no name when absent.
    if (object.contains(QLatin1String(KEY_NAME)))
        pin.name = reader.string(object, KEY_NAME, where);
    pin.at = reader.point(object, KEY_AT, where);
    pin.side = PinSide(reader.choice(object, KEY_SIDE, where, sideNames()));
    // Optional: the default length when absent.
    if (object.contains(QLatin1String(KEY_LENGTH)))
        pin.length = reader.nonNegativeMm(object, KEY_LENGTH, where);

    if (reader.ok() && (pin.at.x % PIN_GRID != 0 || pin.at.y % PIN_GRID != 0))
        reader.fail(JsonReader::pathOf(where, KEY_AT), JsonReader::tr("the pin is not on the pin grid"));
    return pin;
}

} // namespace

bool operator==(const Pin &a, const Pin &b)
{
    return a.number == b.number && a.name == b.name && a.at == b.at && a.side == b.side
           && a.length == b.length;
}

bool operator==(const Symbol &a, const Symbol &b)
{
    return a.graphics == b.graphics && a.pins == b.pins && a.refAt == b.refAt
           && a.valueAt == b.valueAt && a.showPinNumbers == b.showPinNumbers
           && a.showPinNames == b.showPinNames;
}

QJsonObject symbolToJson(const Symbol &symbol)
{
    QJsonArray graphics;
    for (const Graphic &graphic : symbol.graphics)
        graphics.append(graphicToJson(graphic));
    QJsonArray pins;
    for (const Pin &pin : symbol.pins)
        pins.append(pinToJson(pin));

    QJsonObject object;
    object.insert(QLatin1String(KEY_GRAPHICS), graphics);
    object.insert(QLatin1String(KEY_PINS), pins);
    object.insert(QLatin1String(KEY_REF_AT), pointToJson(symbol.refAt));
    object.insert(QLatin1String(KEY_VALUE_AT), pointToJson(symbol.valueAt));
    object.insert(QLatin1String(KEY_SHOW_PIN_NUMBERS), symbol.showPinNumbers);
    object.insert(QLatin1String(KEY_SHOW_PIN_NAMES), symbol.showPinNames);
    return object;
}

Symbol symbolFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    Symbol symbol;

    const QList<QJsonObject> graphics = reader.objectList(object, KEY_GRAPHICS, where);
    const QString graphicsWhere = JsonReader::pathOf(where, KEY_GRAPHICS);
    for (int i = 0; i < graphics.size(); ++i) {
        symbol.graphics.append(
            graphicFromJson(graphics.at(i), JsonReader::pathOf(graphicsWhere, i), reader));
    }

    const QList<QJsonObject> pins = reader.objectList(object, KEY_PINS, where);
    const QString pinsWhere = JsonReader::pathOf(where, KEY_PINS);
    QSet<QString> numbers;
    for (int i = 0; i < pins.size(); ++i) {
        const QString pinWhere = JsonReader::pathOf(pinsWhere, i);
        const Pin pin = pinFromJson(pins.at(i), pinWhere, reader);
        if (reader.ok() && numbers.contains(pin.number)) {
            reader.fail(JsonReader::pathOf(pinWhere, KEY_NUMBER),
                        JsonReader::tr("the pin number is used twice"));
        }
        numbers.insert(pin.number);
        symbol.pins.append(pin);
    }

    symbol.refAt = reader.point(object, KEY_REF_AT, where);
    symbol.valueAt = reader.point(object, KEY_VALUE_AT, where);
    // Optional: shown when absent.
    if (object.contains(QLatin1String(KEY_SHOW_PIN_NUMBERS)))
        symbol.showPinNumbers = reader.boolean(object, KEY_SHOW_PIN_NUMBERS, where);
    if (object.contains(QLatin1String(KEY_SHOW_PIN_NAMES)))
        symbol.showPinNames = reader.boolean(object, KEY_SHOW_PIN_NAMES, where);
    return symbol;
}

} // namespace ppcb
