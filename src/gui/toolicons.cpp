#include "toolicons.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

namespace ppcb {

namespace {

// The pictures are drawn on a square of TOOL_ICON_SIZE units, a unit a
// pixel. Lines are two units wide and straight ones run on whole units, so
// that each covers two whole rows of pixels and stays sharp; the bus four.
const qreal LINE_WIDTH = 2.0;
const qreal BUS_WIDTH = 4.0;
const qreal DOT_RADIUS = 2.0;
// The knobs of the sliders of Project properties.
const qreal KNOB_RADIUS = 3.0;
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

// A curved arrow going left and back: undo, and mirrored, redo.
void drawUndoArrow(QPainter &painter)
{
    QPainterPath path;
    path.moveTo(20, 19);
    path.cubicTo(20, 11, 16, 9, 6, 9);
    painter.drawPath(path);
    painter.drawPolyline(polygon({{10, 5}, {6, 9}, {10, 13}}));
}

void draw(QPainter &painter, ToolIcon icon, const QColor &color)
{
    switch (icon) {
    case ToolIcon::Menu:
        for (const qreal y : {6.0, 12.0, 18.0})
            painter.drawLine(QPointF(4, y), QPointF(20, y));
        break;
    case ToolIcon::ProjectProperties:
        // Three upright sliders, each with its knob, bolder than a dot: not
        // to be taken for the lines of the menu.
        for (const QPointF &knob : {QPointF(5, 9), QPointF(12, 16), QPointF(19, 7)}) {
            painter.drawLine(QPointF(knob.x(), 3), QPointF(knob.x(), 21));
            painter.save();
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawEllipse(knob, KNOB_RADIUS, KNOB_RADIUS);
            painter.restore();
        }
        break;
    case ToolIcon::Save:
        painter.drawPolygon(polygon({{4, 4}, {17, 4}, {20, 7}, {20, 20}, {4, 20}}));
        painter.drawRect(QRectF(8, 4, 7, 5));
        painter.drawRect(QRectF(7, 13, 10, 7));
        break;
    case ToolIcon::Recent:
        // A clock.
        painter.drawEllipse(QPointF(12, 12), 8, 8);
        painter.drawPolyline(polygon({{12, 7}, {12, 12}, {16, 12}}));
        break;
    case ToolIcon::Print:
        // A printer: the paper in, the body, the sheet out.
        painter.drawPolyline(polygon({{7, 9}, {7, 4}, {17, 4}, {17, 9}}));
        painter.drawRect(QRectF(4, 9, 16, 8));
        painter.drawRect(QRectF(7, 14, 10, 6));
        break;
    case ToolIcon::Pdf:
        // A page with its corner folded and an arrow down out of it.
        painter.drawPolyline(polygon({{14, 4}, {6, 4}, {6, 20}, {18, 20}, {18, 8}, {14, 4}, {14, 8},
                                      {18, 8}}));
        painter.drawLine(QPointF(12, 9), QPointF(12, 17));
        painter.drawPolyline(polygon({{9, 14}, {12, 17}, {15, 14}}));
        break;
    case ToolIcon::Undo:
        drawUndoArrow(painter);
        break;
    case ToolIcon::Redo:
        painter.save();
        painter.translate(TOOL_ICON_SIZE, 0);
        painter.scale(-1, 1);
        drawUndoArrow(painter);
        painter.restore();
        break;
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
        // Upright, two wires coming in from the left with a slant.
        painter.drawPolyline(polygon({{3, 7}, {10, 7}, {14, 11}}));
        painter.drawPolyline(polygon({{3, 15}, {10, 15}, {14, 19}}));
        QPen bus = painter.pen();
        bus.setWidthF(BUS_WIDTH);
        painter.setPen(bus);
        painter.drawLine(QPointF(16, 3), QPointF(16, 21));
        break;
    }
    case ToolIcon::Label:
        painter.drawPolygon(polygon({{3, 8}, {15, 8}, {20, 12}, {15, 16}, {3, 16}}));
        painter.drawLine(QPointF(20, 12), QPointF(22, 12));
        break;
    case ToolIcon::Power:
        painter.drawLine(QPointF(12, 3), QPointF(12, 12));
        painter.drawLine(QPointF(5, 12), QPointF(19, 12));
        painter.drawLine(QPointF(8, 16), QPointF(16, 16));
        painter.drawLine(QPointF(11, 20), QPointF(13, 20));
        break;
    case ToolIcon::NoConnect:
        painter.drawLine(QPointF(2, 12), QPointF(12, 12));
        painter.drawLine(QPointF(7, 7), QPointF(17, 17));
        painter.drawLine(QPointF(17, 7), QPointF(7, 17));
        break;
    case ToolIcon::Outline:
        // A board with a corner cut off, drawn corner by corner.
        painter.drawPolyline(polygon({{4, 20}, {4, 4}, {14, 4}, {20, 10}, {20, 15}}));
        dot(painter, {4, 20}, color);
        dot(painter, {20, 15}, color);
        break;
    case ToolIcon::CutOut: {
        // A board with a notch in its top edge, and the rectangle that made it.
        painter.drawPolygon(
            polygon({{3, 9}, {9, 9}, {9, 14}, {15, 14}, {15, 9}, {21, 9}, {21, 20}, {3, 20}}));
        QPen cut = painter.pen();
        cut.setStyle(Qt::DotLine);
        painter.setPen(cut);
        painter.drawPolyline(polygon({{9, 9}, {9, 3}, {15, 3}, {15, 9}}));
        break;
    }
    case ToolIcon::RectangleBoard:
        // A board with its width marked over it.
        painter.drawRect(QRectF(4, 9, 16, 11));
        painter.drawLine(QPointF(4, 5), QPointF(20, 5));
        painter.drawLine(QPointF(4, 3), QPointF(4, 7));
        painter.drawLine(QPointF(20, 3), QPointF(20, 7));
        break;
    case ToolIcon::Hole:
        // A hole with its middle marked.
        painter.drawEllipse(QPointF(12, 12), 7, 7);
        painter.drawLine(QPointF(12, 2), QPointF(12, 8));
        painter.drawLine(QPointF(12, 16), QPointF(12, 22));
        painter.drawLine(QPointF(2, 12), QPointF(8, 12));
        painter.drawLine(QPointF(16, 12), QPointF(22, 12));
        break;
    case ToolIcon::Track: {
        // A track from pad to pad with a bend at 45 degrees.
        QPen track = painter.pen();
        track.setWidthF(BUS_WIDTH);
        painter.save();
        painter.setPen(track);
        painter.drawPolyline(polygon({{5, 19}, {11, 19}, {19, 11}, {19, 5}}));
        painter.restore();
        dot(painter, {5, 19}, color);
        dot(painter, {19, 5}, color);
        break;
    }
    case ToolIcon::Autoroute: {
        // A track from pad to pad laid round a square in its way.
        painter.drawRect(QRectF(9, 9, 8, 8));
        QPen track = painter.pen();
        track.setWidthF(BUS_WIDTH);
        painter.save();
        painter.setPen(track);
        painter.drawPolyline(polygon({{4, 20}, {4, 7}, {7, 4}, {20, 4}}));
        painter.restore();
        dot(painter, {4, 20}, color);
        dot(painter, {20, 4}, color);
        break;
    }
    case ToolIcon::UpdateBoard:
        // A sheet, an arrow down and right from it, and a board with two pads.
        painter.drawRect(QRectF(3, 3, 9, 7));
        painter.drawPolyline(polygon({{7, 10}, {7, 16}, {12, 16}}));
        painter.drawPolyline(polygon({{10, 14}, {12, 16}, {10, 18}}));
        painter.drawRect(QRectF(13, 11, 8, 10));
        painter.drawEllipse(QPointF(15.5, 16), 1, 1);
        painter.drawEllipse(QPointF(18.5, 16), 1, 1);
        break;
    case ToolIcon::Lock:
        // A padlock: its body and the shackle over it.
        painter.drawRect(QRectF(6, 11, 12, 9));
        painter.drawPolyline(polygon({{8, 11}, {8, 8}}));
        painter.drawPolyline(polygon({{16, 11}, {16, 8}}));
        painter.drawArc(QRectF(8, 4, 8, 8), 0, 180 * 16);
        painter.drawLine(QPointF(12, 14), QPointF(12, 17));
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
        painter.setPen(axis);
        painter.drawLine(QPointF(12, 2), QPointF(12, 22));
        break;
    }
    case ToolIcon::Delete:
        painter.drawLine(QPointF(4, 6), QPointF(20, 6));
        painter.drawPolyline(polygon({{9, 6}, {9, 3}, {15, 3}, {15, 6}}));
        painter.drawPolyline(polygon({{6, 6}, {7, 21}, {17, 21}, {18, 6}}));
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
    case ToolIcon::ZoomFitDrawing:
        // The page, or a zigzag for what is drawn on it, and arrows out.
        if (icon == ToolIcon::ZoomFit)
            painter.drawRect(QRectF(8, 8, 8, 8));
        else
            painter.drawPolyline(polygon({{8, 12}, {9, 9}, {11, 15}, {13, 9}, {15, 15}, {16, 12}}));
        for (const QPointF &corner : {QPointF(3, 3), QPointF(21, 3), QPointF(21, 21), QPointF(3, 21)}) {
            // An arrow from the middle to the corner.
            const QPointF from = corner + (QPointF(12, 12) - corner) * 0.45;
            painter.drawLine(from, corner);
            const QPointF along((corner.x() > 12 ? -4 : 4), 0);
            const QPointF down(0, (corner.y() > 12 ? -4 : 4));
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
    case ToolIcon::BrightGrid:
        // A grid of three lines each way.
        for (const qreal at : {6.0, 12.0, 18.0}) {
            painter.drawLine(QPointF(at, 3), QPointF(at, 21));
            painter.drawLine(QPointF(3, at), QPointF(21, at));
        }
        break;
    case ToolIcon::Library:
        painter.drawRect(QRectF(3, 5, 4, 15));
        painter.drawRect(QRectF(9, 5, 4, 15));
        painter.drawPolygon(polygon({{15, 6}, {18, 5}, {21, 19}, {18, 20}}));
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
    case ToolIcon::Drc:
        // A board with a mark of warning on it.
        painter.drawRect(QRectF(3, 4, 18, 16));
        painter.drawLine(QPointF(12, 8), QPointF(12, 13));
        dot(painter, {12, 16.5}, color);
        break;
    case ToolIcon::Bom:
        painter.drawRect(QRectF(4, 4, 16, 16));
        for (const qreal y : {9.0, 13.0, 17.0})
            painter.drawLine(QPointF(4, y), QPointF(20, y));
        painter.drawLine(QPointF(9, 9), QPointF(9, 20));
        break;
    }
}

// A picture 'drawing' paints on the square of units, with lines of 'color'.
template <typename Drawing>
QPixmap pixmap(const QColor &color, int scale, Drawing drawing)
{
    QPixmap result(TOOL_ICON_SIZE * scale, TOOL_ICON_SIZE * scale);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(scale, scale);
    // Square ends and corners: they end on whole units too.
    painter.setPen(QPen(color, LINE_WIDTH, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::NoBrush);
    drawing(painter);
    painter.end();
    result.setDevicePixelRatio(scale);
    return result;
}

// The same picture at both scales.
template <typename Drawing>
QIcon sharpIcon(const QColor &color, Drawing drawing)
{
    QIcon result;
    result.addPixmap(pixmap(color, 1, drawing));
    result.addPixmap(pixmap(color, SHARP_SCALE, drawing));
    return result;
}

} // namespace

QIcon toolIcon(ToolIcon icon, const QColor &color)
{
    return sharpIcon(color, [icon, &color](QPainter &painter) { draw(painter, icon, color); });
}

QPixmap questionPixmap(const QColor &color, int size, qreal ratio)
{
    QPixmap result(QSize(size, size) * ratio);
    result.setDevicePixelRatio(ratio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QFont font = painter.font();
    font.setBold(true);
    font.setPixelSize(size);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QStringLiteral("?"));
    return result;
}

QIcon layerIcon(const QColor &front, const QColor &back, const QColor &color)
{
    return sharpIcon(color, [&front, &back](QPainter &painter) {
        // Two sheets of copper, the one chosen in front, framed in the
        // colour of the lines.
        painter.setBrush(back);
        painter.drawRect(QRectF(9, 3, 12, 12));
        painter.setBrush(front);
        painter.drawRect(QRectF(3, 9, 12, 12));
    });
}

} // namespace ppcb
