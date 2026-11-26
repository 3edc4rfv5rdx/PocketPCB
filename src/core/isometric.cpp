#include "isometric.h"

#include "footprintedit.h"
#include "paint.h"

#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

// Pieces a round side is cut into.
const int ROUND_PIECES = 32;
// The board under a footprint: its thickness, and how far it reaches
// beyond the part.
const double BOARD_THICKNESS = 1.6;
const double BOARD_MARGIN = 1.5;
// Pads and holes lie just over the top of the board, so as to be seen on it.
const double PAD_LIFT = 0.02;
const double HOLE_LIFT = 0.04;
// A part with no body: a box this high over the drawing of its silkscreen.
const double NO_BODY_HEIGHT = 1.0;
// The light comes from above, a little from the front and less from the
// right; a face turned away from it keeps this much of its colour.
const Point3 LIGHT{0.2, 0.5, 1.0};
const double SHADOW = 0.35;
// The share of the target the picture fills.
const double FILL_SHARE = 0.92;
const QColor HOLE_COLOR(40, 40, 40);

const double COS30 = 0.8660254037844386;
const double SIN30 = 0.5;

QPointF projected(const Point3 &point)
{
    return QPointF((point.x - point.y) * COS30, (point.x + point.y) * SIN30 - point.z);
}

double dot(const Point3 &a, const Point3 &b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

double length(const Point3 &a)
{
    return std::sqrt(dot(a, a));
}

// The viewer stands towards +X, +Y and +Z.
bool facesViewer(const SolidFace &face)
{
    return face.normal.x + face.normal.y + face.normal.z > 1e-9;
}

SolidFace face(const QList<Point3> &corners, const Point3 &normal, const QColor &color)
{
    return SolidFace{corners, normal, color};
}

// A flat face on the top of the board, its outline 'outline' at 'z'.
SolidFace flat(const QPolygonF &outline, double z, const QColor &color)
{
    QList<Point3> corners;
    for (const QPointF &point : outline)
        corners.append(Point3{point.x(), point.y(), z});
    return face(corners, Point3{0, 0, 1}, color);
}

QPolygonF circlePolygon(const QPointF &center, double radius)
{
    QPolygonF polygon;
    for (int i = 0; i < ROUND_PIECES; ++i) {
        const double angle = 2 * M_PI * i / ROUND_PIECES;
        polygon.append(center + QPointF(radius * std::cos(angle), radius * std::sin(angle)));
    }
    return polygon;
}

QPolygonF padOutline(const Pad &pad)
{
    const QRectF rect = padRect(pad);
    switch (pad.shape) {
    case PadShape::Circle:
        return circlePolygon(rect.center(), rect.width() / 2);
    case PadShape::Rect:
        return QPolygonF(rect);
    case PadShape::Oval: {
        const double round = std::min(rect.width(), rect.height()) / 2;
        QPainterPath path;
        path.addRoundedRect(rect, round, round);
        return path.toFillPolygon();
    }
    }
    return QPolygonF(rect);
}

} // namespace

QList<SolidFace> boxFaces(const QRectF &plan, double bottom, double top, const QColor &color)
{
    const double x0 = plan.left();
    const double x1 = plan.right();
    const double y0 = plan.top();
    const double y1 = plan.bottom();
    return {
        face({{x0, y0, top}, {x1, y0, top}, {x1, y1, top}, {x0, y1, top}}, {0, 0, 1}, color),
        face({{x0, y0, bottom}, {x0, y1, bottom}, {x1, y1, bottom}, {x1, y0, bottom}}, {0, 0, -1},
             color),
        face({{x1, y0, bottom}, {x1, y1, bottom}, {x1, y1, top}, {x1, y0, top}}, {1, 0, 0}, color),
        face({{x0, y0, bottom}, {x0, y0, top}, {x0, y1, top}, {x0, y1, bottom}}, {-1, 0, 0}, color),
        face({{x0, y1, bottom}, {x0, y1, top}, {x1, y1, top}, {x1, y1, bottom}}, {0, 1, 0}, color),
        face({{x0, y0, bottom}, {x1, y0, bottom}, {x1, y0, top}, {x0, y0, top}}, {0, -1, 0}, color),
    };
}

QList<SolidFace> uprightFaces(const QRectF &plan, double bottom, double top, const QColor &color)
{
    const QPointF center = plan.center();
    const double radius = std::min(plan.width(), plan.height()) / 2;
    QList<SolidFace> faces;
    QList<Point3> topRing;
    QList<Point3> bottomRing;
    for (int i = 0; i < ROUND_PIECES; ++i) {
        const double a = 2 * M_PI * i / ROUND_PIECES;
        const double b = 2 * M_PI * (i + 1) / ROUND_PIECES;
        const double middle = (a + b) / 2;
        const Point3 pa{center.x() + radius * std::cos(a), center.y() + radius * std::sin(a), 0};
        const Point3 pb{center.x() + radius * std::cos(b), center.y() + radius * std::sin(b), 0};
        faces.append(face({{pa.x, pa.y, bottom}, {pb.x, pb.y, bottom}, {pb.x, pb.y, top},
                           {pa.x, pa.y, top}},
                          {std::cos(middle), std::sin(middle), 0}, color));
        topRing.append(Point3{pa.x, pa.y, top});
        bottomRing.prepend(Point3{pa.x, pa.y, bottom});
    }
    faces.append(face(topRing, {0, 0, 1}, color));
    faces.append(face(bottomRing, {0, 0, -1}, color));
    return faces;
}

QList<SolidFace> lyingFaces(const QRectF &plan, double bottom, const QColor &color)
{
    // Along X when the plan is wider than it is deep, else along Y.
    const bool alongX = plan.width() >= plan.height();
    const double radius = (alongX ? plan.height() : plan.width()) / 2;
    const double from = alongX ? plan.left() : plan.top();
    const double to = alongX ? plan.right() : plan.bottom();
    const double across = alongX ? plan.center().y() : plan.center().x();
    const double axisZ = bottom + radius;
    // A point on the round side: 'along' the axis, at 'angle' round it.
    const auto at = [&](double along, double angle) {
        const double side = across + radius * std::cos(angle);
        const double z = axisZ + radius * std::sin(angle);
        return alongX ? Point3{along, side, z} : Point3{side, along, z};
    };
    QList<SolidFace> faces;
    QList<Point3> farEnd;
    QList<Point3> nearEnd;
    for (int i = 0; i < ROUND_PIECES; ++i) {
        const double a = 2 * M_PI * i / ROUND_PIECES;
        const double b = 2 * M_PI * (i + 1) / ROUND_PIECES;
        const double middle = (a + b) / 2;
        const Point3 normal = alongX ? Point3{0, std::cos(middle), std::sin(middle)}
                                     : Point3{std::cos(middle), 0, std::sin(middle)};
        faces.append(face({at(from, a), at(to, a), at(to, b), at(from, b)}, normal, color));
        farEnd.append(at(from, a));
        nearEnd.prepend(at(to, a));
    }
    faces.append(face(farEnd, alongX ? Point3{-1, 0, 0} : Point3{0, -1, 0}, color));
    faces.append(face(nearEnd, alongX ? Point3{1, 0, 0} : Point3{0, 1, 0}, color));
    return faces;
}

QList<SolidFace> footprintFaces(const Footprint &footprint)
{
    // The board reaches round the pads, the drawing and the body.
    QSet<int> all;
    for (int i = 0; i < footprint.pads.size(); ++i)
        all.insert(i);
    QRectF extent;
    for (const QRectF &rect : {padsBounds(footprint, all), footprintBodyBounds(footprint),
                               bodyPlan(footprint)}) {
        if (rect.isNull())
            continue;
        extent = extent.isNull() ? rect : extent.united(rect);
    }
    if (extent.isNull())
        extent = QRectF(-BOARD_MARGIN, -BOARD_MARGIN, 2 * BOARD_MARGIN, 2 * BOARD_MARGIN);
    extent.adjust(-BOARD_MARGIN, -BOARD_MARGIN, BOARD_MARGIN, BOARD_MARGIN);

    QList<SolidFace> faces = boxFaces(extent, -BOARD_THICKNESS, 0, paintcolor::maskTop());
    for (const Pad &pad : footprint.pads) {
        // The pads under the part show on its side.
        if (pad.layers == PadLayers::Back)
            continue;
        faces.append(flat(padOutline(pad), PAD_LIFT, paintcolor::padBoth()));
        if (pad.hole > 0)
            faces.append(flat(circlePolygon(toMm(pad.at), nmToMm(pad.hole) / 2), HOLE_LIFT,
                              HOLE_COLOR));
    }
    if (!footprint.body) {
        const QRectF drawing = footprintBodyBounds(footprint);
        if (!drawing.isNull())
            faces.append(boxFaces(drawing, 0, NO_BODY_HEIGHT,
                                  QColor(QLatin1String(DEFAULT_BODY_COLOR))));
        return faces;
    }
    const Body &body = *footprint.body;
    const QRectF plan = bodyPlan(footprint);
    if (plan.isNull())
        return faces;
    const QColor color(body.color);
    const double bottom = nmToMm(body.raised);
    const double top = bottom + nmToMm(body.height);
    switch (body.shape) {
    case BodyShape::Box:
        faces.append(boxFaces(plan, bottom, top, color));
        break;
    case BodyShape::Upright:
        faces.append(uprightFaces(plan, bottom, top, color));
        break;
    case BodyShape::Lying:
        faces.append(lyingFaces(plan, bottom, color));
        break;
    }
    return faces;
}

void paintIsometric(QPainter &painter, const QList<SolidFace> &faces, const QRectF &target)
{
    QRectF bounds;
    for (const SolidFace &face : faces) {
        if (!facesViewer(face))
            continue;
        for (const Point3 &corner : face.corners) {
            const QPointF point = projected(corner);
            bounds = bounds.isNull() ? QRectF(point, QSizeF(1e-9, 1e-9))
                                     : bounds.united(QRectF(point, QSizeF(1e-9, 1e-9)));
        }
    }
    if (bounds.isNull() || target.isEmpty())
        return;
    const double scale = FILL_SHARE * std::min(target.width() / bounds.width(),
                                               target.height() / bounds.height());
    const QPointF shift = target.center() - bounds.center() * scale;
    const double light = length(LIGHT);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Each solid is convex, so its faces turned to the viewer never cover
    // each other; one solid covers those before it.
    for (const SolidFace &face : faces) {
        if (!facesViewer(face))
            continue;
        QPolygonF polygon;
        for (const Point3 &corner : face.corners)
            polygon.append(shift + projected(corner) * scale);
        const double turned =
            std::max(0.0, dot(face.normal, LIGHT) / (light * length(face.normal)));
        const double share = SHADOW + (1 - SHADOW) * turned;
        const QColor shaded = QColor::fromRgbF(face.color.redF() * share,
                                               face.color.greenF() * share,
                                               face.color.blueF() * share);
        painter.setPen(QPen(shaded, 0.5));
        painter.setBrush(shaded);
        painter.drawPolygon(polygon);
    }
    painter.restore();
}

} // namespace ppcb
