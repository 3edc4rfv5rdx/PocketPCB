#include "graphicedit.h"

#include "paint.h"
#include "sheetedit.h"

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

const int QUARTER_DEGREES = 90;
// The largest radius an arc through three points is given: beyond it the
// points are as good as on one line.
const Coord MAX_ARC_RADIUS = 1000 * NM_PER_MM;
const int FULL_TURN = 360;

Point turnedQuarter(const Point &point, const Point &center)
{
    const Point offset = point - center;
    return center + Point{-offset.y, offset.x};
}

Graphic graphicMoved(Graphic graphic, const Point &delta)
{
    graphic.a = graphic.a + delta;
    graphic.b = graphic.b + delta;
    for (Point &point : graphic.points)
        point = point + delta;
    return graphic;
}

Graphic graphicTurned(Graphic graphic, const Point &center)
{
    graphic.a = turnedQuarter(graphic.a, center);
    graphic.b = turnedQuarter(graphic.b, center);
    for (Point &point : graphic.points)
        point = turnedQuarter(point, center);
    if (graphic.kind == GraphicKind::Arc)
        graphic.startAngle = (graphic.startAngle + QUARTER_DEGREES) % FULL_TURN;
    return graphic;
}

// The angle from 'center' to 'point' in whole degrees, 0 to 359, growing
// towards +Y as the model's angles do.
int angleTo(const Point &center, const Point &point)
{
    const double degrees =
        std::atan2(double(point.y - center.y), double(point.x - center.x)) * 180.0 / M_PI;
    return ((int(std::lround(degrees)) % FULL_TURN) + FULL_TURN) % FULL_TURN;
}

Coord distance(const Point &a, const Point &b)
{
    return Coord(std::llround(std::hypot(double(b.x - a.x), double(b.y - a.y))));
}

} // namespace

int graphicAt(const QList<Graphic> &graphics, const QPointF &at, double reach)
{
    for (int i = int(graphics.size()) - 1; i >= 0; --i) {
        const Graphic &graphic = graphics.at(i);
        const QPolygonF line = graphicOutline(graphic);
        const double halfWidth = graphic.kind == GraphicKind::Text ? 0 : nmToMm(graphic.width) / 2;
        if (!line.isEmpty()) {
            if (isNearPolyline(line, at, reach + halfWidth))
                return i;
        } else if (graphicRect(graphic).adjusted(-reach, -reach, reach, reach).contains(at)) {
            return i;
        }
    }
    return -1;
}

QSet<int> graphicsInside(const QList<Graphic> &graphics, const QRectF &rect)
{
    QSet<int> inside;
    for (int i = 0; i < graphics.size(); ++i) {
        if (rect.contains(graphicRect(graphics.at(i))))
            inside.insert(i);
    }
    return inside;
}

QRectF graphicsBounds(const QList<Graphic> &graphics, const QSet<int> &chosen)
{
    QRectF bounds;
    bool any = false;
    for (int i = 0; i < graphics.size(); ++i) {
        if (!chosen.contains(i))
            continue;
        const QRectF rect = graphicRect(graphics.at(i));
        // By hand: united() passes over a rectangle without width, a line
        // across or up and down.
        bounds = any ? QRectF(QPointF(std::min(bounds.left(), rect.left()),
                                      std::min(bounds.top(), rect.top())),
                              QPointF(std::max(bounds.right(), rect.right()),
                                      std::max(bounds.bottom(), rect.bottom())))
                     : rect;
        any = true;
    }
    return bounds;
}

QList<Graphic> withGraphicsMoved(const QList<Graphic> &graphics, const QSet<int> &chosen,
                                 const Point &delta)
{
    QList<Graphic> result = graphics;
    for (int i = 0; i < result.size(); ++i) {
        if (chosen.contains(i))
            result[i] = graphicMoved(result.at(i), delta);
    }
    return result;
}

QList<Graphic> withGraphicsRotated(const QList<Graphic> &graphics, const QSet<int> &chosen,
                                   const Point &center)
{
    QList<Graphic> result = graphics;
    for (int i = 0; i < result.size(); ++i) {
        if (chosen.contains(i))
            result[i] = graphicTurned(result.at(i), center);
    }
    return result;
}

QList<Graphic> withoutGraphics(const QList<Graphic> &graphics, const QSet<int> &chosen)
{
    QList<Graphic> result;
    for (int i = 0; i < graphics.size(); ++i) {
        if (!chosen.contains(i))
            result.append(graphics.at(i));
    }
    return result;
}

QList<Point> graphicCorners(const Graphic &graphic)
{
    switch (graphic.kind) {
    case GraphicKind::Line:
        return {graphic.a, graphic.b};
    case GraphicKind::Rect:
    case GraphicKind::Oval:
        return {graphic.a, Point{graphic.b.x, graphic.a.y}, graphic.b,
                Point{graphic.a.x, graphic.b.y}};
    case GraphicKind::Polygon:
        return graphic.points;
    case GraphicKind::Circle:
    case GraphicKind::Arc:
    case GraphicKind::Text:
        break;
    }
    return {};
}

int cornerAt(const Graphic &graphic, const QPointF &at, double reach)
{
    const QList<Point> corners = graphicCorners(graphic);
    int nearest = -1;
    double best = reach;
    for (int i = 0; i < corners.size(); ++i) {
        const double length = QLineF(toMm(corners.at(i)), at).length();
        if (length <= best) {
            best = length;
            nearest = i;
        }
    }
    return nearest;
}

Graphic withCornerMoved(const Graphic &graphic, int corner, const Point &to)
{
    Graphic result = graphic;
    switch (graphic.kind) {
    case GraphicKind::Line:
        if (corner == 0)
            result.a = to;
        else if (corner == 1)
            result.b = to;
        break;
    case GraphicKind::Rect:
    case GraphicKind::Oval:
        // The corners go round: a, then across from a along X, b, then
        // across from a along Y.
        if (corner == 0) {
            result.a = to;
        } else if (corner == 1) {
            result.b.x = to.x;
            result.a.y = to.y;
        } else if (corner == 2) {
            result.b = to;
        } else if (corner == 3) {
            result.a.x = to.x;
            result.b.y = to.y;
        }
        break;
    case GraphicKind::Polygon:
        if (corner >= 0 && corner < result.points.size())
            result.points[corner] = to;
        break;
    case GraphicKind::Circle:
    case GraphicKind::Arc:
    case GraphicKind::Text:
        break;
    }
    return result;
}

Point keptStraightOrDiagonal(const Point &from, const Point &to)
{
    const Coord dx = to.x - from.x;
    const Coord dy = to.y - from.y;
    const Coord ax = std::llabs(dx);
    const Coord ay = std::llabs(dy);
    // Nearer across, nearer up and down, or nearer the diagonal: tan 22.5
    // degrees parts them, taken as 0.4142 so that whole numbers do.
    if (ay * 10000 <= ax * 4142)
        return Point{to.x, from.y};
    if (ax * 10000 <= ay * 4142)
        return Point{from.x, to.y};
    const Coord length = (ax + ay) / 2;
    return Point{from.x + (dx < 0 ? -length : length), from.y + (dy < 0 ? -length : length)};
}

Graphic lineBetween(const Point &from, const Point &to)
{
    Graphic graphic;
    graphic.kind = GraphicKind::Line;
    graphic.a = from;
    graphic.b = to;
    return graphic;
}

Graphic rectBetween(const Point &a, const Point &b)
{
    Graphic graphic;
    graphic.kind = GraphicKind::Rect;
    graphic.a = a;
    graphic.b = b;
    return graphic;
}

Graphic ovalBetween(const Point &a, const Point &b)
{
    Graphic graphic = rectBetween(a, b);
    graphic.kind = GraphicKind::Oval;
    return graphic;
}

Graphic circleThrough(const Point &center, const Point &through)
{
    Graphic graphic;
    graphic.kind = GraphicKind::Circle;
    graphic.a = center;
    graphic.radius = distance(center, through);
    return graphic;
}

std::optional<Graphic> arcThrough(const Point &start, const Point &middle, const Point &end)
{
    // The centre is where the middle lines of start-middle and middle-end
    // meet.
    const double ax = double(start.x), ay = double(start.y);
    const double bx = double(middle.x), by = double(middle.y);
    const double cx = double(end.x), cy = double(end.y);
    const double d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    // On one line: no circle; a radius beyond a metre is taken as one too.
    if (std::abs(d) < 1.0)
        return std::nullopt;
    const double a2 = ax * ax + ay * ay;
    const double b2 = bx * bx + by * by;
    const double c2 = cx * cx + cy * cy;
    const Point center{Coord(std::llround((a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d)),
                       Coord(std::llround((a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d))};
    const Coord radius = distance(center, start);
    if (radius <= 0 || radius > MAX_ARC_RADIUS)
        return std::nullopt;
    const int from = angleTo(center, start);
    const int through = angleTo(center, middle);
    const int to = angleTo(center, end);
    // Angles grow clockwise on screen: clockwise from the start, does the
    // middle come before the end? Else the arc goes the other way, and is
    // kept as the clockwise one from the end.
    const auto clockwise = [](int a, int b) { return (b - a + FULL_TURN) % FULL_TURN; };
    const bool forward = clockwise(from, through) < clockwise(from, to);
    Graphic graphic;
    graphic.kind = GraphicKind::Arc;
    graphic.a = center;
    graphic.radius = radius;
    graphic.startAngle = forward ? from : to;
    graphic.spanAngle = forward ? clockwise(from, to) : clockwise(to, from);
    if (graphic.spanAngle == 0)
        return std::nullopt;
    return graphic;
}

Graphic polygonOf(const QList<Point> &points)
{
    Graphic graphic;
    graphic.kind = GraphicKind::Polygon;
    graphic.points = points;
    return graphic;
}

Graphic textAt(const Point &at, const QString &text)
{
    Graphic graphic;
    graphic.kind = GraphicKind::Text;
    graphic.a = at;
    graphic.text = text;
    graphic.height = DEFAULT_TEXT_HEIGHT;
    graphic.align = TextAlign::Center;
    return graphic;
}

} // namespace ppcb
