#include "document.h"
#include "sheetedit.h"

#include <QSignalSpy>
#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

ObjectId id(quint64 value)
{
    return ObjectId{value};
}

// A library with a resistor: a box 5.08 by 2.03 mm and pins 2.54 long.
Library resistorLibrary()
{
    Symbol symbol;
    Graphic box;
    box.kind = GraphicKind::Rect;
    box.a = Point{-2540000, -1016000};
    box.b = Point{2540000, 1016000};
    symbol.graphics.append(box);
    Pin left;
    left.number = QStringLiteral("1");
    left.at = Point{-5080000, 0};
    left.side = PinSide::Left;
    Pin right = left;
    right.number = QStringLiteral("2");
    right.at = Point{5080000, 0};
    right.side = PinSide::Right;
    symbol.pins = {left, right};
    Library library;
    library.symbols.insert(QStringLiteral("resistor"), symbol);
    Part part;
    part.symbol = QStringLiteral("resistor");
    library.parts.insert(QStringLiteral("resistor"), part);
    return library;
}

// One object of every kind, all away from the origin.
Sheet fullSheet()
{
    Sheet sheet;
    Component component;
    component.id = id(1);
    component.part = QStringLiteral("resistor");
    component.designator = QStringLiteral("R1");
    component.at = Point{50 * MM, 50 * MM};
    component.refAt = Point{0, -3 * MM};
    component.valueAt = Point{2 * MM, 3 * MM};
    sheet.components.append(component);

    Wire wire;
    wire.id = id(2);
    wire.points = {Point{10 * MM, 10 * MM}, Point{30 * MM, 10 * MM}, Point{40 * MM, 20 * MM}};
    sheet.wires.append(wire);

    Bus bus;
    bus.id = id(3);
    bus.points = {Point{10 * MM, 80 * MM}, Point{30 * MM, 80 * MM}};
    bus.name = QStringLiteral("D[0..7]");
    sheet.buses.append(bus);

    Label label;
    label.id = id(4);
    label.at = Point{70 * MM, 10 * MM};
    label.text = QStringLiteral("CLK");
    label.rotation = 90;
    sheet.labels.append(label);

    PowerSymbol power;
    power.id = id(5);
    power.at = Point{90 * MM, 10 * MM};
    power.name = QStringLiteral("+5V");
    power.rotation = 90;
    sheet.powerSymbols.append(power);

    NoConnect mark;
    mark.id = id(6);
    mark.at = Point{90 * MM, 50 * MM};
    sheet.noConnects.append(mark);

    SheetGraphic arc;
    arc.id = id(7);
    arc.graphic.kind = GraphicKind::Arc;
    arc.graphic.a = Point{70 * MM, 70 * MM};
    arc.graphic.radius = 5 * MM;
    arc.graphic.startAngle = 10;
    arc.graphic.spanAngle = 60;
    sheet.graphics.append(arc);

    SheetGraphic text;
    text.id = id(8);
    text.graphic.kind = GraphicKind::Text;
    text.graphic.a = Point{90 * MM, 70 * MM};
    text.graphic.text = QStringLiteral("note");
    text.graphic.height = 2 * MM;
    text.graphic.align = TextAlign::Left;
    sheet.graphics.append(text);
    return sheet;
}

QSet<ObjectId> allIds()
{
    QSet<ObjectId> ids;
    for (quint64 i = 1; i <= 8; ++i)
        ids.insert(id(i));
    return ids;
}

} // namespace

class TestSheetEdit : public QObject
{
    Q_OBJECT

private slots:
    void objectsAreFoundUnderTheMouse()
    {
        const QList<SheetObjectShape> shapes = sheetObjectShapes(fullSheet(), resistorLibrary(), ComponentShown());
        // The body of the resistor, and its designator above it.
        QCOMPARE(objectAt(shapes, QPointF(50, 50), 0.5), id(1));
        QCOMPARE(objectAt(shapes, QPointF(50, 47), 0.5), id(1));
        // Near the slanted piece of the wire, not just in its rectangle.
        QCOMPARE(objectAt(shapes, QPointF(35, 15.2), 0.5), id(2));
        QVERIFY(objectAt(shapes, QPointF(32, 18), 0.5).isNull());
        QCOMPARE(objectAt(shapes, QPointF(20, 80), 0.5), id(3));
        QCOMPARE(objectAt(shapes, QPointF(90, 50), 0.5), id(6));
        // A drawing is found by its line, not inside it: on the arc, and not
        // at its centre.
        QCOMPARE(objectAt(shapes, QPointF(73.83, 73.21), 0.5), id(7));
        QVERIFY(objectAt(shapes, QPointF(70, 70), 0.5).isNull());
        // A text by its area.
        QCOMPARE(objectAt(shapes, QPointF(92, 70), 0.5), id(8));
        QVERIFY(objectAt(shapes, QPointF(5, 60), 0.5).isNull());

        // A wire over a part's rectangle is found by its line first.
        const QList<SheetObjectShape> crossing = {
            SheetObjectShape{id(11), QRectF(0, 5, 10, 0), QPolygonF({QPointF(0, 5), QPointF(10, 5)}),
                             QRectF()},
            SheetObjectShape{id(10), QRectF(0, 0, 10, 10), QPolygonF(), QRectF()}};
        QCOMPARE(objectAt(crossing, QPointF(5, 5.2), 0.5), id(11));
        QCOMPARE(objectAt(crossing, QPointF(5, 8), 0.5), id(10));

        // A component the library lacks is still found where it stands.
        const QList<SheetObjectShape> missing = sheetObjectShapes(fullSheet(), Library(), ComponentShown());
        QCOMPARE(objectAt(missing, QPointF(50, 50), 0.5), id(1));
    }

    // A power symbol is found by the drawing of its symbol, and by its net
    // name unless the symbol hides that name.
    void powerSymbolsAreFoundByTheirDrawing()
    {
        Symbol ground;
        Graphic bar;
        bar.kind = GraphicKind::Line;
        bar.a = Point{-1270000, 2540000};
        bar.b = Point{1270000, 2540000};
        ground.graphics = {bar};
        Pin pin;
        pin.number = QStringLiteral("1");
        pin.side = PinSide::Up;
        pin.length = 2540000;
        ground.pins = {pin};
        ground.valueAt = Point{0, 5 * MM};
        ground.power = QStringLiteral("GND");
        ground.hidePowerName = true;
        Library library;
        library.symbols.insert(QStringLiteral("ground"), ground);

        Sheet sheet;
        PowerSymbol power;
        power.id = id(1);
        power.at = Point{20 * MM, 20 * MM};
        power.name = QStringLiteral("GND");
        power.symbol = QStringLiteral("ground");
        sheet.powerSymbols = {power};
        const QRectF hidden = sheetObjectShapes(sheet, library, ComponentShown()).first().rect;
        QCOMPARE(objectAt(sheetObjectShapes(sheet, library, ComponentShown()), QPointF(20, 22.54), 0.5), id(1));
        // The drawing reaches down to the bar, and no further.
        QVERIFY(hidden.bottom() < 23.5);

        sheet.powerSymbols[0].name = QStringLiteral("AGND");
        const QRectF shown = sheetObjectShapes(sheet, library, ComponentShown()).first().rect;
        QVERIFY(shown.bottom() > 25);

        // Turned over, the drawing goes up instead.
        sheet.powerSymbols[0].rotation = 180;
        QVERIFY(sheetObjectShapes(sheet, library, ComponentShown()).first().rect.top() < 15.5);

        // Placed, its symbol is copied into the project, which keeps its own.
        Library project;
        QCOMPARE(withSymbolFrom(project, library, QStringLiteral("ground")).symbols.keys(),
                 QStringList({QStringLiteral("ground")}));
        Symbol own = ground;
        own.power = QStringLiteral("0V");
        project.symbols.insert(QStringLiteral("ground"), own);
        QCOMPARE(withSymbolFrom(project, library, QStringLiteral("ground")).symbols.value(
                     QStringLiteral("ground")).power,
                 QStringLiteral("0V"));
        QVERIFY(withSymbolFrom(Library(), library, QStringLiteral("absent")).symbols.isEmpty());
    }

    void powerNamesAreSetTrimmed()
    {
        const Sheet sheet = fullSheet();
        const Sheet renamed = withPowerName(sheet, {id(5)}, QStringLiteral(" +12V "));
        QCOMPARE(renamed.powerSymbols.at(0).name, QStringLiteral("+12V"));
        // Only power symbols: the component's id changes nothing.
        QVERIFY(withPowerName(sheet, {id(1)}, QStringLiteral("X")) == sheet);
        // A blank name is no name.
        QVERIFY(withPowerName(sheet, {id(5)}, QStringLiteral("  ")) == sheet);
    }

    void labelTextsAreSetAndNumberedOn()
    {
        const Sheet sheet = fullSheet();
        QCOMPARE(withLabelText(sheet, {id(4)}, QStringLiteral(" DATA ")).labels.at(0).text,
                 QStringLiteral("DATA"));
        QVERIFY(withLabelText(sheet, {id(4)}, QStringLiteral(" ")) == sheet);
        QVERIFY(withLabelText(sheet, {id(5)}, QStringLiteral("X")) == sheet);

        QCOMPARE(nextLabelText(QStringLiteral("D0")), QStringLiteral("D1"));
        QCOMPARE(nextLabelText(QStringLiteral("D9")), QStringLiteral("D10"));
        QCOMPARE(nextLabelText(QStringLiteral("A09")), QStringLiteral("A10"));
        QCOMPARE(nextLabelText(QStringLiteral("7")), QStringLiteral("8"));
        QCOMPARE(nextLabelText(QStringLiteral("CLK")), QStringLiteral("CLK"));
        QCOMPARE(nextLabelText(QString()), QString());
    }

    // A segment of a bus is cut out or copied as one of a wire is.
    void busSegmentsAreCutAndCopied()
    {
        Sheet sheet;
        Bus bus;
        bus.id = id(1);
        bus.points = {Point{0, 0}, Point{10 * MM, 0}, Point{10 * MM, 10 * MM}, Point{20 * MM, 10 * MM}};
        bus.name = QStringLiteral("D[0..7]");
        sheet.buses = {bus};
        QCOMPARE(linePoints(sheet, id(1)), bus.points);
        QCOMPARE(segmentAt(bus.points, QPointF(10.2, 5)), 1);

        const Sheet cut = withoutSegment(sheet, id(1), 1);
        QCOMPARE(int(cut.buses.size()), 2);
        QCOMPARE(cut.buses.at(0).id, id(1));
        QCOMPARE(cut.buses.at(0).points, QList<Point>({Point{0, 0}, Point{10 * MM, 0}}));
        QCOMPARE(cut.buses.at(1).points,
                 QList<Point>({Point{10 * MM, 10 * MM}, Point{20 * MM, 10 * MM}}));
        QCOMPARE(cut.buses.at(1).name, bus.name);

        const Sheet piece = segmentOf(sheet, id(1), 2);
        QVERIFY(piece.wires.isEmpty());
        QCOMPARE(int(piece.buses.size()), 1);
        QCOMPARE(piece.buses.at(0).points,
                 QList<Point>({Point{10 * MM, 10 * MM}, Point{20 * MM, 10 * MM}}));
    }

    // The name of a bus is chosen and moved alone, as a label's text is.
    void busNamesAreMovedAlone()
    {
        Sheet sheet;
        Bus bus;
        bus.id = id(1);
        bus.points = {Point{20 * MM, 20 * MM}, Point{40 * MM, 20 * MM}};
        bus.name = QStringLiteral("D[0..7]");
        sheet.buses = {bus};
        const TextHit hit = textAt(sheet, Library(), ComponentShown(), busNameRect(bus, 0).center());
        QCOMPARE(hit.owner, id(1));
        QCOMPARE(textOwnerPoint(sheet, hit), bus.points.first());
        const Point delta{5 * MM, -3 * MM};
        const Sheet moved = withTextMoved(sheet, hit, delta);
        QCOMPARE(moved.buses.at(0).points, bus.points);
        QCOMPARE(moved.buses.at(0).namesAt, QList<Point>({delta}));
        QCOMPARE(textPoint(moved, hit), bus.points.first() + delta);
        QCOMPARE(textRect(moved, Library(), ComponentShown(), hit), busNameRect(bus, 0).translated(5, -3));

        // Shown once more at the far end: each place is found, moved and
        // deleted alone, but the last one stays.
        const Sheet twice = withBusNameAdded(sheet, id(1), bus.points.last());
        QCOMPARE(twice.buses.at(0).namesAt,
                 QList<Point>({Point(), bus.points.last() - bus.points.first()}));
        const TextHit far = textAt(twice, Library(), ComponentShown(), busNameRect(twice.buses.at(0), 1).center());
        QCOMPARE(far.index, 1);
        QCOMPARE(withTextMoved(twice, far, delta).buses.at(0).namesAt.at(1),
                 twice.buses.at(0).namesAt.at(1) + delta);
        QVERIFY(isBusNameRemovable(twice, far));
        const Sheet once = withoutBusName(twice, far);
        QCOMPARE(once.buses.at(0).namesAt, QList<Point>({Point()}));
        QVERIFY(!isBusNameRemovable(once, hit));
        QVERIFY(withoutBusName(once, hit) == once);
    }

    void busesAreNamed()
    {
        Schematic schematic;
        schematic.sheets = {Sheet(), Sheet()};
        QCOMPARE(nextBusName(schematic), QStringLiteral("BUS1"));
        Bus bus;
        bus.name = QStringLiteral("BUS1");
        schematic.sheets[0].buses.append(bus);
        bus.name = QStringLiteral("BUS3");
        schematic.sheets[1].buses.append(bus);
        bus.name = QStringLiteral("D[0..7]");
        schematic.sheets[1].buses.append(bus);
        QCOMPARE(nextBusName(schematic), QStringLiteral("BUS2"));

        const Sheet sheet = fullSheet();
        QCOMPARE(withBusName(sheet, {id(3)}, QStringLiteral(" A[0..3] ")).buses.at(0).name,
                 QStringLiteral("A[0..3]"));
        QVERIFY(withBusName(sheet, {id(3)}, QStringLiteral(" ")) == sheet);
        // A branch is renamed with the bus it branches off.
        Sheet branched = sheet;
        Bus branch = branched.buses.at(0);
        branch.id = id(30);
        branch.points = {Point{20 * MM, 80 * MM}, Point{20 * MM, 90 * MM}};
        branched.buses.append(branch);
        const Sheet renamed = withBusName(branched, {id(30)}, QStringLiteral("A[0..3]"));
        QCOMPARE(renamed.buses.at(0).name, QStringLiteral("A[0..3]"));
        QCOMPARE(renamed.buses.at(1).name, QStringLiteral("A[0..3]"));
    }

    // Two parts side by side, the texts of the second reaching over the
    // drawing of the first: a click on the drawing takes the first.
    void aClickOnTheDrawingTakesItsPart()
    {
        const QList<SheetObjectShape> shapes = {
            SheetObjectShape{id(20), QRectF(0, 0, 30, 10), QPolygonF(), QRectF(0, 0, 10, 10)},
            SheetObjectShape{id(21), QRectF(5, 0, 20, 10), QPolygonF(), QRectF(15, 0, 10, 10)}};
        QCOMPARE(objectAt(shapes, QPointF(7, 5), 0.1), id(20));
        QCOMPARE(objectAt(shapes, QPointF(20, 5), 0.1), id(21));
        // On the texts alone, the topmost.
        QCOMPARE(objectAt(shapes, QPointF(12, 5), 0.1), id(21));
    }

    void rectangleTakesWhatIsWhollyInside()
    {
        const QList<SheetObjectShape> shapes = sheetObjectShapes(fullSheet(), resistorLibrary(), ComponentShown());
        QCOMPARE(objectsInside(shapes, QRectF(5, 5, 40, 20)), QSet<ObjectId>({id(2)}));
        // Half of the wire is not enough.
        QVERIFY(objectsInside(shapes, QRectF(5, 5, 20, 20)).isEmpty());
        // A straight bus has no height, and is still inside.
        QCOMPARE(objectsInside(shapes, QRectF(5, 75, 30, 10)), QSet<ObjectId>({id(3)}));
        // A component needs its drawing inside, not the texts beside it.
        QCOMPARE(objectsInside(shapes, QRectF(44, 48.5, 12, 3)), QSet<ObjectId>({id(1)}));
        QVERIFY(selectionBounds(shapes, QSet<ObjectId>()).isEmpty());
        QCOMPARE(selectionBounds(shapes, {id(2), id(3)}), QRectF(10, 10, 30, 70));
    }

    void movingTakesOnlyTheSelection()
    {
        const Sheet sheet = fullSheet();
        const Point delta{5 * MM, -10 * MM};
        const Sheet moved = movedObjects(sheet, {id(1), id(2)}, delta);
        QCOMPARE(moved.components.at(0).at, sheet.components.at(0).at + delta);
        // Texts move with the component: their places are relative to it.
        QCOMPARE(moved.components.at(0).refAt, sheet.components.at(0).refAt);
        QCOMPARE(moved.wires.at(0).points.at(2), sheet.wires.at(0).points.at(2) + delta);
        QVERIFY(moved.buses == sheet.buses);
        QVERIFY(moved.labels == sheet.labels);

        const Sheet all = movedObjects(sheet, allIds(), delta);
        QCOMPARE(all.labels.at(0).at, sheet.labels.at(0).at + delta);
        QCOMPARE(all.graphics.at(0).graphic.a, sheet.graphics.at(0).graphic.a + delta);
    }

    // A locked component is not moved, turned or mirrored, its texts are;
    // a piece copied of it is not locked.
    void lockedObjectsStay()
    {
        const Sheet sheet = withObjectsLocked(fullSheet(), {id(1)}, true);
        QVERIFY(sheet.components.at(0).locked);
        QVERIFY(areObjectsLocked(sheet, {id(1), id(2)}));
        QVERIFY(!areObjectsLocked(sheet, {id(2)}));
        QVERIFY(hasLockable(sheet, {id(1)}));
        QVERIFY(!hasLockable(sheet, {id(2)}));
        QCOMPARE(movableIds(sheet, {id(1), id(2)}), QSet<ObjectId>{id(2)});
        const Point delta{5 * MM, -10 * MM};
        const Sheet moved = movedObjects(sheet, {id(1), id(2)}, delta);
        QCOMPARE(moved.components.at(0).at, sheet.components.at(0).at);
        QCOMPARE(moved.wires.at(0).points.at(0), sheet.wires.at(0).points.at(0) + delta);
        QVERIFY(rotatedObjects(sheet, {id(1)}, Point()) == sheet);
        QVERIFY(mirroredObjects(sheet, {id(1)}, Point()) == sheet);
        QVERIFY(!objectsOf(sheet, {id(1)}).components.at(0).locked);
        QVERIFY(withObjectsLocked(sheet, {id(1)}, false) == fullSheet());
    }

    void componentTextsAreFoundAndMovedAlone()
    {
        Library library = resistorLibrary();
        Sheet sheet = fullSheet();
        // R1 at (50, 50): its designator at (50, 47), its value at (52, 53).
        QCOMPARE(textAt(sheet, library, ComponentShown(), QPointF(50, 47)).owner, id(1));
        QVERIFY(textAt(sheet, library, ComponentShown(), QPointF(50, 47)).text == ComponentText::Designator);
        // No value: its mark is found where it stands.
        QCOMPARE(textAt(sheet, library, ComponentShown(), QPointF(52, 53)).owner, id(1));
        QVERIFY(textAt(sheet, library, ComponentShown(), QPointF(52, 53)).text == ComponentText::Value);
        // A part that needs no value shows no mark, and none is found.
        library.parts[QStringLiteral("resistor")].noValue = true;
        QVERIFY(textAt(sheet, library, ComponentShown(), QPointF(52, 53)).owner.isNull());
        QVERIFY(componentTextShown(sheet.components.at(0), ComponentText::Value, library, ComponentShown())
                    .isEmpty());
        library.parts[QStringLiteral("resistor")].noValue = false;
        // No designator: nothing is found.
        sheet.components[0].designator.clear();
        QVERIFY(textAt(sheet, library, ComponentShown(), QPointF(50, 47)).owner.isNull());
        sheet = withComponentValue(sheet, {id(1)}, QStringLiteral(" 10k "));
        QCOMPARE(sheet.components.at(0).value, QStringLiteral("10k"));
        sheet.components[0].designator = QStringLiteral("R1");
        QCOMPARE(textAt(sheet, library, ComponentShown(), QPointF(52, 53)).owner, id(1));
        QVERIFY(textAt(sheet, library, ComponentShown(), QPointF(52, 53)).text == ComponentText::Value);
        QVERIFY(textAt(sheet, library, ComponentShown(), QPointF(80, 80)).owner.isNull());
        // The component's own point, between its texts, is not a text.
        QVERIFY(textAt(sheet, library, ComponentShown(), QPointF(50, 50)).owner.isNull());

        const Point delta{2 * MM, -1 * MM};
        const Sheet moved = withTextMoved(sheet, TextHit{id(1), ComponentText::Value}, delta);
        QCOMPARE(moved.components.at(0).valueAt, sheet.components.at(0).valueAt + delta);
        QCOMPARE(moved.components.at(0).refAt, sheet.components.at(0).refAt);
        QCOMPARE(moved.components.at(0).at, sheet.components.at(0).at);
        QVERIFY(moved.wires == sheet.wires);
        QCOMPARE(textPoint(moved, TextHit{id(1), ComponentText::Value}),
                 sheet.components.at(0).at + sheet.components.at(0).valueAt + delta);
        QCOMPARE(textOwnerPoint(moved, TextHit{id(1), ComponentText::Value}),
                 sheet.components.at(0).at);
    }

    // A label's text is chosen and moved alone the same way; its point
    // stays, and the text goes round with the label when it turns.
    void labelTextsAreFoundAndMovedAlone()
    {
        Sheet sheet;
        Label label;
        label.id = id(1);
        label.at = Point{20 * MM, 20 * MM};
        label.text = QStringLiteral("DATA");
        sheet.labels = {label};
        const TextHit hit = textAt(sheet, Library(), ComponentShown(), QPointF(21, 19));
        QCOMPARE(hit.owner, id(1));
        QVERIFY(hasText(sheet, hit));
        QCOMPARE(textPoint(sheet, hit), label.at);
        // Below the wire is no text.
        QVERIFY(textAt(sheet, Library(), ComponentShown(), QPointF(21, 21)).owner.isNull());

        const Point delta{0, -3 * MM};
        const Sheet moved = withTextMoved(sheet, hit, delta);
        QCOMPARE(moved.labels.at(0).at, label.at);
        QCOMPARE(moved.labels.at(0).textAt, delta);
        QCOMPARE(textPoint(moved, hit), label.at + delta);
        QCOMPARE(textOwnerPoint(moved, hit), label.at);
        QVERIFY(textRect(moved, Library(), ComponentShown(), hit).bottom() < 17.5);
        // The old place no longer holds it; the new one does.
        QVERIFY(textAt(moved, Library(), ComponentShown(), QPointF(21, 19)).owner.isNull());
        QCOMPARE(textAt(moved, Library(), ComponentShown(), QPointF(21, 16)).owner, id(1));

        // A quarter turn about its point takes the text round with it.
        const Sheet turned = rotatedObjects(moved, {id(1)}, label.at);
        QCOMPARE(turned.labels.at(0).at, label.at);
        QCOMPARE(turned.labels.at(0).textAt, Point({3 * MM, 0}));
        QVERIFY(!hasText(sheet, TextHit{id(9), ComponentText::Designator}));
    }

    void componentPropertiesAreSet()
    {
        Sheet sheet = fullSheet();
        Component second = sheet.components.first();
        second.id = id(20);
        second.designator = QStringLiteral("R2");
        sheet.components.append(second);
        const QSet<ObjectId> both{id(1), id(20)};

        Sheet edited = withComponentValue(sheet, both, QStringLiteral("1k"));
        QCOMPARE(edited.components.at(0).value, QStringLiteral("1k"));
        QCOMPARE(edited.components.at(1).value, QStringLiteral("1k"));
        QVERIFY(edited.wires == sheet.wires);

        edited = withComponentFootprint(sheet, {id(20)}, QStringLiteral("resistor-vertical"));
        QVERIFY(edited.components.at(0).footprint.isEmpty());
        QCOMPARE(edited.components.at(1).footprint, QStringLiteral("resistor-vertical"));

        edited = withComponentOffBoard(sheet, both, true);
        QVERIFY(edited.components.at(0).offBoard && edited.components.at(1).offBoard);

        edited = withComponentDesignator(sheet, {id(20)}, QStringLiteral(" R7 "));
        QCOMPARE(edited.components.at(1).designator, QStringLiteral("R7"));
        // An empty designator is not taken.
        QVERIFY(withComponentDesignator(sheet, {id(20)}, QStringLiteral("  ")) == sheet);

        Schematic schematic;
        schematic.sheets = {sheet, Sheet()};
        QVERIFY(isDesignatorTaken(schematic, Library(), QStringLiteral("R1"), id(20)));
        // A component does not take its own designator.
        QVERIFY(!isDesignatorTaken(schematic, Library(), QStringLiteral("R1"), id(1)));
        QVERIFY(!isDesignatorTaken(schematic, Library(), QStringLiteral("R3"), id(20)));
    }

    void dragsLandAnObjectsOwnPointOnTheGrid()
    {
        const Sheet sheet = fullSheet();
        QCOMPARE(dragAnchor(sheet, id(1), QPointF(52, 49)), sheet.components.at(0).at);
        // The wire's point nearest to where it was taken.
        QCOMPARE(dragAnchor(sheet, id(2), QPointF(29, 11)), (Point{30 * MM, 10 * MM}));
        QCOMPARE(dragAnchor(sheet, id(2), QPointF(12, 10)), (Point{10 * MM, 10 * MM}));
        QCOMPARE(dragAnchor(sheet, sheet.labels.at(0).id, QPointF(0, 0)), sheet.labels.at(0).at);
        // Nothing there: the point itself.
        QCOMPARE(dragAnchor(sheet, ObjectId(), QPointF(1.5, 2)), (Point{1500000, 2 * MM}));
    }

    void turningIsClockwiseAndComesBackAfterFour()
    {
        const Sheet sheet = fullSheet();
        const Point center{50 * MM, 50 * MM};
        const Sheet once = rotatedObjects(sheet, allIds(), center);
        // What was to the right of the centre goes below it.
        QCOMPARE(once.components.at(0).at, center);
        QCOMPARE(once.components.at(0).rotation, 90);
        QCOMPARE(once.components.at(0).refAt, Point({3 * MM, 0}));
        QCOMPARE(once.noConnects.at(0).at, Point({50 * MM, 90 * MM}));
        QCOMPARE(once.labels.at(0).rotation, 180);
        QCOMPARE(once.graphics.at(0).graphic.startAngle, 100);

        Sheet turned = sheet;
        for (int i = 0; i < 4; ++i)
            turned = rotatedObjects(turned, allIds(), center);
        QVERIFY(turned == sheet);
    }

    void mirroringFlipsAndComesBackAfterTwo()
    {
        const Sheet sheet = fullSheet();
        const Point center{60 * MM, 0};
        const Sheet once = mirroredObjects(sheet, allIds(), center);
        QCOMPARE(once.components.at(0).at, Point({70 * MM, 50 * MM}));
        QVERIFY(once.components.at(0).mirrored);
        QCOMPARE(once.components.at(0).valueAt, Point({-2 * MM, 3 * MM}));
        QCOMPARE(once.wires.at(0).points.at(0), Point({110 * MM, 10 * MM}));
        // A label pointing down still points down, a power symbol pointing
        // right now points left.
        QCOMPARE(once.labels.at(0).rotation, 90);
        QCOMPARE(once.powerSymbols.at(0).rotation, 270);
        // The arc from 10 to 70 degrees becomes the one from 110 to 170.
        QCOMPARE(once.graphics.at(0).graphic.startAngle, 110);
        QVERIFY(once.graphics.at(1).graphic.align == TextAlign::Right);

        QVERIFY(mirroredObjects(once, allIds(), center) == sheet);

        // A turned component mirrored turns the other way.
        Sheet turned = rotatedObjects(sheet, {id(1)}, sheet.components.at(0).at);
        turned = mirroredObjects(turned, {id(1)}, turned.components.at(0).at);
        QCOMPARE(turned.components.at(0).rotation, 270);
    }

    void deletingTakesOnlyTheSelection()
    {
        const Sheet sheet = fullSheet();
        const Sheet rest = withoutObjects(sheet, {id(2), id(4), id(8), id(99)});
        QVERIFY(rest.wires.isEmpty());
        QVERIFY(rest.labels.isEmpty());
        QCOMPARE(rest.graphics.size(), 1);
        QCOMPARE(rest.components.size(), 1);
        QVERIFY(withoutObjects(sheet, allIds()) == Sheet());
        QCOMPARE(withComponentComment(sheet, {id(1)}, QStringLiteral(" 1% ")).components.at(0).comment,
                 QStringLiteral("1%"));
        // A rated power goes to a part that has one only.
        Library powered = resistorLibrary();
        powered.parts[QStringLiteral("resistor")].powerMw = 125;
        QCOMPARE(withComponentPower(sheet, {id(1)}, 2000, powered).components.at(0).powerMw, 2000);
        QVERIFY(withComponentPower(sheet, {id(1)}, 2000, resistorLibrary()) == sheet);
        QCOMPARE(newComponent(powered, QStringLiteral("resistor"), QString()).powerMw, 125);

        // The no-connect mark on a pin of a part goes with the part, unless
        // a pin of another part is there too.
        const Library library = resistorLibrary();
        Sheet marked = sheet;
        NoConnect onPin;
        onPin.id = id(40);
        onPin.at = Point{50 * MM - 5080000, 50 * MM};
        marked.noConnects.append(onPin);
        QCOMPARE(marksOnPinsOf(marked, {id(1)}, library), QSet<ObjectId>({id(40)}));
        QVERIFY(marksOnPinsOf(marked, {id(2)}, library).isEmpty());
        Component other = marked.components.at(0);
        other.id = id(41);
        other.at = Point{50 * MM - 2 * 5080000, 50 * MM};
        marked.components.append(other);
        QVERIFY(marksOnPinsOf(marked, {id(1)}, library).isEmpty());
    }

    void piecesAreCopiedAndPasted()
    {
        const Sheet sheet = fullSheet();
        const Sheet piece = objectsOf(sheet, {id(1), id(2), id(8), id(99)});
        QCOMPARE(objectIds(piece), QSet<ObjectId>({id(1), id(2), id(8)}));
        QVERIFY(piece.components.first() == sheet.components.first());
        QVERIFY(piece.wires.first() == sheet.wires.first());
        QVERIFY(objectsOf(sheet, {}) == withoutObjects(sheet, allIds()));
        QCOMPARE(objectIds(sheet), allIds());

        // New ids, all different, and nothing else changed.
        const Sheet renewed = withNewIds(piece);
        const QSet<ObjectId> ids = objectIds(renewed);
        QCOMPARE(ids.size(), 3);
        QVERIFY(!ids.intersects(allIds()));
        QCOMPARE(renewed.components.first().at, piece.components.first().at);
        QVERIFY(renewed.wires.first().points == piece.wires.first().points);

        const Sheet added = withObjectsAdded(sheet, renewed);
        QCOMPARE(added.components.size(), 2);
        QCOMPARE(added.wires.size(), 2);
        QCOMPARE(added.graphics.size(), 3);
        QCOMPARE(objectIds(added), allIds() + ids);
        QVERIFY(withoutObjects(added, ids) == sheet);
    }

    void pastedComponentsAreNumberedOn()
    {
        Library library = resistorLibrary();
        library.parts[QStringLiteral("resistor")].prefix = QStringLiteral("R");
        library.parts[QStringLiteral("resistor")].prefixGost = QStringLiteral("RG");
        Schematic schematic;
        schematic.sheets.append(fullSheet());

        Sheet piece;
        Component r = fullSheet().components.first();
        piece.components = {r, r};
        Component missing = r;
        missing.part = QStringLiteral("nothing");
        missing.designator = QStringLiteral("X7");
        piece.components.append(missing);

        const Sheet numbered = numberedOn(piece, schematic, library, DesignatorStyle::International);
        // R1 is taken: the two copies take R2 and R3, not both R2.
        QCOMPARE(numbered.components.at(0).designator, QStringLiteral("R2"));
        QCOMPARE(numbered.components.at(1).designator, QStringLiteral("R3"));
        QCOMPARE(numbered.components.at(2).designator, QStringLiteral("X7"));
        QCOMPARE(numberedOn(piece, schematic, library, DesignatorStyle::Gost)
                     .components.at(0).designator,
                 QStringLiteral("RG1"));
    }

    // What differs is found per part used; the update takes the part with
    // its symbol and footprints, gives power to components with none and a
    // listed footprint to those whose footprint is gone.
    void partsAreUpdatedFromTheLibrary()
    {
        Design design;
        design.library = resistorLibrary();
        Footprint oldFootprint;
        oldFootprint.pads.append(Pad());
        design.library.footprints.insert(QStringLiteral("old"), oldFootprint);
        design.library.parts[QStringLiteral("resistor")].footprints = {QStringLiteral("old")};
        Component used = fullSheet().components.first();
        used.footprint = QStringLiteral("old");
        Component powered = used;
        powered.id = id(50);
        powered.powerMw = 2000;
        Sheet sheet;
        sheet.components = {used, powered};
        design.schematic.sheets = {sheet};

        // The same library: nothing differs.
        QVERIFY(partDifferences(design, design.library).isEmpty());

        Library source = design.library;
        Part &part = source.parts[QStringLiteral("resistor")];
        part.powerMw = 125;
        part.footprints = {QStringLiteral("new")};
        source.footprints.insert(QStringLiteral("new"), Footprint());
        QList<PartDifference> differences = partDifferences(design, source);
        QCOMPARE(differences.size(), 1);
        QCOMPARE(differences.first().part, QStringLiteral("resistor"));
        QVERIFY(differences.first().fields);
        QVERIFY(differences.first().footprints);
        QVERIFY(!differences.first().drawing);
        QVERIFY(!differences.first().pins);

        // A pin moved is told apart from the drawing alone.
        source.symbols[QStringLiteral("resistor")].pins[0].at.x -= 2540000;
        differences = partDifferences(design, source);
        QVERIFY(differences.first().drawing && differences.first().pins);
        source.symbols[QStringLiteral("resistor")].pins[0].at.x += 2540000;
        source.symbols[QStringLiteral("resistor")].pins[0].name = QStringLiteral("A");
        differences = partDifferences(design, source);
        QVERIFY(differences.first().drawing && !differences.first().pins);

        const Design updated = withPartsUpdated(design, source, {QStringLiteral("resistor")});
        QVERIFY(updated.library.parts.value(QStringLiteral("resistor")) == part);
        QVERIFY(updated.library.symbols.value(QStringLiteral("resistor"))
                == source.symbols.value(QStringLiteral("resistor")));
        QVERIFY(updated.library.footprints.contains(QStringLiteral("new")));
        const QList<Component> &components = updated.schematic.sheets.at(0).components;
        QCOMPARE(components.at(0).powerMw, 125);
        QCOMPARE(components.at(1).powerMw, 2000);
        QCOMPARE(components.at(0).footprint, QStringLiteral("new"));
        QVERIFY(partDifferences(updated, source).isEmpty());
        // A part not chosen stays.
        QVERIFY(withPartsUpdated(design, source, {}) == design);
    }

    // Outside GOST a power not the common one follows the value as text,
    // and the value's rectangle grows with it.
    void powerFollowsTheValueOutsideGost()
    {
        const Library library = resistorLibrary();
        Component component = fullSheet().components.first();
        component.value = QStringLiteral("10k");
        component.powerMw = 500;
        const ComponentShown marks{DesignatorStyle::Gost, 125};
        const ComponentShown text{DesignatorStyle::International, 125};
        const ComponentShown common{DesignatorStyle::International, 500};
        const auto shown = [&](const ComponentShown &power) {
            return componentTextShown(component, ComponentText::Value, library, power);
        };
        QCOMPARE(shown(marks), QStringLiteral("10k"));
        QCOMPARE(shown(text), QStringLiteral("10k ") + formatPower(500));
        QCOMPARE(shown(common), QStringLiteral("10k"));
        QVERIFY(componentTextRect(component, ComponentText::Value, library, text).width()
                > componentTextRect(component, ComponentText::Value, library, marks).width());
        component.value.clear();
        QVERIFY(shown(text).endsWith(QLatin1Char(' ') + formatPower(500)));
        QVERIFY(shown(text).size() > formatPower(500).size() + 1);

        Project project;
        Schematic schematic;
        Sheet sheet;
        sheet.components = {component, component, fullSheet().components.first()};
        sheet.components[2].powerMw = 250;
        schematic.sheets = {sheet};
        project.designatorStyle = DesignatorStyle::Gost;
        QVERIFY(componentShown(project, schematic).powerMarks());
        project.designatorStyle = DesignatorStyle::International;
        QVERIFY(!componentShown(project, schematic).powerMarks());
        QCOMPARE(componentShown(project, schematic).commonMw, 500);
    }

    // The prefix changes and the number stays; a designator the change
    // would repeat gets the first free number; the others stay.
    void designatorStyleRenamesWithNumbersKept()
    {
        Library library;
        const auto addPart = [&library](const char *name, const char *prefix, const char *gost) {
            Part part;
            part.prefix = QLatin1String(prefix);
            part.prefixGost = QLatin1String(gost);
            library.parts.insert(QLatin1String(name), part);
        };
        addPart("resistor", "R", "R");
        addPart("trimmer", "RV", "R");
        addPart("diode", "D", "VD");
        const auto component = [](quint64 value, const char *part, const char *designator) {
            Component result;
            result.id = id(value);
            result.part = QLatin1String(part);
            result.designator = QLatin1String(designator);
            return result;
        };
        Schematic schematic;
        Sheet first;
        first.components = {component(1, "resistor", "R1"), component(2, "trimmer", "RV1"),
                            component(3, "diode", "D3a"), component(4, "diode", "Power")};
        Sheet second;
        second.components = {component(5, "trimmer", "RV2"), component(6, "resistor", "R1"),
                             component(7, "nothing", "D9"), component(8, "diode", "D10")};
        schematic.sheets = {first, second};

        const Schematic gost = withDesignatorStyle(schematic, library, DesignatorStyle::International,
                                                   DesignatorStyle::Gost);
        const auto designators = [](const Schematic &s) {
            QStringList result;
            for (const Sheet &sheet : s.sheets) {
                for (const Component &c : sheet.components)
                    result.append(c.designator);
            }
            return result;
        };
        // R1 stays twice, as it was; RV1 would be R1 and takes R3, after R2
        // that RV2 became.
        QCOMPARE(designators(gost),
                 QStringList({QStringLiteral("R1"), QStringLiteral("R3"), QStringLiteral("VD3a"),
                              QStringLiteral("Power"), QStringLiteral("R2"), QStringLiteral("R1"),
                              QStringLiteral("D9"), QStringLiteral("VD10")}));

        // Back: the diodes get their prefix back, the trimmers become RV
        // with the numbers they got.
        const Schematic back = withDesignatorStyle(gost, library, DesignatorStyle::Gost,
                                                   DesignatorStyle::International);
        QCOMPARE(designators(back),
                 QStringList({QStringLiteral("R1"), QStringLiteral("RV3"), QStringLiteral("D3a"),
                              QStringLiteral("Power"), QStringLiteral("RV2"), QStringLiteral("R1"),
                              QStringLiteral("D9"), QStringLiteral("D10")}));
    }

    // The units of a part of several keep one designator when the style
    // changes, also when theirs is taken and they get a new number, and
    // when pasted.
    void unitsKeepOneDesignator()
    {
        Library library;
        Part dual;
        dual.symbol = QStringLiteral("a");
        dual.units = {QStringLiteral("b")};
        dual.prefix = QStringLiteral("U");
        dual.prefixGost = QStringLiteral("DA");
        library.parts.insert(QStringLiteral("dual"), dual);
        Part chip = dual;
        chip.units.clear();
        chip.prefix = QStringLiteral("DA");
        library.parts.insert(QStringLiteral("chip"), chip);
        const auto component = [](quint64 value, const char *part, const char *designator,
                                  int unit) {
            Component result;
            result.id = id(value);
            result.part = QLatin1String(part);
            result.designator = QLatin1String(designator);
            result.unit = unit;
            return result;
        };
        Schematic schematic;
        Sheet sheet;
        // U1 would be DA1, a chip's: both its units get DA3, after DA2 that
        // U2 became.
        sheet.components = {component(1, "chip", "DA1", 0), component(2, "dual", "U1", 0),
                            component(3, "dual", "U1", 1), component(4, "dual", "U2", 1),
                            component(5, "dual", "U2", 0)};
        schematic.sheets = {sheet};
        const Schematic gost = withDesignatorStyle(schematic, library, DesignatorStyle::International,
                                                   DesignatorStyle::Gost);
        QStringList designators;
        for (const Component &c : gost.sheets.first().components)
            designators.append(c.designator);
        QCOMPARE(designators, QStringList({QStringLiteral("DA1"), QStringLiteral("DA3"),
                                           QStringLiteral("DA3"), QStringLiteral("DA2"),
                                           QStringLiteral("DA2")}));

        Sheet piece;
        piece.components = {component(6, "dual", "U1", 0), component(7, "dual", "U1", 1),
                            component(8, "dual", "U2", 0)};
        const Sheet pasted = numberedOn(piece, schematic, library, DesignatorStyle::International);
        QCOMPARE(pasted.components.at(0).designator, QStringLiteral("U3"));
        QCOMPARE(pasted.components.at(1).designator, QStringLiteral("U3"));
        QCOMPARE(pasted.components.at(2).designator, QStringLiteral("U4"));
    }

    // The units no component is, and a row of them with the model's fields.
    void missingUnitsStandInARow()
    {
        Library library;
        Symbol symbol;
        Pin pin;
        pin.number = QStringLiteral("1");
        pin.at = Point{-PIN_GRID, 0};
        symbol.pins = {pin};
        library.symbols.insert(QStringLiteral("a"), symbol);
        Part part;
        part.symbol = QStringLiteral("a");
        part.units = {QStringLiteral("a"), QStringLiteral("a")};
        library.parts.insert(QStringLiteral("triple"), part);
        Component first;
        first.id = id(1);
        first.part = QStringLiteral("triple");
        first.designator = QStringLiteral("U1");
        first.value = QStringLiteral("x");
        first.unit = 1;
        Schematic schematic;
        schematic.sheets = {Sheet()};
        schematic.sheets[0].components = {first};
        QCOMPARE(missingUnits(schematic, library, id(1)), QList<int>({0, 2}));
        QVERIFY(missingUnits(schematic, library, id(9)).isEmpty());

        const Sheet row = unitsInARow(library, first, {0, 2});
        QCOMPARE(row.components.size(), 2);
        QCOMPARE(row.components.at(0).unit, 0);
        QCOMPARE(row.components.at(1).unit, 2);
        QCOMPARE(row.components.at(1).designator, QStringLiteral("U1"));
        QCOMPARE(row.components.at(1).value, QStringLiteral("x"));
        QVERIFY(row.components.at(0).id != first.id);
        QCOMPARE(row.components.at(0).at, Point());
        const Coord second = row.components.at(1).at.x;
        QVERIFY(second > 0);
        QCOMPARE(second % PIN_GRID, Coord(0));
        QCOMPARE(row.components.at(1).at.y, Coord(0));
    }

    void designatorsAreNumberedOn()
    {
        Schematic schematic;
        schematic.sheets.append(fullSheet());
        schematic.sheets.append(Sheet());
        QCOMPARE(nextDesignator(schematic, QStringLiteral("R")), QStringLiteral("R2"));
        QCOMPARE(nextDesignator(schematic, QStringLiteral("C")), QStringLiteral("C1"));
        // A gap is filled, and every sheet counts.
        Component c3 = schematic.sheets[0].components.at(0);
        c3.designator = QStringLiteral("R3");
        schematic.sheets[1].components.append(c3);
        QCOMPARE(nextDesignator(schematic, QStringLiteral("R")), QStringLiteral("R2"));
        Component r2 = c3;
        r2.designator = QStringLiteral("R2");
        schematic.sheets[1].components.append(r2);
        QCOMPARE(nextDesignator(schematic, QStringLiteral("R")), QStringLiteral("R4"));
        // RV1 is not an R.
        QCOMPARE(nextDesignator(schematic, QStringLiteral("RV")), QStringLiteral("RV1"));
        // The designators of the board's wire links are taken too.
        QCOMPARE(nextDesignator(schematic, QStringLiteral("R"),
                                {QStringLiteral("R4"), QStringLiteral("RV5")}),
                 QStringLiteral("R5"));

        Part part;
        part.prefix = QStringLiteral("D");
        part.prefixGost = QStringLiteral("VD");
        QCOMPARE(designatorPrefix(part, DesignatorStyle::International), QStringLiteral("D"));
        QCOMPARE(designatorPrefix(part, DesignatorStyle::Gost), QStringLiteral("VD"));
    }

    void newComponentTakesWhatThePartSays()
    {
        Library library = resistorLibrary();
        library.parts[QStringLiteral("resistor")].value = QStringLiteral("10k");
        library.parts[QStringLiteral("resistor")].footprints = {QStringLiteral("r-7.62")};
        library.symbols[QStringLiteral("resistor")].refAt = Point{0, -3 * MM};
        const Component component = newComponent(library, QStringLiteral("resistor"),
                                                 QStringLiteral("r-7.62"));
        QVERIFY(!component.id.isNull());
        QCOMPARE(component.value, QStringLiteral("10k"));
        QCOMPARE(component.footprint, QStringLiteral("r-7.62"));
        QCOMPARE(component.refAt, Point({0, -3 * MM}));
        QVERIFY(!component.offBoard);
        // A part without footprints stays off the board.
        QVERIFY(newComponent(resistorLibrary(), QStringLiteral("resistor"), QString()).offBoard);
        QVERIFY(newComponent(library, QStringLiteral("resistor"), QString()).id
                != component.id);
    }

    void partIsCopiedIntoTheProjectOnce()
    {
        Library source = resistorLibrary();
        source.parts[QStringLiteral("resistor")].footprints = {QStringLiteral("a"),
                                                               QStringLiteral("b")};
        source.footprints.insert(QStringLiteral("a"), Footprint());
        Footprint b;
        b.refAt = Point{1, 1};
        source.footprints.insert(QStringLiteral("b"), b);
        source.parts.insert(QStringLiteral("other"), Part());

        const Library project = withPartFrom(Library(), source, QStringLiteral("resistor"));
        QCOMPARE(project.parts.keys(), QStringList({QStringLiteral("resistor")}));
        QCOMPARE(project.symbols.keys(), QStringList({QStringLiteral("resistor")}));
        QCOMPARE(project.footprints.keys(), QStringList({QStringLiteral("a"), QStringLiteral("b")}));

        // What the project has stays: the library changed, the project did not.
        Library changed = source;
        changed.symbols[QStringLiteral("resistor")].showPinNames = false;
        changed.footprints[QStringLiteral("b")].refAt = Point{2, 2};
        QVERIFY(withPartFrom(project, changed, QStringLiteral("resistor")) == project);
        // A part the source lacks changes nothing.
        QVERIFY(withPartFrom(project, source, QStringLiteral("absent")) == project);
    }

    void undoAndRedoTellWhereTheStepWasMade()
    {
        Document document;
        QSignalSpy applied(&document, &Document::stepApplied);
        Design design = document.design();
        design.schematic.sheets[0].title = QStringLiteral("A");
        document.change(QStringLiteral("Rename"), design, 0, {id(7)});
        // Making the change shows nothing new.
        QCOMPARE(applied.count(), 0);

        document.undoStack()->undo();
        QCOMPARE(applied.count(), 1);
        QCOMPARE(applied.at(0).at(0).toInt(), 0);
        QCOMPARE(applied.at(0).at(1).value<QSet<ObjectId>>(), QSet<ObjectId>({id(7)}));
        document.undoStack()->redo();
        QCOMPARE(applied.count(), 2);
    }
};

QTEST_GUILESS_MAIN(TestSheetEdit)
#include "test_sheetedit.moc"
