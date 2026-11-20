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


// A picture made by 'paint', which gets the painter set up as above.
template <typename Paint>
QImage renderWith(Paint paint)
{
    QImage image = newImage();
    QPainter painter(&image);
    setUp(painter);
    paint(painter);
    return image;
}

// The pad of boardWithPad(): no hole, 2 mm across, 3 mm to the right of the
// footprint's origin.
Footprint padFootprint()
{
    Pad pad;
    pad.at = Point{3000000, 0};
    pad.sizeX = 2000000;
    pad.sizeY = 2000000;
    pad.layers = PadLayers::Front;
    Footprint footprint;
    footprint.pads.append(pad);
    return footprint;
}

// A project library with padFootprint() as "f".
Library padLibrary()
{
    Library library;
    library.footprints.insert(QStringLiteral("f"), padFootprint());
    return library;
}

QImage render(const Board &board, const Library &library = padLibrary())
{
    return renderWith(
        [&board, &library](QPainter &painter) { paintBoard(painter, board, library); });
}

// A schematic of one sheet with nothing on it.
Schematic emptySchematic()
{
    Schematic schematic;
    schematic.sheets.append(Sheet());
    return schematic;
}

QImage render(const Schematic &schematic, const Library &library = Library())
{
    return renderWith([&schematic, &library](QPainter &painter) {
        paintSheet(painter, schematic, library, 0, TitleBlock(), PowerShown());
    });
}

// The first sheet of the design's schematic.
QImage render(const Design &design)
{
    return render(design.schematic, design.library);
}

// Whether any pixel of the rectangle (millimetres) has the colour.
bool anyColor(const QImage &image, double left, double top, double right, double bottom,
              const QColor &color)
{
    const double step = 1.0 / PIXELS_PER_MM;
    for (double y = top; y <= bottom; y += step) {
        for (double x = left; x <= right; x += step) {
            if (isColor(colorAt(image, x, y), color))
                return true;
        }
    }
    return false;
}

// A symbol of one thick line that runs from 1 to 4 mm to the right of the
// origin: it shows which way the symbol is turned.
Symbol arrowSymbol()
{
    Graphic line;
    line.kind = GraphicKind::Line;
    line.a = Point{1000000, 0};
    line.b = Point{4000000, 0};
    line.width = 600000;
    Symbol symbol;
    symbol.graphics.append(line);
    return symbol;
}

// A sheet with one component of the given symbol at -5, -5 mm: away from
// the frame, whose lines run through the positive quarter of the picture.
// The symbol is "s", of the part "p".
Design schematicWith(const Symbol &symbol, int rotation, bool mirrored)
{
    Design design;
    design.schematic = emptySchematic();
    Part part;
    part.symbol = QStringLiteral("s");
    design.library.parts.insert(QStringLiteral("p"), part);
    design.library.symbols.insert(QStringLiteral("s"), symbol);
    Component component;
    component.part = QStringLiteral("p");
    component.at = Point{-5000000, -5000000};
    component.rotation = rotation;
    component.mirrored = mirrored;
    // The grey mark of the empty value goes far from the lines looked for.
    component.valueAt = FAR_VALUE;
    design.schematic.sheets[0].components.append(component);
    return design;
}

// A board with the footprint "f" of padLibrary() placed at the origin.
Board boardWithPad(int rotation, BoardSide side)
{
    Board board;
    Placement placement;
    placement.footprint = QStringLiteral("f");
    placement.rotation = rotation;
    placement.side = side;
    placement.showRef = false;
    board.placements.append(placement);
    return board;
}

// A square outline of 16 mm around the origin; its top right corner is rounded.
Board boardWithOutline(Coord radius)
{
    const Coord half = 8000000;
    Board board;
    board.outline = {OutlineCorner{Point{-half, -half}, 0}, OutlineCorner{Point{half, -half}, radius},
                     OutlineCorner{Point{half, half}, 0}, OutlineCorner{Point{-half, half}, 0}};
    return board;
}

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

    void componentIsTurnedClockwiseAndMirrored()
    {
        // The line of the symbol is looked for 2.5 mm from the component.
        struct Case { int rotation; bool mirrored; double x; double y; };
        const Case cases[] = {
            {0, false, -2.5, -5.0},  {90, false, -5.0, -2.5}, {180, false, -7.5, -5.0},
            {270, false, -5.0, -7.5}, {0, true, -7.5, -5.0},
            // Mirrored first, turned after: left becomes up.
            {90, true, -5.0, -7.5},
        };
        const double spots[4][2] = {{-2.5, -5.0}, {-5.0, -2.5}, {-7.5, -5.0}, {-5.0, -7.5}};
        for (const Case &c : cases) {
            const QImage image = render(schematicWith(arrowSymbol(), c.rotation, c.mirrored));
            for (const double *spot : spots) {
                const bool expected = spot[0] == c.x && spot[1] == c.y;
                QVERIFY2(isColor(colorAt(image, spot[0], spot[1]), paintcolor::symbol()) == expected,
                         qPrintable(QStringLiteral("rotation %1 mirrored %2 at %3, %4")
                                        .arg(c.rotation).arg(c.mirrored).arg(spot[0]).arg(spot[1])));
            }
        }
    }

    // A text of a symbol stays readable however the symbol is placed.
    void symbolTextStaysReadable()
    {
        Graphic text;
        text.kind = GraphicKind::Text;
        text.text = QStringLiteral("LF");
        text.height = 2000000;
        Symbol centred;
        centred.graphics.append(text);

        // Centred on the origin, it looks the same turned over or mirrored...
        const QImage plain = render(schematicWith(centred, 0, false));
        QVERIFY(anyColor(plain, -8, -7, -2, -3, paintcolor::symbol()));
        QCOMPARE(render(schematicWith(centred, 180, false)), plain);
        QCOMPARE(render(schematicWith(centred, 0, true)), plain);
        QCOMPARE(render(schematicWith(centred, 180, true)), plain);
        // ...and reads bottom to top on its side, whichever side that is.
        const QImage upward = render(schematicWith(centred, 270, false));
        QVERIFY(upward != plain);
        QCOMPARE(render(schematicWith(centred, 90, false)), upward);
        QCOMPARE(render(schematicWith(centred, 90, true)), upward);

        // A text that starts at its anchor keeps the anchor: mirrored, it
        // ends there instead.
        text.align = TextAlign::Left;
        Symbol anchored;
        anchored.graphics.append(text);
        const QImage right = render(schematicWith(anchored, 0, false));
        QVERIFY(anyColor(right, -4.8, -7, -1, -3, paintcolor::symbol()));
        QVERIFY(!anyColor(right, -9.5, -7, -5.2, -3, paintcolor::symbol()));
        const QImage left = render(schematicWith(anchored, 0, true));
        QVERIFY(anyColor(left, -9.5, -7, -5.2, -3, paintcolor::symbol()));
        QVERIFY(!anyColor(left, -4.8, -7, -1, -3, paintcolor::symbol()));
    }

    // A paper on a page of its own size fills it; on a page of the other
    // way it is turned; on a smaller one it is shrunk and centred.
    void paperFitsThePage()
    {
        const QRectF a4(0, 0, 297, 210);
        QTransform t = paperOnPage(a4, QSizeF(297, 210));
        QCOMPARE(t.map(QPointF(0, 0)), QPointF(0, 0));
        QCOMPARE(t.map(QPointF(297, 210)), QPointF(297, 210));
        // Upright A4: the paper's top left corner goes to the top right.
        t = paperOnPage(a4, QSizeF(210, 297));
        QCOMPARE(t.map(QPointF(0, 0)), QPointF(210, 0));
        QCOMPARE(t.map(QPointF(297, 210)), QPointF(0, 297));
        QCOMPARE(t.map(QPointF(297, 0)), QPointF(210, 297));
        // A3 on A4, both lying: shrunk to fit and centred.
        const QRectF a3(0, 0, 420, 297);
        t = paperOnPage(a3, QSizeF(297, 210));
        const QRectF placed = t.mapRect(a3);
        QVERIFY(qAbs(placed.width() - 297) < 1e-9 || qAbs(placed.height() - 210) < 1e-9);
        QVERIFY(placed.left() >= -1e-9 && placed.right() <= 297 + 1e-9);
        QVERIFY(placed.top() >= -1e-9 && placed.bottom() <= 210 + 1e-9);
        QVERIFY(qAbs(placed.center().x() - 148.5) < 1e-9);
        QVERIFY(qAbs(placed.center().y() - 105) < 1e-9);
        QVERIFY(paperOnPage(QRectF(), QSizeF(297, 210)).isIdentity());
    }

    // Under MonochromeInk a wire and a bus are black; white text on the
    // plate of a loose label stays white.
    void monochromeInkDrawsBlack()
    {
        Schematic schematic = emptySchematic();
        Sheet &sheet = schematic.sheets[0];
        Wire wire;
        wire.points = {Point{-8000000, -8000000}, Point{-2000000, -8000000}};
        sheet.wires.append(wire);
        Bus bus;
        bus.points = {Point{-8000000, -6000000}, Point{-2000000, -6000000}};
        sheet.buses.append(bus);
        Label loose;
        loose.at = Point{-4000000, 6000000};
        loose.text = QStringLiteral("A");
        sheet.labels.append(loose);
        QImage image;
        {
            const MonochromeInk ink;
            {
                const MonochromeInk inner;
            }
            image = render(schematic);
        }
        QVERIFY(isColor(colorAt(image, -5, -8), Qt::black));
        QVERIFY(isColor(colorAt(image, -5, -6), Qt::black));
        QVERIFY(anyColor(image, -4, 1, -2, 6, Qt::black));
        QVERIFY(anyColor(image, -4, 1, -2, 6, paintcolor::background()));
        QVERIFY(!anyColor(image, -4, 1, -2, 6, paintcolor::looseLabel()));
        // Gone with it.
        const QImage colored = render(schematic);
        QVERIFY(isColor(colorAt(colored, -5, -8), paintcolor::wire()));
    }

    // The drawing's bounds hold a level wire and a named bus with its
    // name, and nothing of the empty paper; an empty sheet has none.
    void drawingBoundsHoldWhatIsPlaced()
    {
        Sheet sheet;
        QVERIFY(sheetDrawingBounds(sheet, Library(), PowerShown()).isNull());
        Wire wire;
        wire.points = {Point{10000000, 20000000}, Point{30000000, 20000000}};
        sheet.wires.append(wire);
        QRectF bounds = sheetDrawingBounds(sheet, Library(), PowerShown());
        QCOMPARE(bounds, QRectF(10, 20, 20, 0));
        Bus bus;
        bus.name = QStringLiteral("D[0..7]");
        bus.points = {Point{10000000, 40000000}, Point{30000000, 40000000}};
        sheet.buses.append(bus);
        bounds = sheetDrawingBounds(sheet, Library(), PowerShown());
        QVERIFY(bounds.contains(busNameRect(bus, 0)));
        QVERIFY(bounds.bottom() >= 40);
        QVERIFY(bounds.width() < 40 && bounds.height() < 40);
    }

    void sheetObjectsAreDrawn()
    {
        Schematic schematic = emptySchematic();
        Sheet &sheet = schematic.sheets[0];

        Wire wire;
        wire.points = {Point{-8000000, -8000000}, Point{-2000000, -8000000}};
        sheet.wires.append(wire);
        Bus bus;
        bus.points = {Point{-8000000, -6000000}, Point{-2000000, -6000000}};
        sheet.buses.append(bus);
        NoConnect mark;
        mark.at = Point{-8000000, -3000000};
        sheet.noConnects.append(mark);
        // A power symbol whose pin's line goes up from its point.
        Library library;
        Symbol stem;
        Pin pin;
        pin.number = QStringLiteral("1");
        pin.side = PinSide::Down;
        pin.length = 2540000;
        stem.pins = {pin};
        stem.power = QStringLiteral("+5V");
        library.symbols.insert(QStringLiteral("stem"), stem);
        PowerSymbol up;
        up.at = Point{-5000000, -2000000};
        up.symbol = QStringLiteral("stem");
        sheet.powerSymbols.append(up);
        PowerSymbol down = up;
        down.at = Point{-2000000, -4000000};
        down.rotation = 180;
        sheet.powerSymbols.append(down);
        // A label on a wire of its own, and one on nothing, which is red.
        Wire under;
        under.points = {Point{-10000000, 9500000}, Point{-1000000, 9500000}};
        sheet.wires.append(under);
        Label joined;
        joined.at = Point{-4000000, 9500000};
        joined.text = QStringLiteral("A");
        sheet.labels.append(joined);
        Label loose = joined;
        loose.at = Point{-4000000, 6000000};
        sheet.labels.append(loose);
        // One the library lacks is a red square.
        PowerSymbol lacking = up;
        lacking.at = Point{-8000000, 3000000};
        lacking.symbol = QStringLiteral("absent");
        sheet.powerSymbols.append(lacking);

        const QImage image = render(schematic, library);
        QVERIFY(isColor(colorAt(image, -5, -8), paintcolor::wire()));
        QVERIFY(isColor(colorAt(image, -5, -6), paintcolor::bus()));
        // The bus is the thicker of the two: the same pixel row off their
        // middles, 0.25 to 0.3 mm, is the bus's colour and the background.
        QVERIFY(isColor(colorAt(image, -5, -6.3), paintcolor::bus()));
        QVERIFY(isColor(colorAt(image, -5, -8.3), paintcolor::background()));
        // A no-connect mark on no pin of a part is red.
        QVERIFY(isColor(colorAt(image, -8, -3), paintcolor::looseNoConnect()));
        // The stem of a power symbol points up, or down when turned over.
        QVERIFY(isColor(colorAt(image, -5, -3.3), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, -5, -0.7), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, -2, -2.7), paintcolor::symbol()));
        QVERIFY(isColor(colorAt(image, -2, -5.3), paintcolor::background()));
        QVERIFY(anyColor(image, -11, 0.5, -5, 5.5, paintcolor::missing()));
        QVERIFY(anyColor(image, -4, 7.5, -2, 9.2, paintcolor::label()));
        QVERIFY(!anyColor(image, -4, 7.5, -2, 9.2, paintcolor::looseLabel()));
        QVERIFY(anyColor(image, -4, 1, -2, 6, paintcolor::looseLabel()));

        // The frame: 20 mm inside the paper's left edge, 5 mm inside its top,
        // seen around 17, 0 of the paper, whose corner is the origin.
        const QImage corner = renderWith([&schematic](QPainter &painter) {
            painter.translate(-17, 0);
            paintSheet(painter, schematic, Library(), 0, TitleBlock(), PowerShown());
        });
        QVERIFY(isColor(colorAt(corner, 3, 8), paintcolor::sheetFrame()));
        QVERIFY(isColor(colorAt(corner, 6, 5), paintcolor::sheetFrame()));
        QVERIFY(isColor(colorAt(corner, 6, 8), paintcolor::background()));
        QVERIFY(isColor(colorAt(corner, 0, 8), paintcolor::background()));

        // A sheet that is not there draws nothing at all.
        const QImage nothing = renderWith([&schematic](QPainter &painter) {
            paintSheet(painter, schematic, Library(), 1, TitleBlock(), PowerShown());
            paintSheet(painter, schematic, Library(), -1, TitleBlock(), PowerShown());
        });
        QCOMPARE(nothing, newImage());
    }

    void paperSizes()
    {
        Sheet sheet;
        QCOMPARE(paperRect(sheet), QRectF(0, 0, 297, 210));
        sheet.portrait = true;
        QCOMPARE(paperRect(sheet), QRectF(0, 0, 210, 297));
        sheet.paper = PaperSize::A3;
        QCOMPARE(paperRect(sheet), QRectF(0, 0, 297, 420));
        sheet.portrait = false;
        QCOMPARE(paperRect(sheet), QRectF(0, 0, 420, 297));
    }

    // The title block grows by a row for every field that is filled in.
    void titleBlockHasARowPerField()
    {
        const Schematic schematic = emptySchematic();
        // The picture's middle is put on the given point of the sheet.
        const auto renderAround = [&schematic](double x, double y, const TitleBlock &block) {
            return renderWith([&](QPainter &painter) {
                painter.translate(-x, -y);
                paintSheet(painter, schematic, Library(), 0, block, PowerShown());
            });
        };
        // The corner of the A4 frame is at 292, 205. The block is 90 mm wide
        // and ends there; a row is 6 mm high. Seen around 285, 195, the top
        // of the last row is 4 mm below the middle, the rows above it end at
        // -2 and -8.
        TitleBlock block;
        const QImage oneRow = renderAround(285, 195, block);
        QVERIFY(isColor(colorAt(oneRow, 0, 4), paintcolor::sheetFrame()));
        QVERIFY(isColor(colorAt(oneRow, 0, -2), paintcolor::background()));

        block.title = QStringLiteral("Amp");
        block.note = QStringLiteral("draft");
        const QImage threeRows = renderAround(285, 195, block);
        QVERIFY(isColor(colorAt(threeRows, 0, 4), paintcolor::sheetFrame()));
        QVERIFY(isColor(colorAt(threeRows, 0, -2), paintcolor::sheetFrame()));
        QVERIFY(isColor(colorAt(threeRows, 0, -8), paintcolor::sheetFrame()));
        QVERIFY(isColor(colorAt(threeRows, 0, -5), paintcolor::background()));
        QVERIFY(isColor(colorAt(threeRows, 0, -9.5), paintcolor::background()));

        // The texts start at the block's left side, which is at 202: seen
        // around 210, 195, the first row lies between -8 and -2.
        const QImage texts = renderAround(210, 195, block);
        QVERIFY(anyColor(texts, -7, -7, 5, -3, paintcolor::sheetFrame()));
        block.title.clear();
        const QImage twoRows = renderAround(210, 195, block);
        QVERIFY(!anyColor(twoRows, -7, -7, 5, -3, paintcolor::sheetFrame()));
    }

    void topCopperCoversBottomCopper()
    {
        Board board;
        Track bottom;
        bottom.side = BoardSide::Bottom;
        bottom.width = 1000000;
        bottom.from = Point{-8000000, -5000000};
        bottom.to = Point{-2000000, -5000000};
        board.tracks.append(bottom);
        Track top;
        top.side = BoardSide::Top;
        top.width = 1000000;
        top.from = Point{-5000000, -8000000};
        top.to = Point{-5000000, -2000000};
        // Top first in the list: the order of drawing is by side, not by list.
        board.tracks.prepend(top);

        const QImage image = render(board);
        QVERIFY(isColor(colorAt(image, -7, -5), paintcolor::padBack()));
        QVERIFY(isColor(colorAt(image, -5, -7), paintcolor::padFront()));
        QVERIFY(isColor(colorAt(image, -5, -5), paintcolor::padFront()));
        QVERIFY(isColor(colorAt(image, -7, -7), paintcolor::background()));
    }

    void viasAndMountingHoles()
    {
        Board board;
        Via via;
        via.at = Point{5000000, -5000000};
        via.drill = 1000000;
        via.size = 2000000;
        board.vias.append(via);
        MountingHole plated;
        plated.at = Point{5000000, 5000000};
        plated.drill = 2000000;
        plated.plated = true;
        plated.ring = 1000000;
        board.holes.append(plated);
        MountingHole bare;
        bare.at = Point{-5000000, 5000000};
        bare.drill = 3000000;
        board.holes.append(bare);

        const QImage image = render(board);
        QVERIFY(isColor(colorAt(image, 5.75, -5), paintcolor::padBoth()));
        QVERIFY(isColor(colorAt(image, 5, -5), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, 6.25, -5), paintcolor::background()));
        // The ring is 1 mm of copper around a hole of 2 mm.
        QVERIFY(isColor(colorAt(image, 6.5, 5), paintcolor::padBoth()));
        QVERIFY(isColor(colorAt(image, 5, 5), paintcolor::background()));
        QVERIFY(isColor(colorAt(image, 7.25, 5), paintcolor::background()));
        // A hole without plating has no copper anywhere.
        QVERIFY(!anyColor(image, -8, 2, -2, 8, paintcolor::padBoth()));
        QVERIFY(anyColor(image, -7, 4.9, -6, 5.1, paintcolor::bareHole()));
    }

    void placementIsTurnedAndFlipped()
    {
        struct Case { int rotation; BoardSide side; double x; double y; };
        const Case cases[] = {
            {0, BoardSide::Top, 3, 0},   {90, BoardSide::Top, 0, 3},
            {180, BoardSide::Top, -3, 0}, {270, BoardSide::Top, 0, -3},
            {0, BoardSide::Bottom, -3, 0},
            // Flipped first, turned after.
            {90, BoardSide::Bottom, 0, -3},
        };
        const double spots[4][2] = {{3, 0}, {0, 3}, {-3, 0}, {0, -3}};
        for (const Case &c : cases) {
            const QImage image = render(boardWithPad(c.rotation, c.side));
            // The pad's Front is the side the part is on.
            const QColor copper = c.side == BoardSide::Top ? paintcolor::padFront()
                                                          : paintcolor::padBack();
            for (const double *spot : spots) {
                const bool expected = spot[0] == c.x && spot[1] == c.y;
                QVERIFY2(isColor(colorAt(image, spot[0], spot[1]), copper) == expected,
                         qPrintable(QStringLiteral("rotation %1 bottom %2 at %3, %4")
                                        .arg(c.rotation).arg(c.side == BoardSide::Bottom)
                                        .arg(spot[0]).arg(spot[1])));
            }
        }
    }

    void designatorOnTheBoard()
    {
        Board board = boardWithPad(0, BoardSide::Top);
        board.placements[0].designator = QStringLiteral("R1");
        board.placements[0].value = QStringLiteral("10k");
        board.placements[0].refAt = Point{0, -6000000};
        board.placements[0].valueAt = Point{0, 6000000};

        // Neither is asked for.
        QVERIFY(!anyColor(render(board), -4, -8, 4, -4, paintcolor::silkscreen()));
        QVERIFY(!anyColor(render(board), -4, 4, 4, 8, paintcolor::silkscreen()));

        board.placements[0].showRef = true;
        QVERIFY(anyColor(render(board), -4, -8, 4, -4, paintcolor::silkscreen()));
        QVERIFY(!anyColor(render(board), -4, 4, 4, 8, paintcolor::silkscreen()));
        board.placements[0].showValue = true;
        QVERIFY(anyColor(render(board), -4, 4, 4, 8, paintcolor::silkscreen()));

        // On the bottom side the silkscreen has its own colour.
        board.placements[0].side = BoardSide::Bottom;
        QVERIFY(anyColor(render(board), -4, -8, 4, -4, paintcolor::silkscreenBottom()));
        QVERIFY(!anyColor(render(board), -4, -8, 4, -4, paintcolor::silkscreen()));
    }

    // The author's text on the bottom side is mirrored about its anchor.
    void bottomTextIsMirrored()
    {
        BoardGraphic item;
        item.graphic.kind = GraphicKind::Text;
        item.graphic.a = Point{0, -5000000};
        item.graphic.text = QStringLiteral("LLLL");
        item.graphic.height = 2000000;
        item.graphic.align = TextAlign::Left;

        Board top;
        top.graphics.append(item);
        const QImage topImage = render(top);
        QVERIFY(anyColor(topImage, 0.2, -7, 9, -3, paintcolor::silkscreen()));
        QVERIFY(!anyColor(topImage, -9, -7, -0.2, -3, paintcolor::silkscreen()));

        item.side = BoardSide::Bottom;
        Board bottom;
        bottom.graphics.append(item);
        const QImage bottomImage = render(bottom);
        QVERIFY(anyColor(bottomImage, -9, -7, -0.2, -3, paintcolor::silkscreenBottom()));
        QVERIFY(!anyColor(bottomImage, 0.2, -7, 9, -3, paintcolor::silkscreenBottom()));
    }

    void outlineCornersAreRounded()
    {
        // A sharp corner: the outline goes through it.
        const QImage sharp = render(boardWithOutline(0));
        QVERIFY(isColor(colorAt(sharp, 0, -8), paintcolor::boardOutline()));
        QVERIFY(isColor(colorAt(sharp, 8, 0), paintcolor::boardOutline()));
        QVERIFY(isColor(colorAt(sharp, 8, -8), paintcolor::boardOutline()));
        QVERIFY(isColor(colorAt(sharp, 0, 0), paintcolor::background()));

        // Rounded with 4 mm: the arc's middle is 4 mm from its centre at 4, -4.
        const double onArc = 4 + 4 * std::cos(PI / 4);
        const QImage round = render(boardWithOutline(4000000));
        QVERIFY(isColor(colorAt(round, 8, -8), paintcolor::background()));
        QVERIFY(isColor(colorAt(round, onArc, -onArc), paintcolor::boardOutline()));
        // The straight parts and the other corners stay.
        QVERIFY(isColor(colorAt(round, 0, -8), paintcolor::boardOutline()));
        QVERIFY(isColor(colorAt(round, 8, 0), paintcolor::boardOutline()));
        QVERIFY(isColor(colorAt(round, -8, -8), paintcolor::boardOutline()));

        // A radius too large for the sides is cut to half a side: 8 mm,
        // around the middle of the board.
        const double onLargeArc = 8 * std::cos(PI / 4);
        const QImage large = render(boardWithOutline(100000000));
        QVERIFY(isColor(colorAt(large, onLargeArc, -onLargeArc), paintcolor::boardOutline()));
        QVERIFY(isColor(colorAt(large, onArc, -onArc), paintcolor::background()));
        QVERIFY(isColor(colorAt(large, -4, -8), paintcolor::boardOutline()));
        QVERIFY(isColor(colorAt(large, 8, 4), paintcolor::boardOutline()));
    }

    void boardBoundsHoldTheBoard()
    {
        // Something to fit the view to even when the board is empty.
        const QRectF empty = boardBounds(Board(), Library());
        QVERIFY(empty.width() > 0 && empty.height() > 0);

        Board board = boardWithOutline(0);
        QCOMPARE(boardBounds(board, Library()), QRectF(-8, -8, 16, 16));

        // What sticks out of the outline counts too.
        Via via;
        via.at = Point{12000000, 0};
        via.size = 2000000;
        via.drill = 1000000;
        board.vias.append(via);
        Track track;
        track.width = 1000000;
        track.from = Point{0, 0};
        track.to = Point{0, 15000000};
        board.tracks.append(track);
        const QRectF bounds = boardBounds(board, Library());
        QVERIFY(bounds.contains(QRectF(-8, -8, 21, 23.5)));

        // A placed footprint counts where it is placed, turned as it is.
        Board placed = boardWithPad(90, BoardSide::Top);
        placed.placements[0].at = Point{20000000, 0};
        QVERIFY(boardBounds(placed, padLibrary()).contains(QPointF(20, 3)));
        QVERIFY(!boardBounds(placed, padLibrary()).contains(QPointF(0, 0)));
        // One the library lacks counts as the square drawn in its place.
        QVERIFY(boardBounds(placed, Library()).contains(QPointF(20, 0)));
    }

    // What the project library lacks is a red square with a question mark,
    // where the symbol or the footprint would be.
    void missingLibraryItemsAreMarked()
    {
        Design design = schematicWith(arrowSymbol(), 0, false);
        QVERIFY(!anyColor(render(design), -8, -8, -2, -2, paintcolor::missing()));
        design.library.symbols.clear();
        QVERIFY(anyColor(render(design), -8, -8, -2, -2, paintcolor::missing()));
        // The part itself missing.
        design = schematicWith(arrowSymbol(), 0, false);
        design.library.parts.clear();
        QVERIFY(anyColor(render(design), -8, -8, -2, -2, paintcolor::missing()));

        const Board board = boardWithPad(0, BoardSide::Top);
        QVERIFY(!anyColor(render(board), -1.8, -1.8, 1.8, 1.8, paintcolor::missing()));
        const QImage missing = render(board, Library());
        QVERIFY(anyColor(missing, -1.8, -1.8, 1.8, 1.8, paintcolor::missing()));
        // No pad is drawn.
        QVERIFY(!isColor(colorAt(missing, 3, 0), paintcolor::padFront()));
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
