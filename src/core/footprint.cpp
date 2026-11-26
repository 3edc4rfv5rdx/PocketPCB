#include "footprint.h"

#include <QRegularExpression>

#include <algorithm>

namespace ppcb {

namespace {

const char KEY_PADS[] = "pads";
const char KEY_GRAPHICS[] = "graphics";
const char KEY_REF_AT[] = "refAt";
const char KEY_VALUE_AT[] = "valueAt";

const char KEY_NUMBER[] = "number";
const char KEY_AT[] = "at";
const char KEY_SHAPE[] = "shape";
const char KEY_SIZE[] = "size";
const char KEY_HOLE[] = "hole";
const char KEY_LAYERS[] = "layers";
const char KEY_BODY[] = "body";
const char KEY_BODY_SHAPE[] = "shape";
const char KEY_BODY_HEIGHT[] = "height";
const char KEY_BODY_RAISED[] = "raised";
const char KEY_BODY_COLOR[] = "color";

// In the order of BodyShape.
const QStringList &bodyShapeNames()
{
    static const QStringList names = {
        QStringLiteral("box"), QStringLiteral("upright"), QStringLiteral("lying"),
    };
    return names;
}

bool isColorCode(const QString &text)
{
    static const QRegularExpression code(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    return code.match(text).hasMatch();
}

QJsonObject bodyToJson(const Body &body)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_BODY_SHAPE), bodyShapeNames().at(int(body.shape)));
    if (body.sizeX > 0 || body.sizeY > 0)
        object.insert(QLatin1String(KEY_SIZE), pointToJson(Point{body.sizeX, body.sizeY}));
    object.insert(QLatin1String(KEY_BODY_HEIGHT), mmToJson(body.height));
    if (body.raised > 0)
        object.insert(QLatin1String(KEY_BODY_RAISED), mmToJson(body.raised));
    object.insert(QLatin1String(KEY_BODY_COLOR), body.color);
    return object;
}

Body bodyFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    Body body;
    body.shape = BodyShape(reader.choice(object, KEY_BODY_SHAPE, where, bodyShapeNames()));
    // Optional: the drawing of the silkscreen when absent.
    if (object.contains(QLatin1String(KEY_SIZE))) {
        const Point size = reader.point(object, KEY_SIZE, where);
        if (reader.ok() && (size.x <= 0 || size.y <= 0))
            reader.fail(JsonReader::pathOf(where, KEY_SIZE),
                        JsonReader::tr("a size greater than zero is expected"));
        body.sizeX = size.x;
        body.sizeY = size.y;
    }
    body.height = reader.positiveMm(object, KEY_BODY_HEIGHT, where);
    if (object.contains(QLatin1String(KEY_BODY_RAISED)))
        body.raised = reader.nonNegativeMm(object, KEY_BODY_RAISED, where);
    body.color = reader.string(object, KEY_BODY_COLOR, where);
    if (reader.ok() && !isColorCode(body.color))
        reader.fail(JsonReader::pathOf(where, KEY_BODY_COLOR),
                    JsonReader::tr("a colour as #rrggbb is expected"));
    return body;
}

// In the order of PadShape.
const QStringList &shapeNames()
{
    static const QStringList names = {
        QStringLiteral("circle"), QStringLiteral("rect"), QStringLiteral("oval"),
    };
    return names;
}

// In the order of PadLayers.
const QStringList &layersNames()
{
    static const QStringList names = {
        QStringLiteral("both"), QStringLiteral("front"), QStringLiteral("back"),
    };
    return names;
}

QJsonObject padToJson(const Pad &pad)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_NUMBER), pad.number);
    object.insert(QLatin1String(KEY_AT), pointToJson(pad.at));
    object.insert(QLatin1String(KEY_SHAPE), shapeNames().at(int(pad.shape)));
    object.insert(QLatin1String(KEY_SIZE), pointToJson(Point{pad.sizeX, pad.sizeY}));
    object.insert(QLatin1String(KEY_HOLE), mmToJson(pad.hole));
    object.insert(QLatin1String(KEY_LAYERS), layersNames().at(int(pad.layers)));
    return object;
}

Pad padFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    Pad pad;
    // Optional: connected to no pin when absent.
    if (object.contains(QLatin1String(KEY_NUMBER)))
        pad.number = reader.string(object, KEY_NUMBER, where);
    pad.at = reader.point(object, KEY_AT, where);
    pad.shape = PadShape(reader.choice(object, KEY_SHAPE, where, shapeNames()));

    const Point size = reader.point(object, KEY_SIZE, where);
    pad.sizeX = size.x;
    pad.sizeY = size.y;
    const QString sizeWhere = JsonReader::pathOf(where, KEY_SIZE);
    if (reader.ok() && (pad.sizeX <= 0 || pad.sizeY <= 0))
        reader.fail(sizeWhere, JsonReader::tr("a size greater than zero is expected"));
    if (reader.ok() && pad.shape == PadShape::Circle && pad.sizeX != pad.sizeY)
        reader.fail(sizeWhere, JsonReader::tr("a round pad must have equal width and height"));

    // Optional: no hole when absent.
    if (object.contains(QLatin1String(KEY_HOLE)))
        pad.hole = reader.nonNegativeMm(object, KEY_HOLE, where);
    if (reader.ok() && pad.hole >= std::min(pad.sizeX, pad.sizeY)) {
        reader.fail(JsonReader::pathOf(where, KEY_HOLE),
                    JsonReader::tr("the hole must be smaller than the pad"));
    }

    // Optional: a pad with a hole goes through the board, one without a
    // hole sits on the part's side.
    if (object.contains(QLatin1String(KEY_LAYERS)))
        pad.layers = PadLayers(reader.choice(object, KEY_LAYERS, where, layersNames()));
    else
        pad.layers = pad.hole > 0 ? PadLayers::Both : PadLayers::Front;
    if (reader.ok() && pad.hole == 0 && pad.layers == PadLayers::Both) {
        reader.fail(JsonReader::pathOf(where, KEY_LAYERS),
                    JsonReader::tr("a pad without a hole must be on one layer"));
    }
    return pad;
}

} // namespace

bool operator==(const Pad &a, const Pad &b)
{
    return a.number == b.number && a.at == b.at && a.shape == b.shape && a.sizeX == b.sizeX
           && a.sizeY == b.sizeY && a.hole == b.hole && a.layers == b.layers;
}

const char DEFAULT_BODY_COLOR[] = "#202020";

bool operator==(const Body &a, const Body &b)
{
    return a.shape == b.shape && a.sizeX == b.sizeX && a.sizeY == b.sizeY && a.height == b.height
           && a.raised == b.raised && a.color == b.color;
}

bool operator==(const Footprint &a, const Footprint &b)
{
    return a.pads == b.pads && a.graphics == b.graphics && a.refAt == b.refAt
           && a.valueAt == b.valueAt && a.body == b.body;
}

QJsonObject footprintToJson(const Footprint &footprint)
{
    QJsonArray pads;
    for (const Pad &pad : footprint.pads)
        pads.append(padToJson(pad));
    QJsonArray graphics;
    for (const Graphic &graphic : footprint.graphics)
        graphics.append(graphicToJson(graphic));

    QJsonObject object;
    object.insert(QLatin1String(KEY_PADS), pads);
    object.insert(QLatin1String(KEY_GRAPHICS), graphics);
    object.insert(QLatin1String(KEY_REF_AT), pointToJson(footprint.refAt));
    object.insert(QLatin1String(KEY_VALUE_AT), pointToJson(footprint.valueAt));
    if (footprint.body)
        object.insert(QLatin1String(KEY_BODY), bodyToJson(*footprint.body));
    return object;
}

Footprint footprintFromJson(const QJsonObject &object, const QString &where, JsonReader &reader)
{
    Footprint footprint;

    const QList<QJsonObject> pads = reader.objectList(object, KEY_PADS, where);
    const QString padsWhere = JsonReader::pathOf(where, KEY_PADS);
    for (int i = 0; i < pads.size(); ++i)
        footprint.pads.append(padFromJson(pads.at(i), JsonReader::pathOf(padsWhere, i), reader));

    const QList<QJsonObject> graphics = reader.objectList(object, KEY_GRAPHICS, where);
    const QString graphicsWhere = JsonReader::pathOf(where, KEY_GRAPHICS);
    for (int i = 0; i < graphics.size(); ++i) {
        footprint.graphics.append(
            graphicFromJson(graphics.at(i), JsonReader::pathOf(graphicsWhere, i), reader));
    }

    footprint.refAt = reader.point(object, KEY_REF_AT, where);
    footprint.valueAt = reader.point(object, KEY_VALUE_AT, where);
    // Optional: a thin box over the silkscreen when absent.
    if (object.contains(QLatin1String(KEY_BODY))) {
        footprint.body = bodyFromJson(reader.object(object, KEY_BODY, where),
                                      JsonReader::pathOf(where, KEY_BODY), reader);
    }
    return footprint;
}

} // namespace ppcb
