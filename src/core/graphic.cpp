#include "graphic.h"

namespace ppcb {

namespace {

const char KEY_KIND[] = "kind";
const char KEY_FROM[] = "from";
const char KEY_TO[] = "to";
const char KEY_CENTER[] = "center";
const char KEY_RADIUS[] = "radius";
const char KEY_START[] = "start";
const char KEY_SPAN[] = "span";
const char KEY_AT[] = "at";
const char KEY_TEXT[] = "text";
const char KEY_HEIGHT[] = "height";
const char KEY_ALIGN[] = "align";
const char KEY_WIDTH[] = "width";
const char KEY_POINTS[] = "points";
const char KEY_FILLED[] = "filled";

const int FULL_TURN = 360;
const int MIN_POLYGON_POINTS = 3;

// In the order of GraphicKind.
const QStringList &kindNames()
{
    static const QStringList names = {
        QStringLiteral("line"), QStringLiteral("rect"), QStringLiteral("circle"),
        QStringLiteral("arc"), QStringLiteral("text"), QStringLiteral("polygon"),
    };
    return names;
}

// In the order of TextAlign.
const QStringList &alignNames()
{
    static const QStringList names = {
        QStringLiteral("left"), QStringLiteral("center"), QStringLiteral("right"),
    };
    return names;
}

} // namespace

bool operator==(const Graphic &a, const Graphic &b)
{
    return a.kind == b.kind && a.a == b.a && a.b == b.b && a.radius == b.radius
           && a.startAngle == b.startAngle && a.spanAngle == b.spanAngle && a.text == b.text
           && a.height == b.height && a.align == b.align && a.points == b.points
           && a.filled == b.filled && a.width == b.width;
}

QJsonObject graphicToJson(const Graphic &graphic)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_KIND), kindNames().at(int(graphic.kind)));

    switch (graphic.kind) {
    case GraphicKind::Line:
    case GraphicKind::Rect:
        object.insert(QLatin1String(KEY_FROM), pointToJson(graphic.a));
        object.insert(QLatin1String(KEY_TO), pointToJson(graphic.b));
        break;
    case GraphicKind::Circle:
        object.insert(QLatin1String(KEY_CENTER), pointToJson(graphic.a));
        object.insert(QLatin1String(KEY_RADIUS), mmToJson(graphic.radius));
        break;
    case GraphicKind::Arc:
        object.insert(QLatin1String(KEY_CENTER), pointToJson(graphic.a));
        object.insert(QLatin1String(KEY_RADIUS), mmToJson(graphic.radius));
        object.insert(QLatin1String(KEY_START), graphic.startAngle);
        object.insert(QLatin1String(KEY_SPAN), graphic.spanAngle);
        break;
    case GraphicKind::Text:
        object.insert(QLatin1String(KEY_AT), pointToJson(graphic.a));
        object.insert(QLatin1String(KEY_TEXT), graphic.text);
        object.insert(QLatin1String(KEY_HEIGHT), mmToJson(graphic.height));
        object.insert(QLatin1String(KEY_ALIGN), alignNames().at(int(graphic.align)));
        break;
    case GraphicKind::Polygon: {
        QJsonArray points;
        for (const Point &point : graphic.points)
            points.append(pointToJson(point));
        object.insert(QLatin1String(KEY_POINTS), points);
        object.insert(QLatin1String(KEY_FILLED), graphic.filled);
        break;
    }
    }

    if (graphic.kind != GraphicKind::Text)
        object.insert(QLatin1String(KEY_WIDTH), mmToJson(graphic.width));
    return object;
}

Graphic graphicFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    Graphic graphic;
    graphic.kind = GraphicKind(reader.choice(object, KEY_KIND, where, kindNames()));
    if (!reader.ok())
        return graphic;

    switch (graphic.kind) {
    case GraphicKind::Line:
    case GraphicKind::Rect:
        graphic.a = reader.point(object, KEY_FROM, where);
        graphic.b = reader.point(object, KEY_TO, where);
        break;
    case GraphicKind::Circle:
        graphic.a = reader.point(object, KEY_CENTER, where);
        graphic.radius = reader.positiveMm(object, KEY_RADIUS, where);
        break;
    case GraphicKind::Arc:
        graphic.a = reader.point(object, KEY_CENTER, where);
        graphic.radius = reader.positiveMm(object, KEY_RADIUS, where);
        graphic.startAngle = reader.integer(object, KEY_START, where, 0, FULL_TURN - 1);
        graphic.spanAngle = reader.integer(object, KEY_SPAN, where, 1, FULL_TURN);
        break;
    case GraphicKind::Text:
        graphic.a = reader.point(object, KEY_AT, where);
        graphic.text = reader.nonEmptyString(object, KEY_TEXT, where);
        graphic.height = reader.positiveMm(object, KEY_HEIGHT, where);
        // Optional: centred when absent.
        if (object.contains(QLatin1String(KEY_ALIGN)))
            graphic.align = TextAlign(reader.choice(object, KEY_ALIGN, where, alignNames()));
        break;
    case GraphicKind::Polygon:
        graphic.points = reader.pointList(object, KEY_POINTS, where);
        if (reader.ok() && graphic.points.size() < MIN_POLYGON_POINTS) {
            reader.fail(JsonReader::pathOf(where, KEY_POINTS),
                        JsonReader::tr("at least three points are expected"));
        }
        // Optional: outline only when absent.
        if (object.contains(QLatin1String(KEY_FILLED)))
            graphic.filled = reader.boolean(object, KEY_FILLED, where);
        break;
    }

    // Optional: the default width when absent. Text has no line width.
    if (graphic.kind != GraphicKind::Text && object.contains(QLatin1String(KEY_WIDTH)))
        graphic.width = reader.positiveMm(object, KEY_WIDTH, where);
    return graphic;
}

} // namespace ppcb
