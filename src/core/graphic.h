#pragma once

#include "jsonio.h"
#include "point.h"

#include <QList>
#include <QString>

namespace ppcb {

// An oval is the rectangle of 'a' and 'b' with its shorter sides made half
// circles: the outline of a body with round ends.
enum class GraphicKind { Line, Rect, Circle, Arc, Text, Polygon, Oval };
enum class TextAlign { Left, Center, Right };

// Line width used when a file does not give one.
constexpr Coord DEFAULT_LINE_WIDTH = 200000; // 0.2 mm

// One drawing primitive of a symbol, of a footprint's silkscreen or of the
// free drawing on a schematic sheet.
// Which fields are used depends on 'kind'; the others keep their defaults.
struct Graphic
{
    GraphicKind kind = GraphicKind::Line;

    // Line: one end. Rect: one corner. Circle, Arc: the centre.
    // Text: the anchor point (see 'align').
    Point a;
    // Line: the other end. Rect: the opposite corner.
    Point b;

    // Circle, Arc.
    Coord radius = 0;
    // Arc, whole degrees. 0 points to +X; angles grow towards +Y, which is
    // clockwise on screen because Y grows downwards.
    int startAngle = 0;
    int spanAngle = 0;

    // Text: the string, the letter height, and which point of the text sits
    // on 'a' horizontally (vertically it is always the middle).
    QString text;
    Coord height = 0;
    TextAlign align = TextAlign::Center;

    // Polygon: its corners, at least three; the last one is joined to the
    // first. 'filled' paints the inside as well as the outline.
    QList<Point> points;
    bool filled = false;

    // Line width. All kinds except Text.
    Coord width = DEFAULT_LINE_WIDTH;
    // The line is drawn dashed. All kinds except Text.
    bool dashed = false;
};

bool operator==(const Graphic &a, const Graphic &b);
inline bool operator!=(const Graphic &a, const Graphic &b) { return !(a == b); }

QJsonObject graphicToJson(const Graphic &graphic);
// 'where' is the path of the object for error messages. After a failure
// (see reader.ok()) the result is not meaningful.
Graphic graphicFromJson(const QJsonObject &object, const QString &where, JsonReader &reader);

} // namespace ppcb
