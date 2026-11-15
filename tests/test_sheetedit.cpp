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
        const QList<SheetObjectShape> shapes = sheetObjectShapes(fullSheet(), resistorLibrary());
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
            SheetObjectShape{id(11), QRectF(0, 5, 10, 0), QPolygonF({QPointF(0, 5), QPointF(10, 5)})},
            SheetObjectShape{id(10), QRectF(0, 0, 10, 10), QPolygonF()}};
        QCOMPARE(objectAt(crossing, QPointF(5, 5.2), 0.5), id(11));
        QCOMPARE(objectAt(crossing, QPointF(5, 8), 0.5), id(10));

        // A component the library lacks is still found where it stands.
        const QList<SheetObjectShape> missing = sheetObjectShapes(fullSheet(), Library());
        QCOMPARE(objectAt(missing, QPointF(50, 50), 0.5), id(1));
    }

    void rectangleTakesWhatIsWhollyInside()
    {
        const QList<SheetObjectShape> shapes = sheetObjectShapes(fullSheet(), resistorLibrary());
        QCOMPARE(objectsInside(shapes, QRectF(5, 5, 40, 20)), QSet<ObjectId>({id(2)}));
        // Half of the wire is not enough.
        QVERIFY(objectsInside(shapes, QRectF(5, 5, 20, 20)).isEmpty());
        // A straight bus has no height, and is still inside.
        QCOMPARE(objectsInside(shapes, QRectF(5, 75, 30, 10)), QSet<ObjectId>({id(3)}));
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

    void componentTextsAreFoundAndMovedAlone()
    {
        Library library = resistorLibrary();
        Sheet sheet = fullSheet();
        // R1 at (50, 50): its designator at (50, 47), its value at (52, 53).
        QCOMPARE(componentTextAt(sheet, library, QPointF(50, 47)).component, id(1));
        QVERIFY(componentTextAt(sheet, library, QPointF(50, 47)).text == ComponentText::Designator);
        // No value: its mark is found where it stands.
        QCOMPARE(componentTextAt(sheet, library, QPointF(52, 53)).component, id(1));
        QVERIFY(componentTextAt(sheet, library, QPointF(52, 53)).text == ComponentText::Value);
        // A part that needs no value shows no mark, and none is found.
        library.parts[QStringLiteral("resistor")].noValue = true;
        QVERIFY(componentTextAt(sheet, library, QPointF(52, 53)).component.isNull());
        QVERIFY(componentTextShown(sheet.components.at(0), ComponentText::Value, library)
                    .isEmpty());
        library.parts[QStringLiteral("resistor")].noValue = false;
        // No designator: nothing is found.
        sheet.components[0].designator.clear();
        QVERIFY(componentTextAt(sheet, library, QPointF(50, 47)).component.isNull());
        sheet = withComponentValue(sheet, {id(1)}, QStringLiteral(" 10k "));
        QCOMPARE(sheet.components.at(0).value, QStringLiteral("10k"));
        sheet.components[0].designator = QStringLiteral("R1");
        QCOMPARE(componentTextAt(sheet, library, QPointF(52, 53)).component, id(1));
        QVERIFY(componentTextAt(sheet, library, QPointF(52, 53)).text == ComponentText::Value);
        QVERIFY(componentTextAt(sheet, library, QPointF(80, 80)).component.isNull());
        // The component's own point, between its texts, is not a text.
        QVERIFY(componentTextAt(sheet, library, QPointF(50, 50)).component.isNull());

        const Point delta{2 * MM, -1 * MM};
        const Sheet moved = withComponentTextMoved(sheet, id(1), ComponentText::Value, delta);
        QCOMPARE(moved.components.at(0).valueAt, sheet.components.at(0).valueAt + delta);
        QCOMPARE(moved.components.at(0).refAt, sheet.components.at(0).refAt);
        QCOMPARE(moved.components.at(0).at, sheet.components.at(0).at);
        QVERIFY(moved.wires == sheet.wires);
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
        QVERIFY(isDesignatorTaken(schematic, QStringLiteral("R1"), id(20)));
        // A component does not take its own designator.
        QVERIFY(!isDesignatorTaken(schematic, QStringLiteral("R1"), id(1)));
        QVERIFY(!isDesignatorTaken(schematic, QStringLiteral("R3"), id(20)));
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
