#include "paint.h"

#include "wiring.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

// Sizes of what the program adds to a drawing, in millimetres.
// Symbols and wires are drawn at least this thick on the screen and on paper,
// whatever the width their files give.
const double SYMBOL_LINE_WIDTH = 0.25;
const double PIN_LINE_WIDTH = SYMBOL_LINE_WIDTH;
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


// The schematic sheet.
const double A4_LONG_SIDE = 297.0;
const double A4_SHORT_SIDE = 210.0;
const double A3_LONG_SIDE = 420.0;
const double A3_SHORT_SIDE = 297.0;
const double FRAME_MARGIN = 5.0;
const double FRAME_LINE_WIDTH = 0.3;
const double TITLE_BLOCK_WIDTH = 90.0;
const double TITLE_ROW_HEIGHT = 6.0;
const double TITLE_TEXT_HEIGHT = 2.5;
const double TITLE_TEXT_MARGIN = 2.0;
const double WIRE_WIDTH = SYMBOL_LINE_WIDTH;
const double BUS_WIDTH = 0.5;
// The dot where wires join.
const double JUNCTION_DIAMETER = 0.8;
const double LABEL_TEXT_HEIGHT = 1.27;
const double LABEL_GAP = 0.4;
const double POWER_STEM = 2.54;
const double POWER_BAR_HALF = 1.27;
// The arrow's head: how far it reaches back and to each side.
const double POWER_ARROW = 0.8;
const int GROUND_BARS = 3;
const double GROUND_BAR_STEP = 0.5;
// How far each slanted stroke of the chassis reaches, back and to the side.
const double CHASSIS_STROKE = 0.8;
const double NO_CONNECT_HALF = 0.635;

// The board.
const double OUTLINE_WIDTH = 0.15;
const double ZONE_LINE_WIDTH = 0.15;
// A circle or an arc is found by the mouse along this many straight pieces.
const int CURVE_PIECES = 32;
const double FULL_TURN_DEGREES = 360.0;
// A rounded corner of the outline is drawn with this many straight pieces.
const int CORNER_PIECES = 12;
// What an empty board is fitted to.
const double EMPTY_BOARD_WIDTH = 100.0;
const double EMPTY_BOARD_HEIGHT = 80.0;
const double HALF_TURN = 180.0;
const double PI = 3.14159265358979323846;

// What stands in for a symbol or footprint the project library lacks.
const double MISSING_SYMBOL_SIZE = 5.08;
const double MISSING_FOOTPRINT_SIZE = 3.0;
const double MISSING_LINE_WIDTH = 0.2;
// The question mark takes this share of the square's side.
const double MISSING_TEXT_SHARE = 0.6;

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
// 'upright' keeps it readable inside a turned or mirrored drawing: it runs
// left to right or bottom to top and is never mirrored; the anchor stays.
void drawText(QPainter &painter, const QPointF &at, const QString &text, double height,
              TextAlign align, const QColor &color, bool upright = false)
{
    const QFont font = drawingFont();
    const QFontMetricsF metrics(font);
    const double capHeight = capHeightOf(metrics);
    const double width = metrics.horizontalAdvance(text);

    painter.save();
    painter.translate(at);
    if (upright) {
        // Where the text's own X axis points on screen.
        const QTransform world = painter.worldTransform();
        const bool runsLeft = world.m11() < 0 && !qFuzzyIsNull(world.m11());
        const bool runsDown = qFuzzyIsNull(world.m11()) && world.m12() > 0;
        if (runsLeft || runsDown) {
            painter.rotate(HALF_TURN);
            if (align == TextAlign::Left)
                align = TextAlign::Right;
            else if (align == TextAlign::Right)
                align = TextAlign::Left;
        }
        // The text is centred on its line, so flipping it over the line
        // undoes a mirror without moving it.
        if (painter.worldTransform().determinant() < 0)
            painter.scale(1, -1);
    }

    double left = 0;
    if (align == TextAlign::Center)
        left = -width / 2;
    else if (align == TextAlign::Right)
        left = -width;

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

// The width a graphic's line is drawn with: its own, but not under 'minWidth'.
double lineWidth(const Graphic &graphic, double minWidth)
{
    return qMax(nmToMm(graphic.width), minWidth);
}

// 'upright': see drawText().
void paintGraphic(QPainter &painter, const Graphic &graphic, const QColor &color,
                  bool upright = false, double minWidth = 0)
{
    const QPointF a = toMm(graphic.a);
    const QPointF b = toMm(graphic.b);
    const double radius = nmToMm(graphic.radius);

    const double width = lineWidth(graphic, minWidth);
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
        drawText(painter, a, graphic.text, nmToMm(graphic.height), graphic.align, color, upright);
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

QRectF graphicBounds(const Graphic &graphic, double minWidth = 0)
{
    const QPointF a = toMm(graphic.a);
    const QPointF b = toMm(graphic.b);
    const double half = lineWidth(graphic, minWidth) / 2;
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

// The line a drawing is found by: its outline, or nothing for one that is
// found by its area (a text, a filled polygon). Circles and arcs are cut into
// straight pieces.
QPolygonF graphicLine(const Graphic &graphic)
{
    const QPointF a = toMm(graphic.a);
    const QPointF b = toMm(graphic.b);
    QPolygonF line;
    switch (graphic.kind) {
    case GraphicKind::Line:
        line << a << b;
        break;
    case GraphicKind::Rect:
        line << a << QPointF(b.x(), a.y()) << b << QPointF(a.x(), b.y()) << a;
        break;
    case GraphicKind::Circle:
    case GraphicKind::Arc: {
        const bool whole = graphic.kind == GraphicKind::Circle;
        const double start = whole ? 0 : graphic.startAngle;
        const double span = whole ? FULL_TURN_DEGREES : graphic.spanAngle;
        const double radius = nmToMm(graphic.radius);
        for (int i = 0; i <= CURVE_PIECES; ++i) {
            // Angles grow towards +Y, as everywhere in the model.
            const double angle = (start + span * i / CURVE_PIECES) * PI / HALF_TURN;
            line << a + QPointF(radius * std::cos(angle), radius * std::sin(angle));
        }
        break;
    }
    case GraphicKind::Polygon:
        if (graphic.filled)
            break;
        for (const Point &point : graphic.points)
            line << toMm(point);
        line << toMm(graphic.points.value(0));
        break;
    case GraphicKind::Text:
        break;
    }
    return line;
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
    bool isEmpty() const { return m_empty; }

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

// 'markPoint' circles the point a wire connects to: in the library view,
// where the symbol is being drawn, not on a sheet.
void paintPin(QPainter &painter, const Pin &pin, bool showNumber, bool showName, bool upright,
              bool markPoint)
{
    const QPointF at = toMm(pin.at);
    const QPointF end = toMm(pinBodyEnd(pin));
    const QPointF middle = (at + end) / 2;

    painter.setPen(linePen(paintcolor::symbol(), PIN_LINE_WIDTH));
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(at, end);
    if (markPoint)
        painter.drawEllipse(at, PIN_POINT_RADIUS, PIN_POINT_RADIUS);

    const double offset = PIN_TEXT_GAP + PIN_TEXT_HEIGHT / 2;
    if (showNumber) {
        // Above a horizontal pin, to the left of a vertical one.
        if (isHorizontal(pin)) {
            drawText(painter, middle - QPointF(0, offset), pin.number, PIN_TEXT_HEIGHT,
                     TextAlign::Center, paintcolor::pinText(), upright);
        } else {
            drawText(painter, middle - QPointF(PIN_TEXT_GAP, 0), pin.number, PIN_TEXT_HEIGHT,
                     TextAlign::Right, paintcolor::pinText(), upright);
        }
    }
    if (showName && !pin.name.isEmpty()) {
        // Inside the body, just past the end of the pin.
        switch (pin.side) {
        case PinSide::Left:
            drawText(painter, end + QPointF(PIN_TEXT_GAP, 0), pin.name, PIN_TEXT_HEIGHT,
                     TextAlign::Left, paintcolor::pinText(), upright);
            break;
        case PinSide::Right:
            drawText(painter, end - QPointF(PIN_TEXT_GAP, 0), pin.name, PIN_TEXT_HEIGHT,
                     TextAlign::Right, paintcolor::pinText(), upright);
            break;
        case PinSide::Up:
            drawText(painter, end + QPointF(0, offset), pin.name, PIN_TEXT_HEIGHT,
                     TextAlign::Center, paintcolor::pinText(), upright);
            break;
        case PinSide::Down:
            drawText(painter, end - QPointF(0, offset), pin.name, PIN_TEXT_HEIGHT,
                     TextAlign::Center, paintcolor::pinText(), upright);
            break;
        }
    }
}

// Copper of one side of the board.
QColor copperColor(BoardSide side)
{
    return side == BoardSide::Top ? paintcolor::padFront() : paintcolor::padBack();
}

BoardSide otherSide(BoardSide side)
{
    return side == BoardSide::Top ? BoardSide::Bottom : BoardSide::Top;
}

// 'partSide' is the side the part is mounted on: a pad's Front is that side.
QColor padColor(PadLayers layers, BoardSide partSide)
{
    switch (layers) {
    case PadLayers::Both:
        return paintcolor::padBoth();
    case PadLayers::Front:
        return copperColor(partSide);
    case PadLayers::Back:
        return copperColor(otherSide(partSide));
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

void paintPad(QPainter &painter, const Pad &pad, BoardSide partSide = BoardSide::Top,
              bool upright = false)
{
    const QRectF rect = padRect(pad);
    const double smaller = std::min(rect.width(), rect.height());

    painter.setPen(Qt::NoPen);
    painter.setBrush(padColor(pad.layers, partSide));
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
                 pad.hole > 0 ? paintcolor::symbol() : paintcolor::padText(), upright);
    }
}


// The graphics and the pins of a symbol, without the marks of the library
// view; 'markPoints' circles the points wires connect to (see paintPin()).
void paintSymbolBody(QPainter &painter, const Symbol &symbol, bool upright, bool markPoints)
{
    for (const Graphic &graphic : symbol.graphics)
        paintGraphic(painter, graphic, paintcolor::symbol(), upright, SYMBOL_LINE_WIDTH);
    for (const Pin &pin : symbol.pins)
        paintPin(painter, pin, symbol.showPinNumbers, symbol.showPinNames, upright, markPoints);
}

// From the coordinates of a symbol to those of the sheet: the symbol is
// mirrored left to right first, then turned clockwise, then moved.
QTransform componentTransform(const Component &component)
{
    QTransform transform;
    transform.translate(nmToMm(component.at.x), nmToMm(component.at.y));
    transform.rotate(component.rotation);
    if (component.mirrored)
        transform.scale(-1, 1);
    return transform;
}

// The same for a footprint on the board; one on the bottom side is flipped.
QTransform placementTransform(const Placement &placement)
{
    QTransform transform;
    transform.translate(nmToMm(placement.at.x), nmToMm(placement.at.y));
    transform.rotate(placement.rotation);
    if (placement.side == BoardSide::Bottom)
        transform.scale(-1, 1);
    return transform;
}

QPolygonF toPolygon(const QList<Point> &points)
{
    QPolygonF polygon;
    for (const Point &point : points)
        polygon.append(toMm(point));
    return polygon;
}

// A square around 'center' with a question mark in it.
QRectF missingRect(const QPointF &center, double size)
{
    return QRectF(center.x() - size / 2, center.y() - size / 2, size, size);
}

void paintMissing(QPainter &painter, const QPointF &center, double size)
{
    painter.save();
    painter.setPen(dashedPen(paintcolor::missing(), MISSING_LINE_WIDTH));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(missingRect(center, size));
    drawText(painter, center, QStringLiteral("?"), size * MISSING_TEXT_SHARE, TextAlign::Center,
             paintcolor::missing());
    painter.restore();
}

void paintComponent(QPainter &painter, const Library &library, const Component &component)
{
    const Symbol *symbol = library.findSymbolOfPart(component.part);
    if (symbol == nullptr) {
        paintMissing(painter, toMm(component.at), MISSING_SYMBOL_SIZE);
    } else {
        painter.save();
        painter.setTransform(componentTransform(component), true);
        paintSymbolBody(painter, *symbol, true, false);
        painter.restore();
    }

    drawText(painter, toMm(componentTextPoint(component, ComponentText::Designator)),
             component.designator, PLACEHOLDER_TEXT_HEIGHT, TextAlign::Center,
             paintcolor::symbol());
    drawText(painter, toMm(componentTextPoint(component, ComponentText::Value)),
             componentTextShown(component, ComponentText::Value, library),
             PLACEHOLDER_TEXT_HEIGHT, TextAlign::Center,
             component.value.isEmpty() ? paintcolor::placeholder() : paintcolor::symbol());
}

// The text lies along the label's direction, just above its point.
void paintLabel(QPainter &painter, const Label &label)
{
    painter.save();
    painter.translate(toMm(label.at));
    painter.rotate(label.rotation);
    drawText(painter, QPointF(0, -(LABEL_TEXT_HEIGHT / 2 + LABEL_GAP)), label.text,
             LABEL_TEXT_HEIGHT, TextAlign::Left, paintcolor::label(), true);
    painter.restore();
}

// The lines of a power symbol, not turned: a stem up from the connection
// point at the origin, and at its end what 'kind' draws.
QList<QLineF> powerLines(PowerKind kind)
{
    const double s = POWER_STEM;
    const double h = POWER_BAR_HALF;
    QList<QLineF> lines = {QLineF(0, 0, 0, -s)};
    switch (kind) {
    case PowerKind::Bar:
        lines.append(QLineF(-h, -s, h, -s));
        break;
    case PowerKind::Arrow:
        lines.append(QLineF(-POWER_ARROW, -s + POWER_ARROW, 0, -s));
        lines.append(QLineF(0, -s, POWER_ARROW, -s + POWER_ARROW));
        break;
    case PowerKind::Ground:
        for (int i = 0; i < GROUND_BARS; ++i) {
            const double half = h * (GROUND_BARS - i) / GROUND_BARS;
            const double y = -s - i * GROUND_BAR_STEP;
            lines.append(QLineF(-half, y, half, y));
        }
        break;
    case PowerKind::Chassis:
        lines.append(QLineF(-h, -s, h, -s));
        for (const double x : {-h, 0.0, h})
            lines.append(QLineF(x, -s, x - CHASSIS_STROKE, -s - CHASSIS_STROKE));
        break;
    }
    return lines;
}

// The name a power symbol shows: none for a ground symbol of the usual ground.
QString powerNameShown(const PowerSymbol &power)
{
    if (power.kind == PowerKind::Ground && power.name == QLatin1String(GROUND_NAME))
        return QString();
    return power.name;
}

// Where the name of a power symbol stands, not turned: beyond its lines.
QPointF powerNameAt(PowerKind kind)
{
    double top = 0;
    for (const QLineF &line : powerLines(kind))
        top = std::min({top, line.y1(), line.y2()});
    return QPointF(0, top - (LABEL_TEXT_HEIGHT / 2 + LABEL_GAP));
}

// The lines of 'kind' and the name beyond them. Not turned, the stem points up.
void paintPowerSymbol(QPainter &painter, const PowerSymbol &power)
{
    painter.save();
    painter.translate(toMm(power.at));
    painter.rotate(power.rotation);
    painter.setPen(linePen(paintcolor::symbol(), WIRE_WIDTH));
    for (const QLineF &line : powerLines(power.kind))
        painter.drawLine(line);
    drawText(painter, powerNameAt(power.kind), powerNameShown(power), LABEL_TEXT_HEIGHT,
             TextAlign::Center, paintcolor::symbol(), true);
    painter.restore();
}

void paintNoConnect(QPainter &painter, const NoConnect &mark)
{
    const QPointF at = toMm(mark.at);
    const double half = NO_CONNECT_HALF;
    painter.setPen(linePen(paintcolor::noConnect(), WIRE_WIDTH));
    painter.drawLine(at + QPointF(-half, -half), at + QPointF(half, half));
    painter.drawLine(at + QPointF(-half, half), at + QPointF(half, -half));
}

// The frame inside the paper's edge and the title block in its bottom right
// corner: one row for every field that is not empty.
void paintFrame(QPainter &painter, const Schematic &schematic, int index,
                const TitleBlock &titleBlock)
{
    const Sheet &sheet = schematic.sheets.at(index);
    const QRectF paper = paperRect(sheet.paper);
    const QRectF frame = paper.adjusted(FRAME_MARGIN, FRAME_MARGIN, -FRAME_MARGIN, -FRAME_MARGIN);

    QStringList rows;
    const QStringList fields = {
        titleBlock.title,    sheet.title,         titleBlock.author, titleBlock.organization,
        titleBlock.contacts, titleBlock.revision, titleBlock.date,
    };
    for (const QString &field : fields) {
        if (!field.isEmpty())
            rows.append(field);
    }
    rows.append(QCoreApplication::translate("ppcb::Paint", "Sheet %1 of %2")
                    .arg(index + 1)
                    .arg(schematic.sheets.size()));
    if (!titleBlock.note.isEmpty())
        rows.append(titleBlock.note);

    const QRectF block(frame.right() - TITLE_BLOCK_WIDTH,
                       frame.bottom() - rows.size() * TITLE_ROW_HEIGHT, TITLE_BLOCK_WIDTH,
                       rows.size() * TITLE_ROW_HEIGHT);

    // The paper's edge as a hairline, the frame and the block as real lines.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(paintcolor::sheetFrame(), 0));
    painter.drawRect(paper);
    painter.setPen(linePen(paintcolor::sheetFrame(), FRAME_LINE_WIDTH));
    painter.drawRect(frame);
    painter.drawRect(block);
    for (int row = 0; row < rows.size(); ++row) {
        const double top = block.top() + row * TITLE_ROW_HEIGHT;
        if (row > 0) {
            painter.setPen(linePen(paintcolor::sheetFrame(), FRAME_LINE_WIDTH));
            painter.drawLine(QPointF(block.left(), top), QPointF(block.right(), top));
        }
        drawText(painter, QPointF(block.left() + TITLE_TEXT_MARGIN, top + TITLE_ROW_HEIGHT / 2),
                 rows.at(row), TITLE_TEXT_HEIGHT, TextAlign::Left, paintcolor::sheetFrame());
    }
}

double lengthOf(const QPointF &vector)
{
    return std::hypot(vector.x(), vector.y());
}

// The points that draw corner 'corner' of an outline coming from 'from' and
// going on to 'to': the corner itself, or an arc of 'radius' that touches
// both sides. An arc too large for its sides is made as large as fits.
QList<QPointF> cornerPoints(const QPointF &from, const QPointF &corner, const QPointF &to,
                            double radius)
{
    const double lengthIn = lengthOf(from - corner);
    const double lengthOut = lengthOf(to - corner);
    if (radius <= 0 || qFuzzyIsNull(lengthIn) || qFuzzyIsNull(lengthOut))
        return {corner};

    const QPointF in = (from - corner) / lengthIn;
    const QPointF out = (to - corner) / lengthOut;
    const double cosine = qBound(-1.0, in.x() * out.x() + in.y() * out.y(), 1.0);
    const double halfAngle = std::acos(cosine) / 2;
    // No corner to round when the sides go straight on or fold back.
    if (qFuzzyIsNull(std::sin(halfAngle)) || qFuzzyIsNull(std::cos(halfAngle)))
        return {corner};

    // An arc may take at most half of each side: the other half belongs to
    // the corner at the side's other end.
    const double fromCorner = std::min(radius / std::tan(halfAngle),
                                       std::min(lengthIn, lengthOut) / 2);
    const double fitted = fromCorner * std::tan(halfAngle);
    const QPointF start = corner + in * fromCorner;
    const QPointF end = corner + out * fromCorner;
    const QPointF bisector = in + out;
    const QPointF center = corner + bisector / lengthOf(bisector) * (fitted / std::sin(halfAngle));

    const double startAngle = std::atan2(start.y() - center.y(), start.x() - center.x());
    double sweep = std::atan2(end.y() - center.y(), end.x() - center.x()) - startAngle;
    // The short way round.
    if (sweep > PI)
        sweep -= 2 * PI;
    else if (sweep < -PI)
        sweep += 2 * PI;

    QList<QPointF> points;
    for (int piece = 0; piece <= CORNER_PIECES; ++piece) {
        const double angle = startAngle + sweep * piece / CORNER_PIECES;
        points.append(center + QPointF(std::cos(angle), std::sin(angle)) * fitted);
    }
    return points;
}

// The closed outline of the board; empty for a board without one.
QPainterPath outlinePath(const QList<OutlineCorner> &outline)
{
    QPainterPath path;
    const int count = int(outline.size());
    for (int i = 0; i < count; ++i) {
        const QPointF from = toMm(outline.at((i + count - 1) % count).at);
        const QPointF to = toMm(outline.at((i + 1) % count).at);
        const QList<QPointF> points = cornerPoints(from, toMm(outline.at(i).at), to,
                                                   nmToMm(outline.at(i).radius));
        for (const QPointF &point : points) {
            if (path.elementCount() == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
    }
    if (count > 0)
        path.closeSubpath();
    return path;
}

void paintTracks(QPainter &painter, const Board &board, BoardSide side)
{
    for (const Track &track : board.tracks) {
        if (track.side != side)
            continue;
        painter.setPen(linePen(copperColor(side), nmToMm(track.width)));
        painter.drawLine(toMm(track.from), toMm(track.to));
    }
}

// A filled circle of copper with a hole in it.
void paintRing(QPainter &painter, const QPointF &at, double copperDiameter, double holeDiameter)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(paintcolor::padBoth());
    painter.drawEllipse(at, copperDiameter / 2, copperDiameter / 2);
    painter.setBrush(paintcolor::background());
    painter.drawEllipse(at, holeDiameter / 2, holeDiameter / 2);
}

void paintMountingHole(QPainter &painter, const MountingHole &hole)
{
    const QPointF at = toMm(hole.at);
    const double drill = nmToMm(hole.drill);
    if (hole.plated) {
        paintRing(painter, at, drill + 2 * nmToMm(hole.ring), drill);
        return;
    }
    painter.setPen(linePen(paintcolor::bareHole(), OUTLINE_WIDTH));
    painter.setBrush(paintcolor::background());
    painter.drawEllipse(at, drill / 2, drill / 2);
}

QColor silkscreenColor(BoardSide side)
{
    return side == BoardSide::Top ? paintcolor::silkscreen() : paintcolor::silkscreenBottom();
}

void paintPlacementPads(QPainter &painter, const Library &library,
                        const Placement &placement)
{
    const Footprint *footprint = library.findFootprint(placement.footprint);
    if (footprint == nullptr) {
        paintMissing(painter, toMm(placement.at), MISSING_FOOTPRINT_SIZE);
        return;
    }
    painter.save();
    painter.setTransform(placementTransform(placement), true);
    for (const Pad &pad : footprint->pads)
        paintPad(painter, pad, placement.side, true);
    painter.restore();
}

// A text of the placement's silkscreen at 'at' on the board; mirrored on
// the bottom side.
void paintPlacementText(QPainter &painter, const Placement &placement, const Point &at,
                        const QString &text)
{
    painter.save();
    painter.translate(toMm(at));
    if (placement.side == BoardSide::Bottom)
        painter.scale(-1, 1);
    drawText(painter, QPointF(0, 0), text, PLACEHOLDER_TEXT_HEIGHT, TextAlign::Center,
             silkscreenColor(placement.side));
    painter.restore();
}

void paintPlacementSilkscreen(QPainter &painter, const Library &library,
                              const Placement &placement)
{
    const Footprint *footprint = library.findFootprint(placement.footprint);
    if (footprint != nullptr) {
        painter.save();
        painter.setTransform(placementTransform(placement), true);
        for (const Graphic &graphic : footprint->graphics)
            paintGraphic(painter, graphic, silkscreenColor(placement.side));
        painter.restore();
    }

    const Point refAt = placement.at + placement.refAt;
    const Point valueAt = placement.at + placement.valueAt;
    if (placement.showRef)
        paintPlacementText(painter, placement, refAt, placement.designator);
    if (placement.showValue && !placement.value.isEmpty())
        paintPlacementText(painter, placement, valueAt, placement.value);
}

// The author's drawing on the silkscreen; mirrored on the bottom side.
void paintBoardGraphic(QPainter &painter, const BoardGraphic &item)
{
    if (item.side == BoardSide::Top || item.graphic.kind != GraphicKind::Text) {
        paintGraphic(painter, item.graphic, silkscreenColor(item.side));
        return;
    }
    // Only a text shows which way it is read; it is mirrored about its anchor.
    Graphic text = item.graphic;
    text.a = Point();
    painter.save();
    painter.translate(toMm(item.graphic.a));
    painter.scale(-1, 1);
    paintGraphic(painter, text, silkscreenColor(item.side));
    painter.restore();
}

QRectF squareAround(const QPointF &center, double size)
{
    return circleRect(center, size / 2);
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

Point powerStemEnd(const PowerSymbol &power)
{
    // Not turned, the stem points up; a quarter turn clockwise on screen,
    // where Y grows downwards, as QPainter::rotate() turns it.
    Point stem{0, -mmToNm(POWER_STEM)};
    for (int turn = 0; turn < power.rotation / 90; ++turn)
        stem = Point{-stem.y, stem.x};
    return power.at + stem;
}

QRectF symbolBounds(const Symbol &symbol)
{
    Bounds bounds;
    addCommonBounds(bounds, symbol.refAt, symbol.valueAt);
    for (const Graphic &graphic : symbol.graphics)
        bounds.add(graphicBounds(graphic, SYMBOL_LINE_WIDTH));
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

QRectF symbolBodyBounds(const Symbol &symbol)
{
    Bounds body;
    for (const Graphic &graphic : symbol.graphics)
        body.add(graphicBounds(graphic, SYMBOL_LINE_WIDTH));
    return body.rect();
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
    paintSymbolBody(painter, symbol, false, true);
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

QRectF paperRect(PaperSize paper)
{
    if (paper == PaperSize::A3)
        return QRectF(0, 0, A3_LONG_SIDE, A3_SHORT_SIDE);
    return QRectF(0, 0, A4_LONG_SIDE, A4_SHORT_SIDE);
}

void paintSheet(QPainter &painter, const Schematic &schematic, const Library &library,
                int index, const TitleBlock &titleBlock)
{
    if (index < 0 || index >= schematic.sheets.size())
        return;
    const Sheet &sheet = schematic.sheets.at(index);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintFrame(painter, schematic, index, titleBlock);
    for (const SheetGraphic &item : sheet.graphics)
        paintGraphic(painter, item.graphic, paintcolor::sheetGraphic());

    painter.setBrush(Qt::NoBrush);
    for (const Bus &bus : sheet.buses) {
        painter.setPen(linePen(paintcolor::bus(), BUS_WIDTH));
        painter.drawPolyline(toPolygon(bus.points));
        // The name sits above the bus's first point.
        const QPointF nameAt = toMm(bus.points.value(0))
                               - QPointF(0, LABEL_TEXT_HEIGHT / 2 + BUS_WIDTH + LABEL_GAP);
        drawText(painter, nameAt, bus.name, LABEL_TEXT_HEIGHT, TextAlign::Left, paintcolor::bus());
    }
    for (const Wire &wire : sheet.wires) {
        painter.setPen(linePen(paintcolor::wire(), WIRE_WIDTH));
        painter.drawPolyline(toPolygon(wire.points));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(paintcolor::wire());
    for (const Point &dot : junctions(sheet, library))
        painter.drawEllipse(toMm(dot), JUNCTION_DIAMETER / 2, JUNCTION_DIAMETER / 2);
    painter.setBrush(Qt::NoBrush);
    for (const Component &component : sheet.components)
        paintComponent(painter, library, component);
    for (const PowerSymbol &power : sheet.powerSymbols)
        paintPowerSymbol(painter, power);
    for (const Label &label : sheet.labels)
        paintLabel(painter, label);
    for (const NoConnect &mark : sheet.noConnects)
        paintNoConnect(painter, mark);
    painter.restore();
}

QList<SheetObjectShape> sheetObjectShapes(const Sheet &sheet, const Library &library)
{
    QList<SheetObjectShape> shapes;
    const auto addRect = [&shapes](const ObjectId &id, const QRectF &rect) {
        shapes.append(SheetObjectShape{id, rect, QPolygonF()});
    };
    for (const SheetGraphic &item : sheet.graphics) {
        shapes.append(SheetObjectShape{item.id, graphicBounds(item.graphic),
                                       graphicLine(item.graphic)});
    }
    for (const Bus &bus : sheet.buses)
        shapes.append(SheetObjectShape{bus.id, toPolygon(bus.points).boundingRect(),
                                       toPolygon(bus.points)});
    for (const Wire &wire : sheet.wires)
        shapes.append(SheetObjectShape{wire.id, toPolygon(wire.points).boundingRect(),
                                       toPolygon(wire.points)});
    for (const Component &component : sheet.components) {
        Bounds bounds;
        const Symbol *symbol = library.findSymbolOfPart(component.part);
        if (symbol == nullptr) {
            bounds.add(missingRect(toMm(component.at), MISSING_SYMBOL_SIZE));
        } else {
            // The drawing without the room symbolBounds() keeps for pin texts.
            Bounds body;
            if (!symbol->graphics.isEmpty())
                body.add(symbolBodyBounds(*symbol));
            for (const Pin &pin : symbol->pins)
                body.add(QRectF(toMm(pin.at), toMm(pinBodyEnd(pin))).normalized());
            if (!body.isEmpty())
                bounds.add(componentTransform(component).mapRect(body.rect()));
            else
                bounds.add(missingRect(toMm(component.at), MISSING_SYMBOL_SIZE));
        }
        bounds.add(componentTextRect(component, ComponentText::Designator, library));
        if (!componentTextShown(component, ComponentText::Value, library).isEmpty())
            bounds.add(componentTextRect(component, ComponentText::Value, library));
        addRect(component.id, bounds.rect());
    }
    for (const PowerSymbol &power : sheet.powerSymbols) {
        // Drawn pointing up, then turned about its point.
        Bounds bounds;
        for (const QLineF &line : powerLines(power.kind))
            bounds.add(QRectF(line.p1(), line.p2()).normalized());
        const QString name = powerNameShown(power);
        if (!name.isEmpty()) {
            bounds.add(textBounds(powerNameAt(power.kind), name, LABEL_TEXT_HEIGHT,
                                  TextAlign::Center));
        }
        QTransform transform;
        transform.translate(nmToMm(power.at.x), nmToMm(power.at.y));
        transform.rotate(power.rotation);
        addRect(power.id, transform.mapRect(bounds.rect()));
    }
    for (const Label &label : sheet.labels) {
        QTransform transform;
        transform.translate(nmToMm(label.at.x), nmToMm(label.at.y));
        transform.rotate(label.rotation);
        const QRectF text = textBounds(QPointF(0, -(LABEL_TEXT_HEIGHT / 2 + LABEL_GAP)),
                                       label.text, LABEL_TEXT_HEIGHT, TextAlign::Left);
        // The label's point belongs to it too.
        addRect(label.id, transform.mapRect(text.united(QRectF(0, -LABEL_GAP, 0, LABEL_GAP))));
    }
    for (const NoConnect &mark : sheet.noConnects)
        addRect(mark.id, squareAround(toMm(mark.at), 2 * NO_CONNECT_HALF));
    return shapes;
}

QString componentTextShown(const Component &component, ComponentText text,
                           const Library &library)
{
    if (text == ComponentText::Designator)
        return component.designator;
    if (!component.value.isEmpty())
        return component.value;
    const Part *part = library.findPart(component.part);
    return part != nullptr && part->noValue ? QString() : QLatin1String(VALUE_PLACEHOLDER);
}

Point componentTextPoint(const Component &component, ComponentText text)
{
    return component.at
           + (text == ComponentText::Designator ? component.refAt : component.valueAt);
}

QRectF componentTextRect(const Component &component, ComponentText text,
                         const Library &library)
{
    return textBounds(toMm(componentTextPoint(component, text)),
                      componentTextShown(component, text, library), PLACEHOLDER_TEXT_HEIGHT,
                      TextAlign::Center);
}

QRectF boardBounds(const Board &board, const Library &library)
{
    Bounds bounds;
    for (const OutlineCorner &corner : board.outline)
        bounds.add(QRectF(toMm(corner.at), QSizeF(0, 0)));
    for (const Placement &placement : board.placements) {
        const Footprint *footprint = library.findFootprint(placement.footprint);
        if (footprint == nullptr)
            bounds.add(missingRect(toMm(placement.at), MISSING_FOOTPRINT_SIZE));
        else
            bounds.add(placementTransform(placement).mapRect(footprintBounds(*footprint)));
    }
    for (const Track &track : board.tracks) {
        const double half = nmToMm(track.width) / 2;
        bounds.add(QRectF(toMm(track.from), toMm(track.to))
                       .normalized()
                       .adjusted(-half, -half, half, half));
    }
    for (const Via &via : board.vias)
        bounds.add(squareAround(toMm(via.at), nmToMm(via.size)));
    for (const MountingHole &hole : board.holes)
        bounds.add(squareAround(toMm(hole.at), nmToMm(hole.drill) + 2 * nmToMm(hole.ring)));
    for (const BoardGraphic &item : board.graphics) {
        // A text on the bottom is mirrored about its anchor: room on both sides.
        const QRectF rect = graphicBounds(item.graphic);
        const double x = nmToMm(item.graphic.a.x);
        bounds.add(rect);
        bounds.add(QRectF(2 * x - rect.right(), rect.top(), rect.width(), rect.height()));
    }
    for (const Zone &zone : board.zones)
        bounds.add(toPolygon(zone.points).boundingRect());

    if (bounds.isEmpty())
        return QRectF(0, 0, EMPTY_BOARD_WIDTH, EMPTY_BOARD_HEIGHT);
    return bounds.rect();
}

void paintBoard(QPainter &painter, const Board &board, const Library &library)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Zones are only outlined: nothing fills them yet.
    painter.setBrush(Qt::NoBrush);
    for (const Zone &zone : board.zones) {
        painter.setPen(dashedPen(copperColor(zone.side), ZONE_LINE_WIDTH));
        painter.drawPolygon(toPolygon(zone.points));
    }

    paintTracks(painter, board, BoardSide::Bottom);
    paintTracks(painter, board, BoardSide::Top);
    for (const Placement &placement : board.placements)
        paintPlacementPads(painter, library, placement);
    for (const Via &via : board.vias)
        paintRing(painter, toMm(via.at), nmToMm(via.size), nmToMm(via.drill));
    for (const MountingHole &hole : board.holes)
        paintMountingHole(painter, hole);

    for (const Placement &placement : board.placements)
        paintPlacementSilkscreen(painter, library, placement);
    for (const BoardGraphic &item : board.graphics)
        paintBoardGraphic(painter, item);

    painter.setPen(linePen(paintcolor::boardOutline(), OUTLINE_WIDTH));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outlinePath(board.outline));
    painter.restore();
}

} // namespace ppcb
