#include "toolicons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

namespace ppcb {

namespace {

// The pictures are drawn on a square of TOOL_ICON_SIZE units.
const qreal LINE_WIDTH = 1.6;
const qreal BUS_WIDTH = 3.5;
const qreal DOT_RADIUS = 1.6;
// Pixels per unit of the larger of the two pixmaps.
const int SHARP_SCALE = 2;

QPolygonF polygon(std::initializer_list<QPointF> points)
{
    return QPolygonF(QList<QPointF>(points));
}

void dot(QPainter &painter, const QPointF &at, const QColor &color)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(at, DOT_RADIUS, DOT_RADIUS);
    painter.restore();
}

void draw(QPainter &painter, ToolIcon icon, const QColor &color)
{
    switch (icon) {
    case ToolIcon::Select:
        painter.drawPolygon(polygon({{6, 3}, {6, 19}, {10, 15.5}, {13, 21.5}, {15.5, 20.5},
                                     {12.5, 14.5}, {18, 14.5}}));
        break;
    case ToolIcon::Wire:
        painter.drawPolyline(polygon({{3, 19}, {10, 19}, {10, 6}, {21, 6}}));
        dot(painter, {3, 19}, color);
        dot(painter, {21, 6}, color);
        break;
    case ToolIcon::Bus: {
        painter.drawLine(QPointF(7, 21), QPointF(7, 13));
        painter.drawLine(QPointF(7, 13), QPointF(10, 10));
        painter.drawLine(QPointF(15, 21), QPointF(15, 13));
        painter.drawLine(QPointF(15, 13), QPointF(18, 10));
        QPen bus = painter.pen();
        bus.setWidthF(BUS_WIDTH);
        painter.setPen(bus);
        painter.drawLine(QPointF(3, 10), QPointF(21, 10));
        break;
    }
    case ToolIcon::Label:
        painter.drawPolygon(polygon({{3, 8}, {15, 8}, {20, 12}, {15, 16}, {3, 16}}));
        painter.drawLine(QPointF(20, 12), QPointF(22, 12));
        break;
    case ToolIcon::Power:
        painter.drawLine(QPointF(12, 3), QPointF(12, 12));
        painter.drawLine(QPointF(5, 12), QPointF(19, 12));
        painter.drawLine(QPointF(8, 15.5), QPointF(16, 15.5));
        painter.drawLine(QPointF(10.5, 19), QPointF(13.5, 19));
        break;
    case ToolIcon::NoConnect:
        painter.drawLine(QPointF(2, 12), QPointF(12, 12));
        painter.drawLine(QPointF(7, 7), QPointF(17, 17));
        painter.drawLine(QPointF(17, 7), QPointF(7, 17));
        break;
    case ToolIcon::Rotate:
        // Three quarters round from the top, ending on the right going up.
        painter.drawArc(QRectF(5, 5, 14, 14), 90 * 16, 270 * 16);
        painter.drawPolyline(polygon({{16, 13}, {19, 9.5}, {22, 13}}));
        break;
    case ToolIcon::Mirror: {
        painter.drawPolygon(polygon({{10, 5}, {10, 19}, {3, 19}}));
        painter.save();
        painter.setBrush(color);
        painter.drawPolygon(polygon({{14, 5}, {14, 19}, {21, 19}}));
        painter.restore();
        QPen axis = painter.pen();
        axis.setStyle(Qt::DashLine);
        axis.setWidthF(LINE_WIDTH / 2);
        painter.setPen(axis);
        painter.drawLine(QPointF(12, 2), QPointF(12, 22));
        break;
    }
    case ToolIcon::Delete:
        painter.drawLine(QPointF(4, 6), QPointF(20, 6));
        painter.drawPolyline(polygon({{9.5, 6}, {9.5, 3.5}, {14.5, 3.5}, {14.5, 6}}));
        painter.drawPolyline(polygon({{6, 6}, {7.5, 21}, {16.5, 21}, {18, 6}}));
        painter.drawLine(QPointF(10, 10), QPointF(10, 17));
        painter.drawLine(QPointF(14, 10), QPointF(14, 17));
        break;
    case ToolIcon::ZoomIn:
    case ToolIcon::ZoomOut:
        painter.drawEllipse(QPointF(10, 10), 6.5, 6.5);
        painter.drawLine(QPointF(15, 15), QPointF(21, 21));
        painter.drawLine(QPointF(7, 10), QPointF(13, 10));
        if (icon == ToolIcon::ZoomIn)
            painter.drawLine(QPointF(10, 7), QPointF(10, 13));
        break;
    case ToolIcon::ZoomFit:
        painter.drawRect(QRectF(8, 8, 8, 8));
        for (const QPointF &corner : {QPointF(3, 3), QPointF(21, 3), QPointF(21, 21), QPointF(3, 21)}) {
            // An arrow from the middle to the corner.
            const QPointF from = corner + (QPointF(12, 12) - corner) * 0.45;
            painter.drawLine(from, corner);
            const QPointF along((corner.x() > 12 ? -3.5 : 3.5), 0);
            const QPointF down(0, (corner.y() > 12 ? -3.5 : 3.5));
            painter.drawPolyline(polygon({corner + along, corner, corner + down}));
        }
        break;
    case ToolIcon::ActualSize: {
        painter.drawLine(QPointF(5, 7), QPointF(7, 5));
        painter.drawLine(QPointF(7, 5), QPointF(7, 19));
        painter.drawLine(QPointF(17, 7), QPointF(19, 5));
        painter.drawLine(QPointF(19, 5), QPointF(19, 19));
        dot(painter, {13, 9.5}, color);
        dot(painter, {13, 15.5}, color);
        break;
    }
    case ToolIcon::Library:
        painter.drawRect(QRectF(3, 5, 4, 15));
        painter.drawRect(QRectF(8, 5, 4, 15));
        painter.drawPolygon(polygon({{13, 6.5}, {16.8, 5.2}, {21, 19}, {17.2, 20.3}}));
        break;
    case ToolIcon::Properties:
        for (const qreal y : {6.0, 12.0, 18.0}) {
            dot(painter, {5, y}, color);
            painter.drawLine(QPointF(9, y), QPointF(20, y));
        }
        break;
    case ToolIcon::Nets:
        painter.drawPolyline(polygon({{5, 18}, {12, 6}, {19, 16}}));
        painter.drawLine(QPointF(12, 6), QPointF(12, 20));
        dot(painter, {5, 18}, color);
        dot(painter, {12, 6}, color);
        dot(painter, {19, 16}, color);
        dot(painter, {12, 20}, color);
        break;
    case ToolIcon::Erc:
        painter.drawPolygon(polygon({{12, 3}, {21.5, 20}, {2.5, 20}}));
        painter.drawLine(QPointF(12, 9), QPointF(12, 14));
        dot(painter, {12, 17}, color);
        break;
    case ToolIcon::Bom:
        painter.drawRect(QRectF(4, 4, 16, 16));
        for (const qreal y : {9.0, 13.0, 17.0})
            painter.drawLine(QPointF(4, y), QPointF(20, y));
        painter.drawLine(QPointF(9, 9), QPointF(9, 20));
        break;
    }
}

QPixmap pixmap(ToolIcon icon, const QColor &color, int scale)
{
    QPixmap result(TOOL_ICON_SIZE * scale, TOOL_ICON_SIZE * scale);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    painter.setPen(QPen(color, LINE_WIDTH, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    draw(painter, icon, color);
    painter.end();
    result.setDevicePixelRatio(scale);
    return result;
}

} // namespace

QIcon toolIcon(ToolIcon icon, const QColor &color)
{
    QIcon result;
    result.addPixmap(pixmap(icon, color, 1));
    result.addPixmap(pixmap(icon, color, SHARP_SCALE));
    return result;
}

} // namespace ppcb
