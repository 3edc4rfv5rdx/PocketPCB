#include "sheetedit.h"

#include <QLineF>

#include <algorithm>
#include <functional>

namespace ppcb {

namespace {

const int QUARTER_TURN = 90;
const int HALF_TURN_DEGREES = 180;
const int FULL_TURN_DEGREES = 360;

// An angle in whole degrees brought into 0..359.
int normalAngle(int degrees)
{
    return ((degrees % FULL_TURN_DEGREES) + FULL_TURN_DEGREES) % FULL_TURN_DEGREES;
}

double distanceToSegment(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double lengthSquared = QPointF::dotProduct(ab, ab);
    if (lengthSquared == 0)
        return QLineF(point, a).length();
    const double t = std::clamp(QPointF::dotProduct(point - a, ab) / lengthSquared, 0.0, 1.0);
    return QLineF(point, a + t * ab).length();
}

bool isNearPolyline(const QPolygonF &polyline, const QPointF &point, double tolerance)
{
    for (int i = 1; i < polyline.size(); ++i) {
        if (distanceToSegment(point, polyline.at(i - 1), polyline.at(i)) <= tolerance)
            return true;
    }
    return polyline.size() == 1 && QLineF(point, polyline.first()).length() <= tolerance;
}

// What a turn or a mirror does to each kind of thing on a sheet.
struct Transform
{
    std::function<Point(const Point &)> point;
    // To an offset that moves with its object (a component's text places).
    std::function<Point(const Point &)> offset;
    // To a component: its rotation and its mirroring.
    std::function<void(Component &)> component;
    // To the rotation of a label and of a power symbol.
    std::function<int(int)> labelRotation;
    std::function<int(int)> powerRotation;
    // To a graphic, after its points: its arc angles and text alignment.
    std::function<void(Graphic &)> graphic;
};

Sheet transformed(const Sheet &sheet, const QSet<ObjectId> &ids, const Transform &transform)
{
    Sheet result = sheet;
    for (Component &component : result.components) {
        if (!ids.contains(component.id))
            continue;
        component.at = transform.point(component.at);
        component.refAt = transform.offset(component.refAt);
        component.valueAt = transform.offset(component.valueAt);
        transform.component(component);
    }
    for (Wire &wire : result.wires) {
        if (ids.contains(wire.id)) {
            for (Point &point : wire.points)
                point = transform.point(point);
        }
    }
    for (Bus &bus : result.buses) {
        if (ids.contains(bus.id)) {
            for (Point &point : bus.points)
                point = transform.point(point);
        }
    }
    for (Label &label : result.labels) {
        if (ids.contains(label.id)) {
            label.at = transform.point(label.at);
            label.rotation = transform.labelRotation(label.rotation);
        }
    }
    for (PowerSymbol &power : result.powerSymbols) {
        if (ids.contains(power.id)) {
            power.at = transform.point(power.at);
            power.rotation = transform.powerRotation(power.rotation);
        }
    }
    for (NoConnect &mark : result.noConnects) {
        if (ids.contains(mark.id))
            mark.at = transform.point(mark.at);
    }
    for (SheetGraphic &item : result.graphics) {
        if (!ids.contains(item.id))
            continue;
        Graphic &graphic = item.graphic;
        graphic.a = transform.point(graphic.a);
        graphic.b = transform.point(graphic.b);
        for (Point &point : graphic.points)
            point = transform.point(point);
        transform.graphic(graphic);
    }
    return result;
}

} // namespace

ObjectId objectAt(const QList<SheetObjectShape> &shapes, const QPointF &point, double tolerance)
{
    for (auto it = shapes.crbegin(); it != shapes.crend(); ++it) {
        const bool hit = it->polyline.isEmpty()
                             ? it->rect.adjusted(-tolerance, -tolerance, tolerance, tolerance)
                                   .contains(point)
                             : isNearPolyline(it->polyline, point, tolerance);
        if (hit)
            return it->id;
    }
    return ObjectId();
}

QSet<ObjectId> objectsInside(const QList<SheetObjectShape> &shapes, const QRectF &rect)
{
    QSet<ObjectId> ids;
    const QRectF area = rect.normalized();
    for (const SheetObjectShape &shape : shapes) {
        // contains() refuses a rectangle of no width, such as a straight wire.
        const QRectF r = shape.rect;
        if (area.left() <= r.left() && r.right() <= area.right() && area.top() <= r.top()
            && r.bottom() <= area.bottom()) {
            ids.insert(shape.id);
        }
    }
    return ids;
}

QRectF selectionBounds(const QList<SheetObjectShape> &shapes, const QSet<ObjectId> &ids)
{
    QRectF bounds;
    bool empty = true;
    for (const SheetObjectShape &shape : shapes) {
        if (!ids.contains(shape.id))
            continue;
        const QRectF r = shape.rect;
        if (empty) {
            bounds = r;
            empty = false;
        } else {
            bounds.setCoords(std::min(bounds.left(), r.left()), std::min(bounds.top(), r.top()),
                             std::max(bounds.right(), r.right()),
                             std::max(bounds.bottom(), r.bottom()));
        }
    }
    return bounds;
}

Sheet movedObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &delta)
{
    Transform transform;
    transform.point = [&delta](const Point &p) { return p + delta; };
    transform.offset = [](const Point &p) { return p; };
    transform.component = [](Component &) {};
    transform.labelRotation = [](int rotation) { return rotation; };
    transform.powerRotation = [](int rotation) { return rotation; };
    transform.graphic = [](Graphic &) {};
    return transformed(sheet, ids, transform);
}

Sheet rotatedObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &center)
{
    // A quarter turn clockwise on screen, where Y grows downwards: what was
    // to the right of the centre goes below it.
    const auto turnOffset = [](const Point &p) { return Point{-p.y, p.x}; };
    Transform transform;
    transform.point = [&center, turnOffset](const Point &p) {
        return center + turnOffset(p - center);
    };
    transform.offset = turnOffset;
    transform.component = [](Component &c) { c.rotation = normalAngle(c.rotation + QUARTER_TURN); };
    transform.labelRotation = [](int r) { return normalAngle(r + QUARTER_TURN); };
    transform.powerRotation = [](int r) { return normalAngle(r + QUARTER_TURN); };
    transform.graphic = [](Graphic &g) {
        if (g.kind == GraphicKind::Arc)
            g.startAngle = normalAngle(g.startAngle + QUARTER_TURN);
    };
    return transformed(sheet, ids, transform);
}

Sheet mirroredObjects(const Sheet &sheet, const QSet<ObjectId> &ids, const Point &center)
{
    const auto flipOffset = [](const Point &p) { return Point{-p.x, p.y}; };
    Transform transform;
    transform.point = [&center, flipOffset](const Point &p) {
        return center + flipOffset(p - center);
    };
    transform.offset = flipOffset;
    // A mirror before a turn equals the opposite turn before the mirror.
    transform.component = [](Component &c) {
        c.mirrored = !c.mirrored;
        c.rotation = normalAngle(-c.rotation);
    };
    // A label's text runs along its direction, which flips about the vertical.
    transform.labelRotation = [](int r) { return normalAngle(HALF_TURN_DEGREES - r); };
    // A power symbol's stem points up at 0: up stays up, right becomes left.
    transform.powerRotation = [](int r) { return normalAngle(-r); };
    transform.graphic = [](Graphic &g) {
        if (g.kind == GraphicKind::Arc)
            g.startAngle = normalAngle(HALF_TURN_DEGREES - g.startAngle - g.spanAngle);
        if (g.kind == GraphicKind::Text) {
            if (g.align == TextAlign::Left)
                g.align = TextAlign::Right;
            else if (g.align == TextAlign::Right)
                g.align = TextAlign::Left;
        }
    };
    return transformed(sheet, ids, transform);
}

Sheet withoutObjects(const Sheet &sheet, const QSet<ObjectId> &ids)
{
    Sheet result = sheet;
    const auto selected = [&ids](const auto &object) { return ids.contains(object.id); };
    result.components.removeIf(selected);
    result.wires.removeIf(selected);
    result.buses.removeIf(selected);
    result.labels.removeIf(selected);
    result.powerSymbols.removeIf(selected);
    result.noConnects.removeIf(selected);
    result.graphics.removeIf(selected);
    return result;
}

} // namespace ppcb
