#include "outline.h"

#include "grid.h"
#include "paint.h"
#include "sheetedit.h"

#include <QLineF>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

// Exact for products of two coordinates, which int64 cannot hold. GCC and
// Clang have it, MinGW too; __extension__ keeps -Wpedantic quiet about it.
__extension__ typedef __int128 Wide;

Wide cross(const Point &a, const Point &b)
{
    return Wide(a.x) * b.y - Wide(a.y) * b.x;
}

Wide dot(const Point &a, const Point &b)
{
    return Wide(a.x) * b.x + Wide(a.y) * b.y;
}

QPointF toPointF(const Point &point)
{
    return QPointF(double(point.x), double(point.y));
}

Point rounded(const QPointF &point)
{
    return Point{std::llround(point.x()), std::llround(point.y())};
}

int orientation(const Point &a, const Point &b, const Point &c)
{
    const Wide value = cross(b - a, c - a);
    return value > 0 ? 1 : (value < 0 ? -1 : 0);
}

// 'c', in line with 'a' and 'b', lies between them.
bool between(const Point &a, const Point &b, const Point &c)
{
    return std::min(a.x, b.x) <= c.x && c.x <= std::max(a.x, b.x) && std::min(a.y, b.y) <= c.y
           && c.y <= std::max(a.y, b.y);
}

// The segments 'a'-'b' and 'c'-'d' cross or touch.
bool segmentsMeet(const Point &a, const Point &b, const Point &c, const Point &d)
{
    const int o1 = orientation(a, b, c);
    const int o2 = orientation(a, b, d);
    const int o3 = orientation(c, d, a);
    const int o4 = orientation(c, d, b);
    if (o1 * o2 < 0 && o3 * o4 < 0)
        return true;
    return (o1 == 0 && between(a, b, c)) || (o2 == 0 && between(a, b, d))
           || (o3 == 0 && between(c, d, a)) || (o4 == 0 && between(c, d, b));
}

// Twice the area, positive when the corners go clockwise on the screen.
double signedArea(const QList<Point> &points)
{
    double area = 0;
    for (int i = 0; i < points.size(); ++i) {
        const Point &a = points.at(i);
        const Point &b = points.at((i + 1) % points.size());
        area += double(a.x) * double(b.y) - double(b.x) * double(a.y);
    }
    return area;
}

QList<Point> cornerPoints(const QList<OutlineCorner> &outline)
{
    QList<Point> points;
    for (const OutlineCorner &corner : outline)
        points.append(corner.at);
    return points;
}

Point snapped(const Point &point, Coord grid)
{
    return Point{snapToGrid(point.x, grid), snapToGrid(point.y, grid)};
}

// Without repeated points and without points in the middle of a straight
// side.
QList<Point> cleaned(QList<Point> points)
{
    QList<Point> result;
    for (const Point &point : points) {
        if (result.isEmpty() || result.last() != point)
            result.append(point);
    }
    while (result.size() > 1 && result.first() == result.last())
        result.removeLast();
    bool changed = true;
    while (changed && result.size() >= 3) {
        changed = false;
        for (int i = 0; i < result.size() && result.size() >= 3; ++i) {
            const Point &previous = result.at((i + result.size() - 1) % result.size());
            const Point &next = result.at((i + 1) % result.size());
            const Point &at = result.at(i);
            const Point in = at - previous;
            const Point out = next - at;
            if (in == Point() || (cross(in, out) == 0 && dot(in, out) > 0)) {
                result.removeAt(i);
                changed = true;
                --i;
            }
        }
    }
    return result;
}

} // namespace

QList<OutlineCorner> rectangleOutline(const Point &topLeft, Coord width, Coord height)
{
    return {OutlineCorner{topLeft, 0}, OutlineCorner{topLeft + Point{width, 0}, 0},
            OutlineCorner{topLeft + Point{width, height}, 0},
            OutlineCorner{topLeft + Point{0, height}, 0}};
}

OutlineProblem outlineProblem(const QList<OutlineCorner> &outline)
{
    const int count = int(outline.size());
    if (count < 3)
        return OutlineProblem::TooFewCorners;
    const auto from = [&outline, count](int side) { return outline.at(side % count).at; };
    const auto step = [&from](int side) { return from(side + 1) - from(side); };
    for (int side = 0; side < count; ++side) {
        if (step(side) == Point())
            return OutlineProblem::ZeroSide;
    }
    for (int side = 0; side < count; ++side) {
        // Turning back along the side before.
        if (cross(step(side), step(side + 1)) == 0 && dot(step(side), step(side + 1)) < 0)
            return OutlineProblem::Crossing;
        // Sides next to each other share a corner and nothing else.
        for (int other = side + 2; other < count; ++other) {
            if (side == 0 && other == count - 1)
                continue;
            if (segmentsMeet(from(side), from(side + 1), from(other), from(other + 1)))
                return OutlineProblem::Crossing;
        }
    }
    return OutlineProblem::None;
}

std::optional<QList<OutlineCorner>> withOutlineCornerMoved(const QList<OutlineCorner> &outline,
                                                           int corner, const Point &to)
{
    if (corner < 0 || corner >= outline.size())
        return std::nullopt;
    QList<OutlineCorner> moved = outline;
    moved[corner].at = to;
    if (outlineProblem(moved) != OutlineProblem::None)
        return std::nullopt;
    return moved;
}

std::optional<QList<OutlineCorner>> withOutlineSideMoved(const QList<OutlineCorner> &outline,
                                                         int side, const Point &delta)
{
    const int count = int(outline.size());
    if (side < 0 || side >= count || count < 3)
        return std::nullopt;
    const int start = side;
    const int end = (side + 1) % count;
    const Point along = outline.at(end).at - outline.at(start).at;
    // A level side goes only up or down, an upright one only sideways.
    Point shift = delta;
    if (along.y == 0)
        shift.x = 0;
    else if (along.x == 0)
        shift.y = 0;
    if (shift == Point())
        return outline;

    QList<OutlineCorner> moved = outline;
    moved[start].at = outline.at(start).at + shift;
    moved[end].at = outline.at(end).at + shift;
    // A side next to it that would turn round folds the outline.
    const int before = (side + count - 1) % count;
    const int after = (end + 1) % count;
    const Point oldIn = outline.at(start).at - outline.at(before).at;
    const Point oldOut = outline.at(after).at - outline.at(end).at;
    const Point newIn = moved.at(start).at - moved.at(before).at;
    const Point newOut = moved.at(after).at - moved.at(end).at;
    if (dot(newIn, oldIn) <= 0 || dot(newOut, oldOut) <= 0)
        return std::nullopt;
    if (outlineProblem(moved) != OutlineProblem::None)
        return std::nullopt;
    return moved;
}

OutlineCut withOutlineCut(const QList<OutlineCorner> &outline, const Point &a, const Point &b,
                          Coord grid)
{
    OutlineCut result{outline, OutlineCutProblem::Misses};
    if (a.x == b.x || a.y == b.y || outline.size() < 3)
        return result;

    // In nanometres: the corners are whole numbers, exact in a double.
    QPolygonF boardPolygon;
    for (const OutlineCorner &corner : outline)
        boardPolygon.append(toPointF(corner.at));
    QPainterPath board;
    board.addPolygon(boardPolygon);
    board.closeSubpath();
    const QRectF rect = QRectF(toPointF(a), toPointF(b)).normalized();
    QPainterPath cut;
    cut.addRect(rect);

    const QList<QPolygonF> pieces = board.subtracted(cut).toSubpathPolygons();
    if (pieces.isEmpty()) {
        result.problem = OutlineCutProblem::Splits;
        return result;
    }
    if (pieces.size() > 1) {
        result.problem =
            board.contains(rect) ? OutlineCutProblem::Inside : OutlineCutProblem::Splits;
        return result;
    }
    // The corners the cut made go to the grid; those that stay are where
    // they were.
    const QList<Point> old = cornerPoints(outline);
    QList<Point> found;
    for (const QPointF &point : pieces.first()) {
        const Point at = rounded(point);
        found.append(old.contains(at) ? at : snapped(at, grid));
    }
    QList<Point> points = cleaned(found);
    if (points.size() < 3) {
        result.problem = OutlineCutProblem::Splits;
        return result;
    }
    // Nothing taken off: the same corners, wherever they start.
    if (std::fabs(signedArea(points)) >= std::fabs(signedArea(old))) {
        result.problem = OutlineCutProblem::Misses;
        return result;
    }
    // The corners go round the way they went, from the same first corner
    // when it stays.
    if ((signedArea(points) > 0) != (signedArea(old) > 0))
        std::reverse(points.begin(), points.end());
    const auto first = std::find(points.begin(), points.end(), old.first());
    if (first != points.end())
        std::rotate(points.begin(), first, points.end());

    QList<OutlineCorner> corners;
    for (const Point &point : std::as_const(points)) {
        OutlineCorner corner{point, 0};
        const int index = int(old.indexOf(point));
        if (index >= 0)
            corner.radius = outline.at(index).radius;
        corners.append(corner);
    }
    if (outlineProblem(corners) != OutlineProblem::None) {
        result.problem = OutlineCutProblem::Splits;
        return result;
    }
    return OutlineCut{corners, OutlineCutProblem::None};
}

int outlineCornerAt(const QList<OutlineCorner> &outline, const QPointF &at, double reach)
{
    int found = -1;
    double nearest = reach;
    for (int i = 0; i < outline.size(); ++i) {
        const double distance = QLineF(toMm(outline.at(i).at), at).length();
        if (distance <= nearest) {
            nearest = distance;
            found = i;
        }
    }
    return found;
}

int outlineSideAt(const QList<OutlineCorner> &outline, const QPointF &at, double reach)
{
    const int count = int(outline.size());
    int found = -1;
    double nearest = reach;
    for (int i = 0; count >= 2 && i < count; ++i) {
        const double distance =
            distanceToSegment(at, toMm(outline.at(i).at), toMm(outline.at((i + 1) % count).at));
        if (distance <= nearest) {
            nearest = distance;
            found = i;
        }
    }
    return found;
}

} // namespace ppcb
