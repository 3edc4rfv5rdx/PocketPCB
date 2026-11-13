#include "paint.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

#include <cmath>
#include <cstdlib>

using namespace ppcb;

namespace {

// The test picture: 20 pixels per millimetre, origin in the middle.
const int IMAGE_SIZE = 400;
const double PIXELS_PER_MM = 20.0;
// Antialiasing and colour conversion may shift a channel a little.
const int COLOR_TOLERANCE = 40;
const double PI = 3.14159265358979323846;

// The "REF" and "VALUE" marks are moved here, away from what is tested.
const Point FAR_REF{0, -9000000};
const Point FAR_VALUE{0, 9000000};

QImage newImage()
{
    QImage image(IMAGE_SIZE, IMAGE_SIZE, QImage::Format_ARGB32_Premultiplied);
    image.fill(paintcolor::background());
    return image;
}

void setUp(QPainter &painter)
{
    painter.translate(IMAGE_SIZE / 2, IMAGE_SIZE / 2);
    painter.scale(PIXELS_PER_MM, PIXELS_PER_MM);
}

QImage render(const Symbol &symbol)
{
    QImage image = newImage();
    QPainter painter(&image);
    setUp(painter);
    paintSymbol(painter, symbol);
    return image;
}

QImage render(const Footprint &footprint)
{
    QImage image = newImage();
    QPainter painter(&image);
    setUp(painter);
    paintFootprint(painter, footprint);
    return image;
}

// Colour of the picture at a point given in millimetres.
QColor colorAt(const QImage &image, double xMm, double yMm)
{
    return image.pixelColor(int(std::lround(IMAGE_SIZE / 2 + xMm * PIXELS_PER_MM)),
                            int(std::lround(IMAGE_SIZE / 2 + yMm * PIXELS_PER_MM)));
}

bool isColor(const QColor &actual, const QColor &expected)
{
    return std::abs(actual.red() - expected.red()) <= COLOR_TOLERANCE
           && std::abs(actual.green() - expected.green()) <= COLOR_TOLERANCE
           && std::abs(actual.blue() - expected.blue()) <= COLOR_TOLERANCE;
}

Symbol emptySymbol()
{
    Symbol symbol;
    symbol.refAt = FAR_REF;
    symbol.valueAt = FAR_VALUE;
    return symbol;
}

Footprint emptyFootprint()
{
    Footprint footprint;
    footprint.refAt = FAR_REF;
    footprint.valueAt = FAR_VALUE;
    return footprint;
}

Graphic thickArc(int start, int span)
{
    Graphic arc;
    arc.kind = GraphicKind::Arc;
    arc.radius = 5000000;
    arc.startAngle = start;
    arc.spanAngle = span;
    arc.width = 500000;
    return arc;
}

// A point of a circle of 5 mm at the given angle, by the model's rule:
// 0 is +X, angles grow towards +Y.
double arcX(int degrees) { return 5.0 * std::cos(degrees * PI / 180.0); }
double arcY(int degrees) { return 5.0 * std::sin(degrees * PI / 180.0); }

} // namespace

class TestPaint : public QObject
{
    Q_OBJECT

private slots:
    void pinPointsTowardsTheBody()
    {
        Pin pin;
        pin.at = Point{0, 0};
        pin.length = 2540000;
        pin.side = PinSide::Left;
        QVERIFY(pinBodyEnd(pin) == (Point{2540000, 0}));
        pin.side = PinSide::Right;
        QVERIFY(pinBodyEnd(pin) == (Point{-2540000, 0}));
        pin.side = PinSide::Up;
        QVERIFY(pinBodyEnd(pin) == (Point{0, 2540000}));
        pin.side = PinSide::Down;
        QVERIFY(pinBodyEnd(pin) == (Point{0, -2540000}));
    }

    // Angles grow towards +Y: clockwise on screen.
    void arcTurnsClockwiseOnScreen()
    {
        Symbol symbol = emptySymbol();
        symbol.graphics.append(thickArc(0, 90));
        const QImage image = render(symbol);

        QVERIFY(isColor(colorAt(image, arcX(45), arcY(45)), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, arcX(10), arcY(10)), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, arcX(80), arcY(80)), paintcolor::symbol()));
        // The other three quarters stay empty.
        QVERIFY(isColor(colorAt(image, arcX(-45), arcY(-45)), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, arcX(135), arcY(135)), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, arcX(225), arcY(225)), paintcolor::background()));
    }

    void arcStartAndSpan()
    {
        Symbol symbol = emptySymbol();
        symbol.graphics.append(thickArc(90, 180)); // from +Y through -X to -Y
        const QImage image = render(symbol);

        QVERIFY(isColor(colorAt(image, arcX(180), arcY(180)), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, arcX(100), arcY(100)), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, arcX(260), arcY(260)), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, arcX(0), arcY(0)), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, arcX(45), arcY(45)), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, arcX(315), arcY(315)), paintcolor::background()));
    }

    void symbolShapesAndPins()
    {
        Symbol symbol = emptySymbol();
        symbol.showPinNumbers = false;
        symbol.showPinNames = false;

        Graphic rect;
        rect.kind = GraphicKind::Rect;
        rect.a = Point{-2540000, -1016000};
        rect.b = Point{2540000, 1016000};
        rect.width = 300000;
        symbol.graphics.append(rect);

        Pin pin;
        pin.number = QStringLiteral("1");
        pin.at = Point{-5080000, 0};
        pin.side = PinSide::Left;
        symbol.pins.append(pin);

        const QImage image = render(symbol);
        // The edges of the rectangle, and its empty inside.
        QVERIFY(isColor(colorAt(image, -2.54, 0.5), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, 2.54, -0.5), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, 1.0, -1.016), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, 1.5, 0.5), paintcolor::background()));
        // The pin's line, from the wire point to the body.
        QVERIFY(isColor(colorAt(image, -3.81, 0), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, -3.81, 0.8), paintcolor::background()));
        // Nothing where a pin on the other side would be.
        QVERIFY(isColor(colorAt(image, 3.81, 0), paintcolor::background()));
    }

    void polygonFill()
    {
        Graphic triangle;
        triangle.kind = GraphicKind::Polygon;
        triangle.points = {Point{-4000000, 3000000}, Point{4000000, 3000000}, Point{0, 7000000}};

        Symbol outline = emptySymbol();
        outline.graphics.append(triangle);
        QVERIFY(isColor(colorAt(render(outline), 0, 4.5), paintcolor::background()));
        QVERIFY(isColor(colorAt(render(outline), 0, 3.0), paintcolor::symbol()));

        triangle.filled = true;
        Symbol filled = emptySymbol();
        filled.graphics.append(triangle);
        QVERIFY(isColor(colorAt(render(filled), 0, 4.5), paintcolor::symbol()));
        // The fill does not leak outside.
        QVERIFY(isColor(colorAt(render(filled), 0, 2.0), paintcolor::background()));
    }

    void dashedLineHasGaps()
    {
        // 1.5 mm of line, 1 mm of gap, from the left end at x = -8.
        Graphic line;
        line.kind = GraphicKind::Line;
        line.a = Point{-8000000, 0};
        line.b = Point{8000000, 0};
        line.width = 400000;

        Symbol solid = emptySymbol();
        solid.graphics.append(line);
        QVERIFY(isColor(colorAt(render(solid), -7.25, 0), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(render(solid), -6.0, 0), paintcolor::symbol()));

        line.dashed = true;
        Symbol dashed = emptySymbol();
        dashed.graphics.append(line);
        const QImage image = render(dashed);
        QVERIFY(isColor(colorAt(image, -7.25, 0), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, -6.0, 0), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, -4.75, 0), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, -3.5, 0), paintcolor::background()));

        // The dashes do not depend on the line width.
        line.width = 200000;
        Symbol thin = emptySymbol();
        thin.graphics.append(line);
        QVERIFY(isColor(colorAt(render(thin), -4.75, 0), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(render(thin), -3.5, 0), paintcolor::background()));
    }

    void textIsDrawn()
    {
        Graphic text;
        text.kind = GraphicKind::Text;
        text.a = Point{5000000, 5000000};
        text.text = QStringLiteral("WWW");
        text.height = 2000000;
        Symbol symbol = emptySymbol();
        symbol.graphics.append(text);

        const QImage image = render(symbol);
        // Some ink within the text's place; where exactly depends on the font.
        int inked = 0;
        for (double y = 4.0; y <= 6.0; y += 0.05) {
            for (double x = 2.0; x <= 8.0; x += 0.05) {
                if (!isColor(colorAt(image, x, y), paintcolor::background()))
                    ++inked;
            }
        }
        QVERIFY(inked > 0);
    }

    void padsHolesAndLayers()
    {
        Footprint footprint = emptyFootprint();

        Pad through; // no number: nothing is written over the hole
        through.at = Point{-5000000, 0};
        through.sizeX = 2000000;
        through.sizeY = 2000000;
        through.hole = 1000000;
        through.layers = PadLayers::Both;
        footprint.pads.append(through);

        Pad front;
        front.at = Point{0, 0};
        front.shape = PadShape::Rect;
        front.sizeX = 3000000;
        front.sizeY = 1000000;
        front.layers = PadLayers::Front;
        footprint.pads.append(front);

        Pad back = front;
        back.at = Point{5000000, 0};
        back.shape = PadShape::Oval;
        back.layers = PadLayers::Back;
        footprint.pads.append(back);

        const QImage image = render(footprint);
        // The through pad: a hole in the middle, copper around it, nothing outside.
        QVERIFY(isColor(colorAt(image, -5.0, 0), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, -5.0 + 0.75, 0), paintcolor::padBoth()));
        QVERIFY(isColor(colorAt(image, -5.0, -0.75), paintcolor::padBoth()));
        QVERIFY(isColor(colorAt(image, -5.0 + 1.3, 0), paintcolor::background()));
        // Pads without a hole are solid, each in the colour of its layer.
        // (The origin mark crosses the middle of the front pad, so a point beside it is taken.)
        QVERIFY(isColor(colorAt(image, 1.2, 0.3), paintcolor::padFront()));
        QVERIFY(isColor(colorAt(image, 5.0, 0), paintcolor::padBack()));
        // A rectangle has its corner, an oval does not.
        QVERIFY(isColor(colorAt(image, 1.4, 0.4), paintcolor::padFront()));
        QVERIFY(isColor(colorAt(image, 5.0 + 1.4, 0.4), paintcolor::background()));
    }

    void boundsHoldTheDrawing()
    {
        Symbol symbol = emptySymbol();
        Graphic line; // horizontal: a rectangle of no height
        line.a = Point{-6000000, 3000000};
        line.b = Point{6000000, 3000000};
        symbol.graphics.append(line);
        Pin pin;
        pin.number = QStringLiteral("1");
        pin.at = Point{-7620000, 0};
        symbol.pins.append(pin);

        const QRectF bounds = symbolBounds(symbol);
        QVERIFY(bounds.contains(QPointF(-6.0, 3.0)));
        QVERIFY(bounds.contains(QPointF(6.0, 3.0)));
        QVERIFY(bounds.contains(toMm(pin.at)));
        QVERIFY(bounds.contains(toMm(pinBodyEnd(pin))));
        QVERIFY(bounds.contains(toMm(FAR_REF)));
        QVERIFY(bounds.contains(toMm(FAR_VALUE)));

        Footprint footprint = emptyFootprint();
        Pad pad;
        pad.at = Point{8000000, -2000000};
        pad.sizeX = 2000000;
        pad.sizeY = 4000000;
        footprint.pads.append(pad);
        const QRectF footprintRect = footprintBounds(footprint);
        QVERIFY(footprintRect.contains(QPointF(8.99, -3.99)));
        QVERIFY(footprintRect.contains(QPointF(7.01, -0.01)));
    }

    void boundsAreNeverEmpty()
    {
        const QRectF symbolRect = symbolBounds(Symbol());
        QVERIFY(symbolRect.width() > 0 && symbolRect.height() > 0);
        const QRectF footprintRect = footprintBounds(Footprint());
        QVERIFY(footprintRect.width() > 0 && footprintRect.height() > 0);
        QVERIFY(symbolRect.contains(QPointF(0, 0)));
    }
};

QTEST_MAIN(TestPaint)
#include "test_paint.moc"
