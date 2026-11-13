#include "paint.h"

#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>

namespace ppcb {

namespace {

// Sizes of what the program adds to a drawing, in millimetres.
const double PIN_LINE_WIDTH = 0.15;
const double PIN_TEXT_HEIGHT = 1.0;
const double PIN_TEXT_GAP = 0.4;
const double PIN_POINT_RADIUS = 0.3;
const double PLACEHOLDER_TEXT_HEIGHT = 1.27;
const double ORIGIN_MARK_SIZE = 0.8;
// The pad number takes this share of the pad's smaller side.
const double PAD_TEXT_SHARE = 0.45;
// Text is drawn with a font this many pixels high and scaled to millimetres.
const int FONT_PIXELS = 100;
// Used when the font cannot tell the height of its capitals.
const double FALLBACK_CAP_SHARE = 0.7;
// Width of a letter as a share of the text height, for bounds only.
const double LETTER_WIDTH_SHARE = 1.0;
// A dashed line: the length of a dash and of the gap after it.
const double DASH_LENGTH = 1.5;
const double DASH_GAP = 1.0;
// QPainter measures angles in sixteenths of a degree.
const int QT_ANGLE_UNITS = 16;

const char REF_PLACEHOLDER[] = "REF";
const char VALUE_PLACEHOLDER[] = "VALUE";

QFont drawingFont()
{
    QFont font;
    font.setPixelSize(FONT_PIXELS);
    return font;
}

double capHeightOf(const QFontMetricsF &metrics)
{
    const double height = metrics.capHeight();
    return height > 0 ? height : FONT_PIXELS * FALLBACK_CAP_SHARE;
}

// Text whose capitals are 'height' mm tall, vertically centred on 'at'.
void drawText(QPainter &painter, const QPointF &at, const QString &text, double height,
              TextAlign align, const QColor &color)
{
    const QFont font = drawingFont();
    const QFontMetricsF metrics(font);
    const double capHeight = capHeightOf(metrics);
    const double width = metrics.horizontalAdvance(text);

    double left = 0;
    if (align == TextAlign::Center)
        left = -width / 2;
    else if (align == TextAlign::Right)
        left = -width;

    painter.save();
    painter.translate(at);
    painter.scale(height / capHeight, height / capHeight);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QPointF(left, capHeight / 2), text);
    painter.restore();
}

// A rectangle surely holding a text drawn by drawText().
QRectF textBounds(const QPointF &at, const QString &text, double height, TextAlign align)
{
    const double width = text.size() * height * LETTER_WIDTH_SHARE;
    double left = at.x();
    if (align == TextAlign::Center)
        left -= width / 2;
    else if (align == TextAlign::Right)
        left -= width;
    return QRectF(left, at.y() - height, width, 2 * height);
}

QPen linePen(const QColor &color, double width)
{
    QPen pen(color, width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

// Dashes of the same length whatever the line width. Their ends are cut
// flat: round ends would eat into the gaps.
QPen dashedPen(const QColor &color, double width)
{
    QPen pen(color, width);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::RoundJoin);
    // QPen counts a dash pattern in line widths.
    if (width > 0)
        pen.setDashPattern({DASH_LENGTH / width, DASH_GAP / width});
    else
        pen.setStyle(Qt::DashLine);
    return pen;
}

QRectF circleRect(const QPointF &center, double radius)
{
    return QRectF(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius);
}

void paintGraphic(QPainter &painter, const Graphic &graphic, const QColor &color)
{
    const QPointF a = toMm(graphic.a);
    const QPointF b = toMm(graphic.b);
    const double radius = nmToMm(graphic.radius);

    const double width = nmToMm(graphic.width);
    painter.setPen(graphic.dashed ? dashedPen(color, width) : linePen(color, width));
    painter.setBrush(Qt::NoBrush);

    switch (graphic.kind) {
    case GraphicKind::Line:
        painter.drawLine(a, b);
        break;
    case GraphicKind::Rect:
        painter.drawRect(QRectF(a, b).normalized());
        break;
    case GraphicKind::Circle:
        painter.drawEllipse(a, radius, radius);
        break;
    case GraphicKind::Arc:
        // The model's angles grow clockwise on screen; QPainter's grow
        // counter-clockwise, hence the minus signs.
        painter.drawArc(circleRect(a, radius), -graphic.startAngle * QT_ANGLE_UNITS,
                        -graphic.spanAngle * QT_ANGLE_UNITS);
        break;
    case GraphicKind::Text:
        drawText(painter, a, graphic.text, nmToMm(graphic.height), graphic.align, color);
        break;
    case GraphicKind::Polygon: {
        QPolygonF polygon;
        for (const Point &point : graphic.points)
            polygon.append(toMm(point));
        if (graphic.filled)
            painter.setBrush(color);
        painter.drawPolygon(polygon);
        break;
    }
    }
}

QRectF graphicBounds(const Graphic &graphic)
{
    const QPointF a = toMm(graphic.a);
    const QPointF b = toMm(graphic.b);
    const double half = nmToMm(graphic.width) / 2;
    QRectF bounds;

    switch (graphic.kind) {
    case GraphicKind::Line:
    case GraphicKind::Rect:
        bounds = QRectF(a, b).normalized();
        break;
    case GraphicKind::Circle:
    case GraphicKind::Arc:
        // The whole circle for an arc: larger than needed, never smaller.
        bounds = circleRect(a, nmToMm(graphic.radius));
        break;
    case GraphicKind::Text:
        return textBounds(a, graphic.text, nmToMm(graphic.height), graphic.align);
    case GraphicKind::Polygon: {
        QPolygonF polygon;
        for (const Point &point : graphic.points)
            polygon.append(toMm(point));
        bounds = polygon.boundingRect();
        break;
    }
    }
    return bounds.adjusted(-half, -half, half, half);
}

// Collects rectangles into one. QRectF::united() ignores rectangles of zero
// width or height (a horizontal line), so the union is done by hand.
class Bounds
{
public:
    void add(const QRectF &rect)
    {
        if (m_empty) {
            m_rect = rect;
            m_empty = false;
            return;
        }
        m_rect.setCoords(std::min(m_rect.left(), rect.left()), std::min(m_rect.top(), rect.top()),
                         std::max(m_rect.right(), rect.right()),
                         std::max(m_rect.bottom(), rect.bottom()));
    }

    QRectF rect() const { return m_rect; }

private:
    QRectF m_rect;
    bool m_empty = true;
};

void paintOriginMark(QPainter &painter)
{
    painter.setPen(QPen(paintcolor::placeholder(), 0));
    painter.drawLine(QPointF(-ORIGIN_MARK_SIZE, 0), QPointF(ORIGIN_MARK_SIZE, 0));
    painter.drawLine(QPointF(0, -ORIGIN_MARK_SIZE), QPointF(0, ORIGIN_MARK_SIZE));
}

void paintPlaceholders(QPainter &painter, const Point &refAt, const Point &valueAt)
{
    drawText(painter, toMm(refAt), QLatin1String(REF_PLACEHOLDER), PLACEHOLDER_TEXT_HEIGHT,
             TextAlign::Center, paintcolor::placeholder());
    drawText(painter, toMm(valueAt), QLatin1String(VALUE_PLACEHOLDER), PLACEHOLDER_TEXT_HEIGHT,
             TextAlign::Center, paintcolor::placeholder());
}

void addCommonBounds(Bounds &bounds, const Point &refAt, const Point &valueAt)
{
    bounds.add(QRectF(-ORIGIN_MARK_SIZE, -ORIGIN_MARK_SIZE, 2 * ORIGIN_MARK_SIZE,
                      2 * ORIGIN_MARK_SIZE));
    bounds.add(textBounds(toMm(refAt), QLatin1String(REF_PLACEHOLDER), PLACEHOLDER_TEXT_HEIGHT,
                          TextAlign::Center));
    bounds.add(textBounds(toMm(valueAt), QLatin1String(VALUE_PLACEHOLDER),
                          PLACEHOLDER_TEXT_HEIGHT, TextAlign::Center));
}

bool isHorizontal(const Pin &pin)
{
    return pin.side == PinSide::Left || pin.side == PinSide::Right;
}

void paintPin(QPainter &painter, const Pin &pin, bool showNumber, bool showName)
{
    const QPointF at = toMm(pin.at);
    const QPointF end = toMm(pinBodyEnd(pin));
    const QPointF middle = (at + end) / 2;

    painter.setPen(linePen(paintcolor::symbol(), PIN_LINE_WIDTH));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(at, end);
    // Marks the point a wire connects to.
    painter.drawEllipse(at, PIN_POINT_RADIUS, PIN_POINT_RADIUS);

    const double offset = PIN_TEXT_GAP + PIN_TEXT_HEIGHT / 2;
    if (showNumber) {
        // Above a horizontal pin, to the left of a vertical one.
        if (isHorizontal(pin)) {
            drawText(painter, middle - QPointF(0, offset), pin.number, PIN_TEXT_HEIGHT,
                     TextAlign::Center, paintcolor::pinText());
        } else {
            drawText(painter, middle - QPointF(PIN_TEXT_GAP, 0), pin.number, PIN_TEXT_HEIGHT,
                     TextAlign::Right, paintcolor::pinText());
        }
    }
    if (showName && !pin.name.isEmpty()) {
        // Inside the body, just past the end of the pin.
        switch (pin.side) {
        case PinSide::Left:
            drawText(painter, end + QPointF(PIN_TEXT_GAP, 0), pin.name, PIN_TEXT_HEIGHT,
                     TextAlign::Left, paintcolor::pinText());
            break;
        case PinSide::Right:
            drawText(painter, end - QPointF(PIN_TEXT_GAP, 0), pin.name, PIN_TEXT_HEIGHT,
                     TextAlign::Right, paintcolor::pinText());
            break;
        case PinSide::Up:
            drawText(painter, end + QPointF(0, offset), pin.name, PIN_TEXT_HEIGHT,
                     TextAlign::Center, paintcolor::pinText());
            break;
        case PinSide::Down:
            drawText(painter, end - QPointF(0, offset), pin.name, PIN_TEXT_HEIGHT,
                     TextAlign::Center, paintcolor::pinText());
            break;
        }
    }
}

QColor padColor(PadLayers layers)
{
    switch (layers) {
    case PadLayers::Both:
        return paintcolor::padBoth();
    case PadLayers::Front:
        return paintcolor::padFront();
    case PadLayers::Back:
        return paintcolor::padBack();
    }
    return paintcolor::padBoth();
}

QRectF padRect(const Pad &pad)
{
    const QPointF at = toMm(pad.at);
    const double width = nmToMm(pad.sizeX);
    const double height = nmToMm(pad.sizeY);
    return QRectF(at.x() - width / 2, at.y() - height / 2, width, height);
}

void paintPad(QPainter &painter, const Pad &pad)
{
    const QRectF rect = padRect(pad);
    const double smaller = std::min(rect.width(), rect.height());

    painter.setPen(Qt::NoPen);
    painter.setBrush(padColor(pad.layers));
    switch (pad.shape) {
    case PadShape::Circle:
        painter.drawEllipse(rect);
        break;
    case PadShape::Rect:
        painter.drawRect(rect);
        break;
    case PadShape::Oval:
        painter.drawRoundedRect(rect, smaller / 2, smaller / 2);
        break;
    }

    if (pad.hole > 0) {
        painter.setBrush(paintcolor::background());
        painter.drawEllipse(rect.center(), nmToMm(pad.hole) / 2, nmToMm(pad.hole) / 2);
    }
    if (!pad.number.isEmpty()) {
        drawText(painter, rect.center(), pad.number, smaller * PAD_TEXT_SHARE, TextAlign::Center,
                 pad.hole > 0 ? paintcolor::symbol() : paintcolor::padText());
    }
}

} // namespace

QPointF toMm(const Point &point)
{
    return QPointF(nmToMm(point.x), nmToMm(point.y));
}

Point pinBodyEnd(const Pin &pin)
{
    // The wire comes from 'side', so the body lies the other way.
    switch (pin.side) {
    case PinSide::Left:
        return pin.at + Point{pin.length, 0};
    case PinSide::Right:
        return pin.at - Point{pin.length, 0};
    case PinSide::Up:
        return pin.at + Point{0, pin.length};
    case PinSide::Down:
        return pin.at - Point{0, pin.length};
    }
    return pin.at;
}

QRectF symbolBounds(const Symbol &symbol)
{
    Bounds bounds;
    addCommonBounds(bounds, symbol.refAt, symbol.valueAt);
    for (const Graphic &graphic : symbol.graphics)
        bounds.add(graphicBounds(graphic));
    // Room for the pin's texts on every side of its line.
    const double margin = PIN_TEXT_GAP + 2 * PIN_TEXT_HEIGHT;
    for (const Pin &pin : symbol.pins) {
        const QRectF line = QRectF(toMm(pin.at), toMm(pinBodyEnd(pin))).normalized();
        bounds.add(line.adjusted(-margin, -margin, margin, margin));
        bounds.add(textBounds(toMm(pinBodyEnd(pin)), pin.name, PIN_TEXT_HEIGHT, TextAlign::Center)
                       .adjusted(-margin, -margin, margin, margin));
    }
    return bounds.rect();
}

QRectF footprintBounds(const Footprint &footprint)
{
    Bounds bounds;
    addCommonBounds(bounds, footprint.refAt, footprint.valueAt);
    for (const Graphic &graphic : footprint.graphics)
        bounds.add(graphicBounds(graphic));
    for (const Pad &pad : footprint.pads)
        bounds.add(padRect(pad));
    return bounds.rect();
}

void paintSymbol(QPainter &painter, const Symbol &symbol)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintOriginMark(painter);
    for (const Graphic &graphic : symbol.graphics)
        paintGraphic(painter, graphic, paintcolor::symbol());
    for (const Pin &pin : symbol.pins)
        paintPin(painter, pin, symbol.showPinNumbers, symbol.showPinNames);
    paintPlaceholders(painter, symbol.refAt, symbol.valueAt);
    painter.restore();
}

void paintFootprint(QPainter &painter, const Footprint &footprint)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Copper first, silkscreen over it, as on a real board.
    for (const Pad &pad : footprint.pads)
        paintPad(painter, pad);
    for (const Graphic &graphic : footprint.graphics)
        paintGraphic(painter, graphic, paintcolor::silkscreen());
    paintOriginMark(painter);
    paintPlaceholders(painter, footprint.refAt, footprint.valueAt);
    painter.restore();
}

} // namespace ppcb
