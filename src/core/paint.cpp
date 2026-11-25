#include "paint.h"

#include "boardcopper.h"
#include "strokefont.h"
#include "wiring.h"

#include <QCoreApplication>
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
const double SYMBOL_LINE_WIDTH = 0.4;
const double PIN_LINE_WIDTH = SYMBOL_LINE_WIDTH;
const double PIN_TEXT_HEIGHT = 1.0;
const double PIN_TEXT_GAP = 0.4;
const double PIN_POINT_RADIUS = 0.3;
const double PLACEHOLDER_TEXT_HEIGHT = 1.27;
// The line from a placement's text to the part on the silkscreen, in
// millimetres: bolder than the drawings of the footprints.
const double LEADER_LINE_WIDTH = 0.3;
const double ORIGIN_MARK_SIZE = 0.8;
// The pad number takes this share of the pad's smaller side.
const double PAD_TEXT_SHARE = 0.45;
// The lines of the letters are this share of the height of capitals wide.
const double STROKE_WIDTH_SHARE = 0.12;
// How far letters such as 'y' reach under the baseline, as a share of the
// height of capitals.
const double DESCENT_SHARE = 1.0 / 3;
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
// The frame inside the paper's edge: wider on the left, room for binding.
const double FRAME_MARGIN = 5.0;
const double FRAME_BINDING_MARGIN = 20.0;
const double FRAME_LINE_WIDTH = 0.3;
const double TITLE_BLOCK_WIDTH = 90.0;
const double TITLE_ROW_HEIGHT = 6.0;
const double TITLE_TEXT_HEIGHT = 2.5;
const double TITLE_TEXT_MARGIN = 2.0;
const double WIRE_WIDTH = SYMBOL_LINE_WIDTH;
const double BUS_WIDTH = 1.0;
// The dot where wires join.
const double JUNCTION_DIAMETER = 1.2;
// Where buses meet: larger, as a bus is wider than a wire.
const double BUS_JUNCTION_DIAMETER = 2.0;
const double LABEL_TEXT_HEIGHT = 1.27;
const double LABEL_GAP = 0.4;
// The hollow diamond on a label's point, which it is taken by: from its
// centre to each corner, and the width of its line.
const double LABEL_POINT_HALF = 0.4;
const double LABEL_POINT_LINE = 0.12;
// A label is taken by its point within this square, besides its text.
const double LABEL_POINT_GRIP = 1.0;
// How far the red plate under a label that joins nothing reaches past its text.
const double LOOSE_LABEL_MARGIN = 0.3;
// Stands for the text of a label that has none yet, while it is placed.
const char EMPTY_LABEL_MARK[] = "?";
const double NO_CONNECT_HALF = 0.635;
// The ring around a problem of the rules check: its radius and line.
const double ERC_MARK_RADIUS = 1.5;
const double ERC_MARK_LINE_WIDTH = 0.3;

// The board.
const double OUTLINE_WIDTH = 0.15;
// How much darker the copper of the side not chosen is (QColor::darker()).
const int DIMMED_COPPER = 250;
// The arms of the cross in the middle of a mounting hole, as a part of its
// radius.
const double HOLE_CROSS_PART = 0.6;
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
const double QUARTER_TURN = 90.0;
// The mark of a rated power inside a resistor: how far its strokes reach
// from the middle of the body across it and along it, how far apart they
// stand, and the height of its Roman numerals.
const double POWER_STROKE_HALF = 0.6;
// Slanted strokes lean at 45 degrees, unlike the upright ones of 1 and 2 W.
const double POWER_SLANT_HALF = POWER_STROKE_HALF;
const double POWER_LONG_HALF = 1.5;
const double POWER_STROKE_GAP = 0.6;
const double POWER_TEXT_HEIGHT = 1.0;
const int MILLIWATTS_PER_WATT = 1000;

// What stands in for a symbol or footprint the project library lacks.
const double MISSING_SYMBOL_SIZE = 5.08;
const double MISSING_FOOTPRINT_SIZE = 3.0;
const double MISSING_LINE_WIDTH = 0.2;
// The question mark takes this share of the square's side.
const double MISSING_TEXT_SHARE = 0.6;

const char REF_PLACEHOLDER[] = "REF";
const char VALUE_PLACEHOLDER[] = "VALUE";

// How many MonochromeInk live on this thread: while any does, inked()
// makes every colour black.
thread_local int monochromeDepth = 0;

// 'color' as it is drawn: black under MonochromeInk, all but the
// background's colour, which stays for what is drawn over black.
QColor inked(const QColor &color)
{
    if (monochromeDepth == 0 || color == paintcolor::background())
        return color;
    return QColor(Qt::black);
}

QPen linePen(const QColor &color, double width)
{
    QPen pen(inked(color), width);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    return pen;
}

// Text in the stroke font whose capitals are 'height' mm tall, vertically
// centred on 'at'. 'upright' keeps it readable inside a turned or mirrored
// drawing: it runs left to right or bottom to top and is never mirrored;
// the anchor stays.
void drawText(QPainter &painter, const QPointF &at, const QString &text, double height,
              TextAlign align, const QColor &color, bool upright = false)
{
    const StrokeText laid = strokeText(text);
    const double width = laid.width * height;

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

    // The baseline half the height of capitals under the anchor.
    QTransform place;
    place.translate(left, height / 2);
    place.scale(height, height);
    painter.setPen(linePen(color, height * STROKE_WIDTH_SHARE));
    painter.setBrush(Qt::NoBrush);
    for (const QPolygonF &stroke : laid.strokes)
        painter.drawPolyline(place.map(stroke));
    painter.restore();
}

// A rectangle holding a text drawn by drawText(), not turned: from the top
// of its capitals to the bottom of letters such as 'y', the width of its
// letters, and the width of their lines round that. For no text, one of
// no size at 'at', which holds no point.
QRectF textBounds(const QPointF &at, const QString &text, double height, TextAlign align)
{
    if (text.isEmpty())
        return QRectF(at, QSizeF());
    const double width = strokeText(text).width * height;
    double left = at.x();
    if (align == TextAlign::Center)
        left -= width / 2;
    else if (align == TextAlign::Right)
        left -= width;
    const double margin = height * STROKE_WIDTH_SHARE;
    return QRectF(left - margin, at.y() - height / 2 - margin, width + 2 * margin,
                  height * (1 + DESCENT_SHARE) + 2 * margin);
}

// Dashes of the same length whatever the line width. Their ends are cut
// flat: round ends would eat into the gaps.
QPen dashedPen(const QColor &color, double width)
{
    QPen pen(inked(color), width);
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
            painter.setBrush(inked(color));
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
    painter.setPen(QPen(inked(paintcolor::placeholder()), 0));
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
    return boardLayerColor(copperLayer(side));
}

BoardSide otherSide(BoardSide side)
{
    return side == BoardSide::Top ? BoardSide::Bottom : BoardSide::Top;
}

// 'partSide' is the side the part is mounted on: a pad's Front is that side.
// The copper of 'side', darker when another side is the one shown bright.
QColor shownCopper(BoardSide side, std::optional<BoardSide> bright)
{
    const QColor color = copperColor(side);
    return bright && *bright != side ? color.darker(DIMMED_COPPER) : color;
}

QColor padColor(PadLayers layers, BoardSide partSide, std::optional<BoardSide> bright = std::nullopt)
{
    switch (layers) {
    case PadLayers::Both:
        return paintcolor::padBoth();
    case PadLayers::Front:
        return shownCopper(partSide, bright);
    case PadLayers::Back:
        return shownCopper(otherSide(partSide), bright);
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

// What shows through a hole, and the colour of a pad's number over it.
struct HoleColors
{
    QColor hole;
    QColor text;
};

// A footprint alone on the paper, and one on the board.
HoleColors paperHoles()
{
    return HoleColors{paintcolor::background(), paintcolor::symbol()};
}

HoleColors boardHoles()
{
    return HoleColors{paintcolor::boardBackground(), paintcolor::padText()};
}

// True when a pad on 'layers' of a part on 'partSide' has a copper layer
// among 'shown'.
bool padShown(PadLayers layers, BoardSide partSide, const BoardLayerSet &shown)
{
    const bool front = shown.contains(copperLayer(partSide));
    const bool back = shown.contains(copperLayer(otherSide(partSide)));
    switch (layers) {
    case PadLayers::Both:
        return front || back;
    case PadLayers::Front:
        return front;
    case PadLayers::Back:
        return back;
    }
    return true;
}

void paintPad(QPainter &painter, const Pad &pad, const HoleColors &holes,
              BoardSide partSide = BoardSide::Top, bool upright = false,
              std::optional<BoardSide> bright = std::nullopt)
{
    const QRectF rect = padRect(pad);
    const double smaller = std::min(rect.width(), rect.height());

    painter.setPen(Qt::NoPen);
    painter.setBrush(padColor(pad.layers, partSide, bright));
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
        painter.setBrush(holes.hole);
        painter.drawEllipse(rect.center(), nmToMm(pad.hole) / 2, nmToMm(pad.hole) / 2);
    }
    if (!pad.number.isEmpty()) {
        drawText(painter, rect.center(), pad.number, smaller * PAD_TEXT_SHARE, TextAlign::Center,
                 pad.hole > 0 ? holes.text : paintcolor::padText(), upright);
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

// The mark of a resistor's rated power in the middle of its body, the first
// rectangle of its symbol, after GOST 2.728: three, two or one slanted
// strokes for 0.05, 0.125 and 0.25 W, a stroke along the body for 0.5 W,
// one or two across it for 1 and 2 W, Roman numerals for 5 W and more in
// whole watts. Nothing for any other power, or without a rectangle.
static void paintPowerMark(QPainter &painter, const Symbol &symbol, int powerMw)
{
    const auto body = std::find_if(symbol.graphics.cbegin(), symbol.graphics.cend(),
                                   [](const Graphic &g) { return g.kind == GraphicKind::Rect; });
    if (body == symbol.graphics.cend())
        return;
    const QRectF rect = QRectF(toMm(body->a), toMm(body->b)).normalized();
    painter.save();
    painter.translate(rect.center());
    // Along the body is along its longer side.
    if (rect.height() > rect.width())
        painter.rotate(QUARTER_TURN);
    painter.setPen(linePen(paintcolor::symbol(), SYMBOL_LINE_WIDTH));
    painter.setBrush(Qt::NoBrush);
    // 'count' strokes side by side along the body, each drawn by 'stroke'
    // about its own middle.
    const auto strokes = [&painter](int count, const QLineF &stroke) {
        for (int i = 0; i < count; ++i)
            painter.drawLine(
                stroke.translated((i - (count - 1) / 2.0) * POWER_STROKE_GAP, 0));
    };
    const QLineF slanted(-POWER_SLANT_HALF, POWER_STROKE_HALF, POWER_SLANT_HALF,
                         -POWER_STROKE_HALF);
    const QLineF across(0, -POWER_STROKE_HALF, 0, POWER_STROKE_HALF);
    switch (powerMw) {
    case 50:
        strokes(3, slanted);
        break;
    case 125:
        strokes(2, slanted);
        break;
    case 250:
        strokes(1, slanted);
        break;
    case 500:
        painter.drawLine(QLineF(-POWER_LONG_HALF, 0, POWER_LONG_HALF, 0));
        break;
    case 1000:
        strokes(1, across);
        break;
    case 2000:
        strokes(2, across);
        break;
    default:
        if (powerMw >= 5 * MILLIWATTS_PER_WATT && powerMw % MILLIWATTS_PER_WATT == 0)
            drawText(painter, QPointF(), romanNumeral(powerMw / MILLIWATTS_PER_WATT),
                     POWER_TEXT_HEIGHT, TextAlign::Center, paintcolor::symbol(), true);
        break;
    }
    painter.restore();
}

void paintComponent(QPainter &painter, const Library &library, const Component &component,
                    const PowerShown &power)
{
    const Symbol *symbol = library.findSymbolOfPart(component.part);
    if (symbol == nullptr) {
        paintMissing(painter, toMm(component.at), MISSING_SYMBOL_SIZE);
    } else {
        painter.save();
        painter.setTransform(componentTransform(component), true);
        paintSymbolBody(painter, *symbol, true, false);
        if (power.marks && component.powerMw > 0)
            paintPowerMark(painter, *symbol, component.powerMw);
        painter.restore();
    }

    drawText(painter, toMm(componentTextPoint(component, ComponentText::Designator)),
             component.designator, PLACEHOLDER_TEXT_HEIGHT, TextAlign::Center,
             paintcolor::symbol());
    drawText(painter, toMm(componentTextPoint(component, ComponentText::Value)),
             componentTextShown(component, ComponentText::Value, library, power),
             PLACEHOLDER_TEXT_HEIGHT, TextAlign::Center,
             component.value.isEmpty() ? paintcolor::placeholder() : paintcolor::symbol());
}

// Where a label's text stands, not turned: just above its point.
QPointF labelTextAt()
{
    return QPointF(0, -(LABEL_TEXT_HEIGHT / 2 + LABEL_GAP));
}

// A diamond on its point and the text along the label's direction, just
// above where it stands; the text is white on a red plate, and the diamond
// red, when the label joins nothing, a grey question mark while it has no
// text yet.
void paintLabel(QPainter &painter, const Label &label, bool joined)
{
    const bool empty = label.text.isEmpty();
    const QColor mark = joined || empty ? paintcolor::label() : paintcolor::looseLabel();
    painter.save();
    // Hollow, so that it is not taken for the dot where wires join.
    const QPointF at = toMm(label.at);
    const double h = LABEL_POINT_HALF;
    painter.setPen(linePen(mark, LABEL_POINT_LINE));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(QPolygonF({at + QPointF(0, -h), at + QPointF(h, 0), at + QPointF(0, h),
                                   at + QPointF(-h, 0)}));
    painter.restore();

    painter.save();
    painter.translate(toMm(labelTextPoint(label)));
    painter.rotate(label.rotation);
    const QString text = empty ? QString::fromLatin1(EMPTY_LABEL_MARK) : label.text;
    QColor color = empty ? paintcolor::placeholder() : paintcolor::label();
    if (!empty && !joined) {
        // Upright text of a turned label covers the same place, so the
        // plate goes under it whichever way the label is turned.
        const QRectF plate = textBounds(labelTextAt(), text, LABEL_TEXT_HEIGHT, TextAlign::Left);
        painter.fillRect(plate.adjusted(-LOOSE_LABEL_MARGIN, -LOOSE_LABEL_MARGIN,
                                        LOOSE_LABEL_MARGIN, LOOSE_LABEL_MARGIN),
                         inked(paintcolor::looseLabel()));
        color = paintcolor::background();
    }
    drawText(painter, labelTextAt(), text, LABEL_TEXT_HEIGHT, TextAlign::Left, color, true);
    painter.restore();
}

// Where the name of a bus is drawn the 'index'th time: just above where it
// stands.
QPointF busNameAt(const Bus &bus, int index)
{
    return toMm(busNamePoint(bus, index))
           - QPointF(0, LABEL_TEXT_HEIGHT / 2 + BUS_WIDTH + LABEL_GAP);
}

// From the coordinates of a power symbol's drawing to those of the sheet:
// turned clockwise about its pin, then moved.
QTransform powerTransform(const PowerSymbol &power)
{
    QTransform transform;
    transform.translate(nmToMm(power.at.x), nmToMm(power.at.y));
    transform.rotate(power.rotation);
    return transform;
}

// The net name a power symbol shows: none when its symbol hides the name
// it is placed with and the name is still that one.
QString powerNameShown(const PowerSymbol &power, const Symbol &symbol)
{
    if (symbol.hidePowerName && power.name == symbol.power)
        return QString();
    return power.name;
}

// Where the net name stands on the sheet: the symbol's 'valueAt', turned
// with it; the text itself stays level.
QPointF powerNameAt(const PowerSymbol &power, const Symbol &symbol)
{
    return powerTransform(power).map(toMm(symbol.valueAt));
}

// The drawing of its symbol, turned, and the net name; a red square where
// the project library lacks the symbol.
void paintPowerSymbol(QPainter &painter, const Library &library, const PowerSymbol &power)
{
    const Symbol *symbol = library.findSymbol(power.symbol);
    if (symbol == nullptr) {
        paintMissing(painter, toMm(power.at), MISSING_SYMBOL_SIZE);
        return;
    }
    painter.save();
    painter.setTransform(powerTransform(power), true);
    paintSymbolBody(painter, *symbol, true, false);
    painter.restore();
    drawText(painter, powerNameAt(power, *symbol), powerNameShown(power, *symbol),
             LABEL_TEXT_HEIGHT, TextAlign::Center, paintcolor::symbol());
}

// A cross; red when it stands on no pin of a part.
void paintNoConnect(QPainter &painter, const NoConnect &mark, bool onPin)
{
    const QPointF at = toMm(mark.at);
    const double half = NO_CONNECT_HALF;
    painter.setPen(linePen(onPin ? paintcolor::noConnect() : paintcolor::looseNoConnect(),
                           WIRE_WIDTH));
    painter.drawLine(at + QPointF(-half, -half), at + QPointF(half, half));
    painter.drawLine(at + QPointF(-half, half), at + QPointF(half, -half));
}

// The frame inside the paper's edge and the title block in its bottom right
// corner: one row for every field that is not empty.
void paintFrame(QPainter &painter, const Schematic &schematic, int index,
                const TitleBlock &titleBlock)
{
    const Sheet &sheet = schematic.sheets.at(index);
    const QRectF paper = paperRect(sheet);
    const QRectF frame = paper.adjusted(FRAME_BINDING_MARGIN, FRAME_MARGIN, -FRAME_MARGIN, -FRAME_MARGIN);

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
    painter.setPen(QPen(inked(paintcolor::sheetFrame()), 0));
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

} // namespace

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

namespace {

void paintTracks(QPainter &painter, const Board &board, BoardSide side,
                 const BoardLayerSet &shown, std::optional<BoardSide> bright)
{
    if (!shown.contains(copperLayer(side)))
        return;
    for (const Track &track : board.tracks) {
        if (track.side != side)
            continue;
        painter.setPen(linePen(shownCopper(side, bright), nmToMm(track.width)));
        painter.drawLine(toMm(track.from), toMm(track.to));
    }
}

// A filled circle of copper of 'color' with a hole in it; only the hole
// when 'copper' is false.
void paintRing(QPainter &painter, const QPointF &at, double copperDiameter, double holeDiameter,
               const QColor &color, bool copper)
{
    painter.setPen(Qt::NoPen);
    if (copper) {
        painter.setBrush(color);
        painter.drawEllipse(at, copperDiameter / 2, copperDiameter / 2);
    }
    painter.setBrush(paintcolor::boardBackground());
    painter.drawEllipse(at, holeDiameter / 2, holeDiameter / 2);
}

bool throughShown(const BoardLayerSet &shown)
{
    return shown.contains(BoardLayer::CopperTop) || shown.contains(BoardLayer::CopperBottom);
}

void paintMountingHole(QPainter &painter, const Board &board, const MountingHole &hole,
                       const BoardLayerSet &shown, bool lockedMarked,
                       std::optional<BoardSide> bright)
{
    const QPointF at = toMm(hole.at);
    const double drill = nmToMm(hole.drill);
    const bool marked = lockedMarked && hole.locked;
    const QColor edge = marked ? paintcolor::lockedPart() : paintcolor::bareHole();
    if (hole.ring > 0) {
        // The pad in the colour of its copper: of one side, or through.
        const bool top = holePadOn(board, hole, BoardSide::Top);
        const bool bottom = holePadOn(board, hole, BoardSide::Bottom);
        const QColor copper =
            top && bottom ? paintcolor::padBoth()
                          : shownCopper(top ? BoardSide::Top : BoardSide::Bottom, bright);
        const bool copperShown = (top && shown.contains(BoardLayer::CopperTop))
                                 || (bottom && shown.contains(BoardLayer::CopperBottom));
        paintRing(painter, at, nmToMm(holeOuterDiameter(hole)), drill,
                  marked ? paintcolor::lockedPart() : copper, copperShown);
        // The plating, a line round the inside of the pad, with the copper.
        if (hole.plated && copperShown) {
            painter.setPen(linePen(paintcolor::platedWall(), OUTLINE_WIDTH));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(at, drill / 2, drill / 2);
        }
    } else {
        // A bare hole: its edge, as it would not show on black.
        painter.setPen(linePen(edge, OUTLINE_WIDTH));
        painter.setBrush(paintcolor::boardBackground());
        painter.drawEllipse(at, drill / 2, drill / 2);
    }
    // A cross marks the middle, where the drill goes.
    const double arm = drill / 2 * HOLE_CROSS_PART;
    painter.setPen(linePen(edge, OUTLINE_WIDTH));
    painter.drawLine(at - QPointF(arm, 0), at + QPointF(arm, 0));
    painter.drawLine(at - QPointF(0, arm), at + QPointF(0, arm));
}

QColor silkscreenColor(BoardSide side)
{
    return boardLayerColor(silkscreenLayer(side));
}

void paintPlacementPads(QPainter &painter, const Library &library, const Placement &placement,
                        const BoardLayerSet &shown, std::optional<BoardSide> bright)
{
    const Footprint *footprint = library.findFootprint(placement.footprint);
    if (footprint == nullptr) {
        paintMissing(painter, toMm(placement.at), MISSING_FOOTPRINT_SIZE);
        return;
    }
    painter.save();
    painter.setTransform(placementTransform(placement), true);
    for (const Pad &pad : footprint->pads) {
        if (padShown(pad.layers, placement.side, shown))
            paintPad(painter, pad, boardHoles(), placement.side, true, bright);
    }
    painter.restore();
}

// A text of the placement's silkscreen at 'at' on the board; mirrored on
// the bottom side.
void paintPlacementText(QPainter &painter, const Placement &placement, const Point &at,
                        const QString &text, const QColor &color)
{
    painter.save();
    painter.translate(toMm(at));
    if (placement.side == BoardSide::Bottom)
        painter.scale(-1, 1);
    drawText(painter, QPointF(0, 0), text, PLACEHOLDER_TEXT_HEIGHT, TextAlign::Center, color);
    painter.restore();
}

// The placement's silkscreen in 'color'.
void paintPlacementSilkscreen(QPainter &painter, const Library &library,
                              const Placement &placement, const QColor &color)
{
    const Footprint *footprint = library.findFootprint(placement.footprint);
    if (footprint != nullptr) {
        painter.save();
        painter.setTransform(placementTransform(placement), true);
        for (const Graphic &graphic : footprint->graphics)
            paintGraphic(painter, graphic, color);
        painter.restore();
    }

    for (const ComponentText text : {ComponentText::Designator, ComponentText::Value}) {
        const QString shown = placementTextShown(placement, text);
        if (!shown.isEmpty())
            paintPlacementText(painter, placement, placementTextPoint(placement, text), shown,
                               color);
        const QLineF leader = leaderLine(placement, text);
        if (!leader.isNull()) {
            painter.setPen(linePen(color, LEADER_LINE_WIDTH));
            painter.drawLine(leader);
        }
    }
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

// The silkscreen of 'side': the parts on it, the author's drawing, and on
// the top, the side of the parts, the wires of wire links with their
// designators. With 'lockedMarked', see paintBoard().
void paintSilkscreen(QPainter &painter, const Board &board, const Library &library,
                     BoardSide side, bool lockedMarked)
{
    for (const Placement &placement : board.placements) {
        if (placement.side == side)
            paintPlacementSilkscreen(painter, library, placement,
                                     lockedMarked && placement.locked
                                         ? paintcolor::lockedPart()
                                         : silkscreenColor(side));
    }
    for (const BoardGraphic &item : board.graphics) {
        if (item.side == side)
            paintBoardGraphic(painter, item);
    }
    if (side != BoardSide::Top)
        return;
    // An insulated one dashed.
    for (const WireLink &link : board.links) {
        painter.setPen(QPen(inked(paintcolor::silkscreenTop()), nmToMm(WIRE_LINK_WIRE),
                            link.insulated ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(toMm(link.from), toMm(link.to));
    }
    for (const WireLink &link : board.links)
        drawText(painter, toMm(linkTextPoint(link)), link.designator, PLACEHOLDER_TEXT_HEIGHT,
                 TextAlign::Center, paintcolor::silkscreenTop());
}

QRectF squareAround(const QPointF &center, double size)
{
    return circleRect(center, size / 2);
}

// The room between a wire link and its designator.
const double LINK_TEXT_GAP = 0.3;

// Where the designator of 'link' is centred unless moved: beside the
// middle of its wire, above a level or slanted one, right of an upright
// one, clear of its pads.
QPointF linkTextPlace(const WireLink &link)
{
    const QPointF from = toMm(link.from);
    const QPointF to = toMm(link.to);
    const double length = QLineF(from, to).length();
    // The side away from the wire: up the sheet, or right.
    QPointF away((from.y() - to.y()) / length, (to.x() - from.x()) / length);
    if (away.y() > 0 || (qFuzzyIsNull(away.y()) && away.x() < 0))
        away = -away;
    const QRectF text =
        textBounds(QPointF(0, 0), link.designator, PLACEHOLDER_TEXT_HEIGHT, TextAlign::Center);
    // How far the text reaches towards the wire from its middle.
    const double reach =
        std::abs(away.x()) * text.width() / 2 + std::abs(away.y()) * text.height() / 2;
    const double clear = std::max(nmToMm(link.size), nmToMm(WIRE_LINK_WIRE)) / 2;
    return (from + to) / 2 + away * (clear + LINK_TEXT_GAP + reach);
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
    // A power symbol has no designator; its net name stands for the value.
    if (symbol.isPower()) {
        drawText(painter, toMm(symbol.valueAt), symbol.power, LABEL_TEXT_HEIGHT,
                 TextAlign::Center, paintcolor::placeholder());
    } else {
        paintPlaceholders(painter, symbol.refAt, symbol.valueAt);
    }
    painter.restore();
}

void paintFootprint(QPainter &painter, const Footprint &footprint)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Copper first, silkscreen over it, as on a real board.
    for (const Pad &pad : footprint.pads)
        paintPad(painter, pad, paperHoles());
    for (const Graphic &graphic : footprint.graphics)
        paintGraphic(painter, graphic, paintcolor::silkscreen());
    paintOriginMark(painter);
    paintPlaceholders(painter, footprint.refAt, footprint.valueAt);
    painter.restore();
}

QTransform paperOnPage(const QRectF &paper, const QSizeF &page)
{
    if (paper.isEmpty() || page.isEmpty())
        return QTransform();
    const bool turned = (paper.width() > paper.height()) != (page.width() > page.height())
                        && paper.width() != paper.height() && page.width() != page.height();
    const QSizeF size = turned ? paper.size().transposed() : paper.size();
    const double scale = qMin(page.width() / size.width(), page.height() / size.height());
    QTransform transform;
    // Last applied first: the paper's corner to the origin, turned, sized,
    // then centred.
    transform.translate((page.width() - size.width() * scale) / 2,
                        (page.height() - size.height() * scale) / 2);
    transform.scale(scale, scale);
    if (turned) {
        // A quarter clockwise takes the paper's left edge to the top, its
        // height now along X from the origin.
        transform.translate(size.width(), 0);
        transform.rotate(90);
    }
    transform.translate(-paper.left(), -paper.top());
    return transform;
}

MonochromeInk::MonochromeInk()
{
    ++monochromeDepth;
}

MonochromeInk::~MonochromeInk()
{
    --monochromeDepth;
}

QRectF paperRect(const Sheet &sheet)
{
    double longSide = A4_LONG_SIDE;
    double shortSide = A4_SHORT_SIDE;
    if (sheet.paper == PaperSize::A3) {
        longSide = A3_LONG_SIDE;
        shortSide = A3_SHORT_SIDE;
    }
    if (sheet.portrait)
        return QRectF(0, 0, shortSide, longSide);
    return QRectF(0, 0, longSide, shortSide);
}

void paintSheet(QPainter &painter, const Schematic &schematic, const Library &library,
                int index, const TitleBlock &titleBlock, const PowerShown &power)
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
        for (int i = 0; i < bus.namesAt.size(); ++i)
            drawText(painter, busNameAt(bus, i), bus.name, LABEL_TEXT_HEIGHT, TextAlign::Left,
                     paintcolor::bus());
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(inked(paintcolor::bus()));
    for (const Point &dot : busJunctions(sheet))
        painter.drawEllipse(toMm(dot), BUS_JUNCTION_DIAMETER / 2, BUS_JUNCTION_DIAMETER / 2);
    painter.setBrush(Qt::NoBrush);
    for (const Wire &wire : sheet.wires) {
        painter.setPen(linePen(paintcolor::wire(), WIRE_WIDTH));
        painter.drawPolyline(toPolygon(wire.points));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(inked(paintcolor::junction()));
    for (const Point &dot : junctions(sheet, library))
        painter.drawEllipse(toMm(dot), JUNCTION_DIAMETER / 2, JUNCTION_DIAMETER / 2);
    painter.setBrush(Qt::NoBrush);
    for (const Component &component : sheet.components)
        paintComponent(painter, library, component, power);
    for (const PowerSymbol &power : sheet.powerSymbols)
        paintPowerSymbol(painter, library, power);
    const QList<Point> pins = pinPoints(sheet, library);
    for (const Label &label : sheet.labels) {
        const bool joined =
            pins.contains(label.at)
            || std::any_of(sheet.wires.cbegin(), sheet.wires.cend(),
                           [&label](const Wire &wire) { return isOnWire(wire, label.at); });
        paintLabel(painter, label, joined);
    }
    const QList<Point> partPins = partPinPoints(sheet, library);
    for (const NoConnect &mark : sheet.noConnects)
        paintNoConnect(painter, mark, partPins.contains(mark.at));
    painter.restore();
}

void paintErcMarks(QPainter &painter, const QList<ErcProblem> &problems, int index)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(linePen(paintcolor::ercMark(), ERC_MARK_LINE_WIDTH));
    painter.setBrush(Qt::NoBrush);
    for (const ErcProblem &problem : problems) {
        if (problem.sheet == index)
            painter.drawEllipse(toMm(problem.at), ERC_MARK_RADIUS, ERC_MARK_RADIUS);
    }
    painter.restore();
}

void paintDrcMarks(QPainter &painter, const QList<DrcProblem> &problems)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    for (const bool errors : {false, true}) {
        painter.setPen(linePen(errors ? paintcolor::ercMark() : paintcolor::drcWarning(),
                               ERC_MARK_LINE_WIDTH));
        for (const DrcProblem &problem : problems) {
            if (problem.error == errors)
                painter.drawEllipse(toMm(problem.at), ERC_MARK_RADIUS, ERC_MARK_RADIUS);
        }
    }
    painter.restore();
}

namespace {

// The drawings and the pin lines of a symbol, without the room
// symbolBounds() keeps for pin texts. A power symbol has a pin at least, so
// its bounds are never empty.
Bounds symbolDrawingBounds(const Symbol &symbol)
{
    Bounds body;
    if (!symbol.graphics.isEmpty())
        body.add(symbolBodyBounds(symbol));
    for (const Pin &pin : symbol.pins)
        body.add(QRectF(toMm(pin.at), toMm(pinBodyEnd(pin))).normalized());
    return body;
}

} // namespace

QList<SheetObjectShape> sheetObjectShapes(const Sheet &sheet, const Library &library,
                                          const PowerShown &power)
{
    QList<SheetObjectShape> shapes;
    const auto addRect = [&shapes](const ObjectId &id, const QRectF &rect,
                                   const QRectF &body = QRectF()) {
        shapes.append(SheetObjectShape{id, rect, QPolygonF(), body});
    };
    for (const SheetGraphic &item : sheet.graphics) {
        shapes.append(SheetObjectShape{item.id, graphicBounds(item.graphic),
                                       graphicLine(item.graphic), QRectF()});
    }
    for (const Bus &bus : sheet.buses)
        shapes.append(SheetObjectShape{bus.id, toPolygon(bus.points).boundingRect(),
                                       toPolygon(bus.points), QRectF()});
    for (const Wire &wire : sheet.wires)
        shapes.append(SheetObjectShape{wire.id, toPolygon(wire.points).boundingRect(),
                                       toPolygon(wire.points), QRectF()});
    for (const Component &component : sheet.components) {
        QRectF drawing;
        const Symbol *symbol = library.findSymbolOfPart(component.part);
        const Bounds body = symbol != nullptr ? symbolDrawingBounds(*symbol) : Bounds();
        if (!body.isEmpty())
            drawing = componentTransform(component).mapRect(body.rect());
        else
            drawing = missingRect(toMm(component.at), MISSING_SYMBOL_SIZE);
        Bounds bounds;
        bounds.add(drawing);
        bounds.add(componentTextRect(component, ComponentText::Designator, library, power));
        if (!componentTextShown(component, ComponentText::Value, library, power).isEmpty())
            bounds.add(componentTextRect(component, ComponentText::Value, library, power));
        addRect(component.id, bounds.rect(), drawing);
    }
    for (const PowerSymbol &power : sheet.powerSymbols) {
        const Symbol *symbol = library.findSymbol(power.symbol);
        if (symbol == nullptr) {
            addRect(power.id, missingRect(toMm(power.at), MISSING_SYMBOL_SIZE));
            continue;
        }
        // The drawing, as for a component, then the level name.
        const QRectF drawing =
            powerTransform(power).mapRect(symbolDrawingBounds(*symbol).rect());
        Bounds bounds;
        bounds.add(drawing);
        const QString name = powerNameShown(power, *symbol);
        if (!name.isEmpty()) {
            bounds.add(textBounds(powerNameAt(power, *symbol), name, LABEL_TEXT_HEIGHT,
                                  TextAlign::Center));
        }
        addRect(power.id, bounds.rect(), drawing);
    }
    for (const Label &label : sheet.labels)
        addRect(label.id, labelTextRect(label).united(squareAround(toMm(label.at),
                                                                   LABEL_POINT_GRIP)));
    for (const NoConnect &mark : sheet.noConnects)
        addRect(mark.id, squareAround(toMm(mark.at), 2 * NO_CONNECT_HALF));
    return shapes;
}

Point busNamePoint(const Bus &bus, int index)
{
    return bus.points.value(0) + bus.namesAt.value(index);
}

QRectF sheetDrawingBounds(const Sheet &sheet, const Library &library, const PowerShown &power)
{
    Bounds bounds;
    for (const SheetObjectShape &shape : sheetObjectShapes(sheet, library, power))
        bounds.add(shape.rect);
    for (const Bus &bus : sheet.buses) {
        if (bus.name.isEmpty())
            continue;
        for (int i = 0; i < bus.namesAt.size(); ++i)
            bounds.add(busNameRect(bus, i));
    }
    return bounds.isEmpty() ? QRectF() : bounds.rect();
}

QRectF busNameRect(const Bus &bus, int index)
{
    return textBounds(busNameAt(bus, index), bus.name, LABEL_TEXT_HEIGHT, TextAlign::Left);
}

Point labelTextPoint(const Label &label)
{
    return label.at + label.textAt;
}

QRectF labelTextRect(const Label &label)
{
    QTransform transform;
    const QPointF at = toMm(labelTextPoint(label));
    transform.translate(at.x(), at.y());
    transform.rotate(label.rotation);
    // The question mark of a label with no text yet.
    const QString text = label.text.isEmpty() ? QString::fromLatin1(EMPTY_LABEL_MARK) : label.text;
    return transform.mapRect(textBounds(labelTextAt(), text, LABEL_TEXT_HEIGHT, TextAlign::Left));
}

QRectF powerNameRect(const PowerSymbol &power, const Library &library)
{
    const Symbol *symbol = library.findSymbol(power.symbol);
    if (symbol == nullptr)
        return missingRect(toMm(power.at), MISSING_SYMBOL_SIZE);
    return textBounds(powerNameAt(power, *symbol), power.name, LABEL_TEXT_HEIGHT,
                      TextAlign::Center);
}

QString revisionWord()
{
    return QCoreApplication::translate("ppcb::Paint", "Revision");
}

QString componentTextShown(const Component &component, ComponentText text,
                           const Library &library, const PowerShown &power)
{
    if (text == ComponentText::Designator)
        return component.designator;
    QString shown = component.value;
    if (shown.isEmpty()) {
        const Part *part = library.findPart(component.part);
        if (part == nullptr || !part->noValue)
            shown = QLatin1String(VALUE_PLACEHOLDER);
    }
    if (!power.marks && component.powerMw > 0 && component.powerMw != power.commonMw) {
        const QString watts = formatPower(component.powerMw);
        shown = shown.isEmpty() ? watts : shown + QLatin1Char(' ') + watts;
    }
    return shown;
}

Point componentTextPoint(const Component &component, ComponentText text)
{
    return component.at
           + (text == ComponentText::Designator ? component.refAt : component.valueAt);
}

QRectF componentTextRect(const Component &component, ComponentText text,
                         const Library &library, const PowerShown &power)
{
    return textBounds(toMm(componentTextPoint(component, text)),
                      componentTextShown(component, text, library, power), PLACEHOLDER_TEXT_HEIGHT,
                      TextAlign::Center);
}

QRectF placementRect(const Placement &placement, const Library &library)
{
    const Footprint *footprint = library.findFootprint(placement.footprint);
    if (footprint == nullptr)
        return missingRect(toMm(placement.at), MISSING_FOOTPRINT_SIZE);
    return placementTransform(placement).mapRect(footprintBounds(*footprint));
}

QString placementTextShown(const Placement &placement, ComponentText text)
{
    if (text == ComponentText::Designator)
        return placement.showRef ? placement.designator : QString();
    return placement.showValue ? placement.value : QString();
}

Point placementTextPoint(const Placement &placement, ComponentText text)
{
    return placement.at
           + (text == ComponentText::Designator ? placement.refAt : placement.valueAt);
}

QRectF placementTextRect(const Placement &placement, ComponentText text)
{
    return textBounds(toMm(placementTextPoint(placement, text)),
                      placementTextShown(placement, text), PLACEHOLDER_TEXT_HEIGHT,
                      TextAlign::Center);
}

Point linkTextPoint(const WireLink &link)
{
    const QPointF place = linkTextPlace(link);
    return Point{mmToNm(place.x()), mmToNm(place.y())} + link.textAt;
}

QRectF linkTextRect(const WireLink &link)
{
    return textBounds(toMm(linkTextPoint(link)), link.designator, PLACEHOLDER_TEXT_HEIGHT,
                      TextAlign::Center);
}

QLineF leaderLine(const Placement &placement, ComponentText text)
{
    const bool wanted =
        text == ComponentText::Designator ? placement.refLine : placement.valueLine;
    if (!wanted || placementTextShown(placement, text).isEmpty())
        return QLineF();
    const QRectF label = placementTextRect(placement, text);
    const QPointF part = toMm(placement.at);
    const QPointF onText(qBound(label.left(), part.x(), label.right()),
                         qBound(label.top(), part.y(), label.bottom()));
    if (onText == part)
        return QLineF();
    return QLineF(onText, part);
}

QRectF boardBounds(const Board &board, const Library &library)
{
    Bounds bounds;
    for (const OutlineCorner &corner : board.outline)
        bounds.add(QRectF(toMm(corner.at), QSizeF(0, 0)));
    for (const Placement &placement : board.placements)
        bounds.add(placementRect(placement, library));
    for (const Track &track : board.tracks) {
        const double half = nmToMm(track.width) / 2;
        bounds.add(QRectF(toMm(track.from), toMm(track.to))
                       .normalized()
                       .adjusted(-half, -half, half, half));
    }
    for (const Via &via : board.vias)
        bounds.add(squareAround(toMm(via.at), nmToMm(via.size)));
    for (const WireLink &link : board.links) {
        bounds.add(squareAround(toMm(link.from), nmToMm(link.size)));
        bounds.add(squareAround(toMm(link.to), nmToMm(link.size)));
        if (!link.designator.isEmpty())
            bounds.add(linkTextRect(link));
    }
    for (const MountingHole &hole : board.holes)
        bounds.add(squareAround(toMm(hole.at), nmToMm(holeOuterDiameter(hole))));
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

QRectF layoutBounds(const Board &board, const Library &library)
{
    if (board.placements.isEmpty() && board.tracks.isEmpty() && board.vias.isEmpty()
        && board.holes.isEmpty() && board.graphics.isEmpty() && board.zones.isEmpty())
        return boardBounds(board, library);
    Board laid = board;
    laid.outline.clear();
    return boardBounds(laid, library);
}

QColor boardLayerColor(BoardLayer layer)
{
    switch (layer) {
    case BoardLayer::CopperTop:
        return paintcolor::padFront();
    case BoardLayer::CopperBottom:
        return paintcolor::padBack();
    case BoardLayer::MaskTop:
        return paintcolor::maskTop();
    case BoardLayer::MaskBottom:
        return paintcolor::maskBottom();
    case BoardLayer::SilkscreenTop:
        return paintcolor::silkscreenTop();
    case BoardLayer::SilkscreenBottom:
        return paintcolor::silkscreenBottom();
    case BoardLayer::PasteTop:
        return paintcolor::pasteTop();
    case BoardLayer::PasteBottom:
        return paintcolor::pasteBottom();
    case BoardLayer::Outline:
        return paintcolor::boardOutline();
    }
    return paintcolor::boardOutline();
}

void paintBoard(QPainter &painter, const Board &board, const Library &library,
                const BoardLayerSet &shown, bool lockedMarked, std::optional<BoardSide> bright)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Zones are only outlined: nothing fills them yet.
    painter.setBrush(Qt::NoBrush);
    for (const Zone &zone : board.zones) {
        if (!shown.contains(copperLayer(zone.side)))
            continue;
        painter.setPen(dashedPen(copperColor(zone.side), ZONE_LINE_WIDTH));
        painter.drawPolygon(toPolygon(zone.points));
    }

    // The side shown bright over the other.
    const BoardSide under = bright == BoardSide::Bottom ? BoardSide::Top : BoardSide::Bottom;
    paintTracks(painter, board, under, shown, bright);
    paintTracks(painter, board, otherSide(under), shown, bright);
    for (const Placement &placement : board.placements)
        paintPlacementPads(painter, library, placement, shown, bright);
    for (const Via &via : board.vias) {
        paintRing(painter, toMm(via.at), nmToMm(via.size), nmToMm(via.drill), paintcolor::via(),
                  throughShown(shown));
    }
    // The pads of wire links, through the board as those of parts are.
    const QColor linkPad =
        board.copper == CopperSides::Both
            ? paintcolor::padBoth()
            : shownCopper(board.copper == CopperSides::Bottom ? BoardSide::Bottom : BoardSide::Top,
                          bright);
    for (const WireLink &link : board.links) {
        for (const Point &end : {link.from, link.to})
            paintRing(painter, toMm(end), nmToMm(link.size), nmToMm(link.drill), linkPad,
                      throughShown(shown));
    }
    for (const MountingHole &hole : board.holes)
        paintMountingHole(painter, board, hole, shown, lockedMarked, bright);

    // The top over the bottom.
    for (const BoardSide side : {BoardSide::Bottom, BoardSide::Top}) {
        if (shown.contains(silkscreenLayer(side)))
            paintSilkscreen(painter, board, library, side, lockedMarked);
    }

    if (shown.contains(BoardLayer::Outline)) {
        painter.setPen(linePen(paintcolor::boardOutline(), OUTLINE_WIDTH));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(outlinePath(board.outline));
    }
    painter.restore();
}

namespace {

// The heights of the caption and the ruler's label of the board's print,
// the length of the ruler's ticks, and the width of its lines, in mm.
const double PRINT_CAPTION_HEIGHT = 3.0;
const double PRINT_RULER_TEXT_HEIGHT = 2.5;
const double PRINT_RULER_TICK_LENGTH = 2.0;
const double PRINT_RULER_LINE = 0.2;
// The lines of the circle and the cross of a hole without a pad, in mm.
const double PRINT_BARE_HOLE_LINE = 0.2;

// A filled circle of 'diameter' at 'at'.
void paintDot(QPainter &painter, const QPointF &at, double diameter, const QColor &color)
{
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(at, diameter / 2, diameter / 2);
}

// The centre hole of a pad drilled 'drill' wide, as 'print' asks.
void paintCentreHole(QPainter &painter, const QPointF &at, Coord drill, const BoardPrint &print,
                     const QColor &paper)
{
    const Coord diameter = std::min(print.centreHole, drill);
    if (diameter > 0)
        paintDot(painter, at, nmToMm(diameter), paper);
}

void paintCopperPad(QPainter &painter, const CopperPad &pad, const QColor &ink)
{
    const double width = nmToMm(pad.sizeX);
    const double height = nmToMm(pad.sizeY);
    const QRectF rect(toMm(pad.at) - QPointF(width / 2, height / 2), QSizeF(width, height));
    const double smaller = std::min(width, height);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
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
}

// The copper of 'print.side' in 'ink', with the centre holes in 'paper'.
void paintCopperPrint(QPainter &painter, const Board &board, const Library &library,
                      const BoardPrint &print, const QColor &ink, const QColor &paper)
{
    const BoardSide side = print.side;
    for (const Track &track : board.tracks) {
        if (track.side != side)
            continue;
        QPen pen(ink, nmToMm(track.width));
        pen.setCapStyle(Qt::RoundCap);
        painter.setPen(pen);
        painter.drawLine(toMm(track.from), toMm(track.to));
    }
    // The pads, and then their centre holes over every copper.
    const QList<CopperPad> pads = copperPads(board, library, side);
    for (const CopperPad &pad : pads)
        paintCopperPad(painter, pad, ink);
    for (const CopperPad &pad : pads) {
        if (pad.drill > 0)
            paintCentreHole(painter, toMm(pad.at), pad.drill, print, paper);
    }
    // A hole without a pad: a circle just inside its edge and a cross over
    // its middle, drilled away with it.
    QPen pen(ink, PRINT_BARE_HOLE_LINE);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    for (const MountingHole &hole : board.holes) {
        if (hole.ring > 0)
            continue;
        const QPointF at = toMm(hole.at);
        const double radius = nmToMm(hole.drill) / 2;
        const double circle = radius - PRINT_BARE_HOLE_LINE / 2;
        if (circle > 0)
            painter.drawEllipse(at, circle, circle);
        painter.drawLine(at - QPointF(radius, 0), at + QPointF(radius, 0));
        painter.drawLine(at - QPointF(0, radius), at + QPointF(0, radius));
    }
}

} // namespace

void paintBoardLayerPrint(QPainter &painter, const Board &board, const Library &library,
                          const BoardPrint &print, const QRectF &area)
{
    const bool copper = print.layer == BoardPrintLayer::Copper;
    const bool inverted = copper && print.inverted;
    const QColor ink = inverted ? QColor(Qt::white) : QColor(Qt::black);
    const QColor paper = inverted ? QColor(Qt::black) : QColor(Qt::white);
    const bool marks = print.registrationMarks;
    painter.save();
    // Solid black to its edges, without the grey a smoothed edge has.
    painter.setRenderHint(QPainter::Antialiasing, false);
    if (inverted) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(paper);
        painter.drawRect(boardPrintCopyRect(area, marks));
    }
    if (copper) {
        paintCopperPrint(painter, board, library, print, ink, paper);
    } else {
        paintSilkscreenLayer(painter, board, library, print.side);
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(ink, OUTLINE_WIDTH));
    painter.drawPath(outlinePath(board.outline));
    if (marks) {
        QPen pen(ink, REGISTRATION_MARK_LINE);
        pen.setCapStyle(Qt::FlatCap);
        painter.setPen(pen);
        const double arm = REGISTRATION_MARK_ARM;
        for (const QPointF &centre : registrationMarkCentres(area)) {
            painter.drawLine(centre - QPointF(arm, 0), centre + QPointF(arm, 0));
            painter.drawLine(centre - QPointF(0, arm), centre + QPointF(0, arm));
        }
    }
    painter.restore();
}

void paintSilkscreenLayer(QPainter &painter, const Board &board, const Library &library,
                          BoardSide side)
{
    const MonochromeInk monochrome;
    paintSilkscreen(painter, board, library, side, false);
}

void paintBoardPrintHeader(QPainter &painter, const QPointF &at, const QString &caption,
                           const QString &rulerLabel, const QString &drills)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor black(Qt::black);
    drawText(painter, at + QPointF(0, PRINT_CAPTION_HEIGHT / 2), caption, PRINT_CAPTION_HEIGHT,
             TextAlign::Left, black);
    // The ruler under the caption, its ticks hanging from it.
    const double y = at.y() + 2 * PRINT_CAPTION_HEIGHT + PRINT_RULER_TEXT_HEIGHT / 2;
    QPen pen(black, PRINT_RULER_LINE);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(at.x(), y), QPointF(at.x() + SCALE_RULER_LENGTH, y));
    for (double x = 0; x <= SCALE_RULER_LENGTH + 0.001; x += SCALE_RULER_TICK)
        painter.drawLine(QPointF(at.x() + x, y), QPointF(at.x() + x, y + PRINT_RULER_TICK_LENGTH));
    drawText(painter, QPointF(at.x() + SCALE_RULER_LENGTH + PRINT_RULER_TEXT_HEIGHT, y),
             rulerLabel, PRINT_RULER_TEXT_HEIGHT, TextAlign::Left, black);
    // The drill table under the ruler's ticks.
    drawText(painter,
             QPointF(at.x(), y + PRINT_RULER_TICK_LENGTH + PRINT_CAPTION_HEIGHT),
             drills, PRINT_RULER_TEXT_HEIGHT, TextAlign::Left, black);
    painter.restore();
}

} // namespace ppcb
