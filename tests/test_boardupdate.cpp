#include "boardupdate.h"

#include "outline.h"
#include "ratsnest.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(double x, double y)
{
    return Point{mmToNm(x), mmToNm(y)};
}

// A part "two" with pins "1" at (-5, 0) and "2" at (5, 0) mm and a
// footprint "fp" with pads "1" and "2".
Library twoPinLibrary()
{
    Symbol symbol;
    Pin left;
    left.number = QStringLiteral("1");
    left.at = mm(-5, 0);
    left.side = PinSide::Left;
    Pin right = left;
    right.number = QStringLiteral("2");
    right.side = PinSide::Right;
    right.at = mm(5, 0);
    symbol.pins = {left, right};

    Footprint footprint;
    Pad pad;
    pad.number = QStringLiteral("1");
    pad.at = mm(-2.54, 0);
    pad.sizeX = 1600000;
    pad.sizeY = 1600000;
    pad.hole = 800000;
    footprint.pads.append(pad);
    pad.number = QStringLiteral("2");
    pad.at = mm(2.54, 0);
    footprint.pads.append(pad);

    Part part;
    part.symbol = QStringLiteral("two");
    part.footprints = {QStringLiteral("fp")};

    Library library;
    library.symbols.insert(QStringLiteral("two"), symbol);
    library.footprints.insert(QStringLiteral("fp"), footprint);
    library.parts.insert(QStringLiteral("two"), part);
    return library;
}

Component part(const QString &designator, const Point &at)
{
    Component component;
    component.id = newObjectId();
    component.part = QStringLiteral("two");
    component.footprint = QStringLiteral("fp");
    component.designator = designator;
    component.value = QStringLiteral("1k");
    component.at = at;
    return component;
}

// R1 and R2 side by side, R1's pin 2 on R2's pin 1.
Design twoResistors()
{
    Design design;
    design.library = twoPinLibrary();
    Sheet sheet;
    sheet.id = newObjectId();
    sheet.components = {part(QStringLiteral("R2"), mm(10, 0)),
                        part(QStringLiteral("R1"), mm(0, 0))};
    design.schematic.sheets = {sheet};
    return design;
}

const Placement *placementOf(const Board &board, const QString &designator)
{
    for (const Placement &placement : board.placements) {
        if (placement.designator == designator)
            return &placement;
    }
    return nullptr;
}

} // namespace

class TestBoardUpdate : public QObject
{
    Q_OBJECT

private slots:
    void newComponentsArePlaced()
    {
        const Design design = twoResistors();
        const BoardUpdate update = boardUpdatedFromSchematic(design);
        QCOMPARE(update.changes,
                 (QList<BoardChange>{
                     {BoardChangeKind::Add, QStringLiteral("R1"), QString(), QStringLiteral("fp")},
                     {BoardChangeKind::Add, QStringLiteral("R2"), QString(),
                      QStringLiteral("fp")}}));
        const Placement *r1 = placementOf(update.board, QStringLiteral("R1"));
        const Placement *r2 = placementOf(update.board, QStringLiteral("R2"));
        QVERIFY(r1 != nullptr && r2 != nullptr);
        QCOMPARE(r1->component, design.schematic.sheets.first().components.at(1).id);
        QCOMPARE(r1->value, QStringLiteral("1k"));
        QVERIFY(r1->showRef);
        // The pads joined on the schematic share a net.
        QVERIFY(!r1->padNets.value(QStringLiteral("2")).isEmpty());
        QCOMPARE(r1->padNets.value(QStringLiteral("2")), r2->padNets.value(QStringLiteral("1")));
        // A pin joined to nothing is on no net: its pad has none.
        QVERIFY(!r1->padNets.contains(QStringLiteral("1")));
        // One under the other on the grid, R1 first, apart.
        QCOMPARE(r1->at.x, r2->at.x);
        QVERIFY(r2->at.y > r1->at.y);
        QCOMPARE(r1->at.x % design.project.boardGridStep, Coord(0));
        QCOMPARE(r1->at.y % design.project.boardGridStep, Coord(0));
    }

    // Two units of one part: one placement, its pads on the nets of the
    // pins of both, kept on the unit it is tied to.
    void unitsOfOnePartAreOnePlacement()
    {
        Design design = twoResistors();
        Library &library = design.library;
        // Unit A has pin 1 alone, unit B pin 2 alone, each at its origin.
        Symbol a;
        Pin pin;
        pin.number = QStringLiteral("1");
        pin.side = PinSide::Left;
        a.pins = {pin};
        Symbol b = a;
        b.pins[0].number = QStringLiteral("2");
        library.symbols.insert(QStringLiteral("unit-a"), a);
        library.symbols.insert(QStringLiteral("unit-b"), b);
        Part dual;
        dual.symbol = QStringLiteral("unit-a");
        dual.units = {QStringLiteral("unit-b")};
        dual.footprints = {QStringLiteral("fp")};
        library.parts.insert(QStringLiteral("dual"), dual);
        // R1 stands at (0, 0): unit A on its pin 2, unit B on its pin 1.
        Component unitA = part(QStringLiteral("U1"), mm(5, 0));
        unitA.part = QStringLiteral("dual");
        Component unitB = unitA;
        unitB.id = newObjectId();
        unitB.unit = 1;
        unitB.at = mm(-5, 0);
        design.schematic.sheets[0].components.append({unitB, unitA});

        BoardUpdate update = boardUpdatedFromSchematic(design);
        QCOMPARE(update.board.placements.size(), 3);
        const Placement *u1 = placementOf(update.board, QStringLiteral("U1"));
        const Placement *r1 = placementOf(update.board, QStringLiteral("R1"));
        QVERIFY(u1 != nullptr && r1 != nullptr);
        QCOMPARE(u1->component, unitA.id);
        QVERIFY(!u1->padNets.value(QStringLiteral("1")).isEmpty());
        QCOMPARE(u1->padNets.value(QStringLiteral("1")), r1->padNets.value(QStringLiteral("2")));
        QVERIFY(!u1->padNets.value(QStringLiteral("2")).isEmpty());
        QCOMPARE(u1->padNets.value(QStringLiteral("2")), r1->padNets.value(QStringLiteral("1")));

        // A placement tied to unit B stays tied to it.
        design.board = update.board;
        for (Placement &placement : design.board.placements) {
            if (placement.designator == QStringLiteral("U1"))
                placement.component = unitB.id;
        }
        update = boardUpdatedFromSchematic(design);
        QVERIFY(update.changes.isEmpty());
        QCOMPARE(placementOf(update.board, QStringLiteral("U1"))->component, unitB.id);
    }

    void nothingChangesTheSecondTime()
    {
        Design design = twoResistors();
        design.board = boardUpdatedFromSchematic(design).board;
        const BoardUpdate again = boardUpdatedFromSchematic(design);
        QVERIFY(again.changes.isEmpty());
        QVERIFY(again.board == design.board);
    }

    void placementsFollowTheirComponents()
    {
        Design design = twoResistors();
        design.board = boardUpdatedFromSchematic(design).board;
        // A placement moved by hand stays where it is.
        design.board.placements[0].at = mm(77, 33);
        const ObjectId movedId = design.board.placements.at(0).id;
        QList<Component> &components = design.schematic.sheets.first().components;
        // R2 is gone; R1 is renamed R5 and given another value; R3 is new
        // but off the board, R4 has no footprint.
        components.removeAt(0);
        components[0].designator = QStringLiteral("R5");
        components[0].value = QStringLiteral("2k2");
        Component offBoard = part(QStringLiteral("R3"), mm(30, 30));
        offBoard.offBoard = true;
        Component bare = part(QStringLiteral("R4"), mm(40, 30));
        bare.footprint.clear();
        components << offBoard << bare;

        const BoardUpdate update = boardUpdatedFromSchematic(design);
        QCOMPARE(update.changes,
                 (QList<BoardChange>{
                     {BoardChangeKind::Remove, QStringLiteral("R2"), QStringLiteral("fp"),
                      QString()},
                     {BoardChangeKind::Designator, QStringLiteral("R5"), QStringLiteral("R1"),
                      QStringLiteral("R5")},
                     {BoardChangeKind::Value, QStringLiteral("R5"), QStringLiteral("1k"),
                      QStringLiteral("2k2")},
                     // Its pin 2 joins nothing now: the pad has no net.
                     {BoardChangeKind::Nets, QStringLiteral("R5"), QString(), QString()}}));
        QCOMPARE(update.board.placements.size(), 1);
        const Placement &kept = update.board.placements.first();
        QCOMPARE(kept.designator, QStringLiteral("R5"));
        QCOMPARE(kept.value, QStringLiteral("2k2"));
        QCOMPARE(kept.id, movedId);
        QCOMPARE(kept.at, mm(77, 33));
    }

    void tracksGoOnToANewFootprint()
    {
        Design design = twoResistors();
        // "wide" has pad 1 at (-5.08, 0) and no pad 2.
        Footprint wide;
        Pad pad;
        pad.number = QStringLiteral("1");
        pad.at = mm(-5.08, 0);
        pad.sizeX = 1600000;
        pad.sizeY = 1600000;
        pad.hole = 800000;
        wide.pads.append(pad);
        design.library.footprints.insert(QStringLiteral("wide"), wide);
        design.board = boardUpdatedFromSchematic(design).board;
        const Placement r2 = *placementOf(design.board, QStringLiteral("R2"));
        const auto track = [&design](const Point &from, const Point &to) {
            Track result;
            result.id = newObjectId();
            result.from = from;
            result.to = to;
            design.board.tracks.append(result);
            return result.id;
        };
        // Straight down from the pads, clear of the other part.
        const Point below = placedPadPoint(r2, mm(-2.54, 0)) + mm(0, 10);
        const ObjectId onFirst = track(below, placedPadPoint(r2, mm(-2.54, 0)));
        track(placedPadPoint(r2, mm(2.54, 0)), placedPadPoint(r2, mm(2.54, 0)) + mm(0, 10));
        design.schematic.sheets.first().components[0].footprint = QStringLiteral("wide");

        // Pad 1 is still there, further out: the track stretches to it; the
        // track from pad 2, which is gone, goes.
        const Board board = boardUpdatedFromSchematic(design).board;
        QCOMPARE(board.tracks.size(), 1);
        QCOMPARE(board.tracks.first().id, onFirst);
        QCOMPARE(board.tracks.first().from, below);
        QCOMPARE(board.tracks.first().to, placedPadPoint(r2, mm(-5.08, 0)));
    }

    void aRemovedPlacementTakesItsTracks()
    {
        Design design = twoResistors();
        design.board = boardUpdatedFromSchematic(design).board;
        // A track from each pad of R2, and one between two points apart.
        const Placement &r2 = *placementOf(design.board, QStringLiteral("R2"));
        const auto track = [&design](const Point &from, const Point &to) {
            Track result;
            result.id = newObjectId();
            result.from = from;
            result.to = to;
            design.board.tracks.append(result);
            return result.id;
        };
        track(placedPadPoint(r2, mm(-2.54, 0)), mm(50, 50));
        track(placedPadPoint(r2, mm(2.54, 0)), mm(60, 50));
        const ObjectId apart = track(mm(70, 70), mm(80, 70));
        design.schematic.sheets.first().components.removeAt(0);

        const Board board = boardUpdatedFromSchematic(design).board;
        QCOMPARE(board.tracks.size(), 1);
        QCOMPARE(board.tracks.first().id, apart);
    }

    void aPlacementWhoseFootprintIsMissingIsKept()
    {
        Design design = twoResistors();
        design.board = boardUpdatedFromSchematic(design).board;
        const Board placed = design.board;
        // The footprint file is gone from the project library.
        design.library.footprints.remove(QStringLiteral("fp"));

        const BoardUpdate update = boardUpdatedFromSchematic(design);
        QVERIFY(update.board == placed);
        QCOMPARE(update.changes,
                 (QList<BoardChange>{{BoardChangeKind::MissingFootprint, QStringLiteral("R1"),
                                      QString(), QStringLiteral("fp")},
                                     {BoardChangeKind::MissingFootprint, QStringLiteral("R2"),
                                      QString(), QStringLiteral("fp")}}));

        // One never placed is not added.
        design.board = Board();
        const BoardUpdate fresh = boardUpdatedFromSchematic(design);
        QVERIFY(fresh.board.placements.isEmpty());
        QCOMPARE(fresh.changes,
                 (QList<BoardChange>{{BoardChangeKind::MissingFootprintNotAdded,
                                      QStringLiteral("R1"), QString(), QStringLiteral("fp")},
                                     {BoardChangeKind::MissingFootprintNotAdded,
                                      QStringLiteral("R2"), QString(), QStringLiteral("fp")}}));
    }

    void netsOfPadsKeepTheirNamesWhenOtherNetsComeFirst()
    {
        Design design = twoResistors();
        BoardUpdate update = boardUpdatedFromSchematic(design);
        design.board = update.board;
        design.project = update.project;
        const QString name =
            placementOf(design.board, QStringLiteral("R1"))->padNets.value(QStringLiteral("2"));
        QCOMPARE(name, QStringLiteral("NN1"));

        // A new net whose first pin, R0.2, comes before R1.2.
        QList<Component> &components = design.schematic.sheets[0].components;
        components << part(QStringLiteral("R0"), mm(-30, 30))
                   << part(QStringLiteral("R00"), mm(-20, 30));
        update = boardUpdatedFromSchematic(design);
        for (const BoardChange &change : update.changes)
            QCOMPARE(int(change.kind), int(BoardChangeKind::Add));
        QCOMPARE(placementOf(update.board, QStringLiteral("R1"))->padNets.value(QStringLiteral("2")),
                 name);
        QCOMPARE(placementOf(update.board, QStringLiteral("R0"))->padNets.value(QStringLiteral("2")),
                 QStringLiteral("NN2"));
    }

    // A name of the nets table given by a label to another net would make
    // the two one net on the board: the update is refused.
    void netsSharingANameAreNotPutOnTheBoard()
    {
        Design design = twoResistors();
        QList<Component> &components = design.schematic.sheets[0].components;
        components << part(QStringLiteral("R3"), mm(30, 0)) << part(QStringLiteral("R4"), mm(40, 0));
        design.project.netsByPins = {
            NetByPins{QStringLiteral("NN1"),
                      {PinRef{components.at(1).id, QStringLiteral("2")},
                       PinRef{components.at(0).id, QStringLiteral("1")}}}};
        Label label;
        label.id = newObjectId();
        label.text = QStringLiteral("NN1");
        label.at = mm(35, 0);
        design.schematic.sheets[0].labels.append(label);

        const BoardUpdate update = boardUpdatedFromSchematic(design);
        QCOMPARE(update.sharedNames, QStringList{QStringLiteral("NN1")});
        QVERIFY(update.changes.isEmpty());
        QVERIFY(update.board == design.board);
        QVERIFY(update.project == design.project);

        // Named otherwise, the two nets go on the board apart.
        design.schematic.sheets[0].labels[0].text = QStringLiteral("CLK");
        const BoardUpdate named = boardUpdatedFromSchematic(design);
        QVERIFY(named.sharedNames.isEmpty());
        const Placement *r1 = placementOf(named.board, QStringLiteral("R1"));
        const Placement *r3 = placementOf(named.board, QStringLiteral("R3"));
        QVERIFY(r1 != nullptr && r3 != nullptr);
        QCOMPARE(r1->padNets.value(QStringLiteral("2")), QStringLiteral("NN1"));
        QCOMPARE(r3->padNets.value(QStringLiteral("2")), QStringLiteral("CLK"));
    }

    void netsOfPadsFollowTheSchematic()
    {
        Design design = twoResistors();
        design.board = boardUpdatedFromSchematic(design).board;
        // R2 moved off R1: their pins join nothing, their pads lose the net.
        design.schematic.sheets[0].components[0].at = mm(30, 0);
        const BoardUpdate update = boardUpdatedFromSchematic(design);
        const Placement *r1 = placementOf(update.board, QStringLiteral("R1"));
        const Placement *r2 = placementOf(update.board, QStringLiteral("R2"));
        QVERIFY(r1 != nullptr && r2 != nullptr);
        QVERIFY(r1->padNets.isEmpty());
        QVERIFY(r2->padNets.isEmpty());
        QCOMPARE(update.changes,
                 (QList<BoardChange>{
                     {BoardChangeKind::Nets, QStringLiteral("R1"), QString(), QString()},
                     {BoardChangeKind::Nets, QStringLiteral("R2"), QString(), QString()}}));
    }

    void newPlacementsStandRightOfTheOutline()
    {
        Design design = twoResistors();
        design.board.outline = rectangleOutline(mm(10, 20), 40 * MM, 30 * MM);
        const BoardUpdate update = boardUpdatedFromSchematic(design);
        for (const Placement &placement : update.board.placements)
            QVERIFY(placement.at.x > 50 * MM + NEW_PLACEMENT_GAP);
        const Placement *r1 = placementOf(update.board, QStringLiteral("R1"));
        QVERIFY(r1 != nullptr);
        QVERIFY(r1->at.y >= 20 * MM && r1->at.y < 30 * MM);
    }

    // The pads of holes go where most pads of their net went: a net renamed
    // takes them along, a net kept keeps them, a net gone drops them.
    void holeNetsFollowTheirPads()
    {
        Placement first;
        first.id = newObjectId();
        first.padNets = {{QStringLiteral("1"), QStringLiteral("GND")},
                         {QStringLiteral("2"), QStringLiteral("A")}};
        Placement second = first;
        second.id = newObjectId();
        second.padNets = {{QStringLiteral("1"), QStringLiteral("GND")},
                          {QStringLiteral("2"), QStringLiteral("B")}};
        Board before;
        before.placements = {first, second};
        for (const char *net : {"GND", "A", "B", "C"}) {
            MountingHole hole;
            hole.id = newObjectId();
            hole.ring = MM;
            hole.net = QLatin1String(net);
            before.holes.append(hole);
        }
        Board after = before;
        // GND renamed VSS on both, A kept, B gone with the second part.
        after.placements[0].padNets = {{QStringLiteral("1"), QStringLiteral("VSS")},
                                       {QStringLiteral("2"), QStringLiteral("A")}};
        after.placements.removeLast();
        const QHash<QString, QString> followed = holeNetsFollowed(before, after);
        QCOMPARE(followed.value(QStringLiteral("GND")), QStringLiteral("VSS"));
        QCOMPARE(followed.value(QStringLiteral("A")), QStringLiteral("A"));
        QCOMPARE(followed.value(QStringLiteral("B")), QString());
        // A net no pad was ever on, and no pad is on now, is dropped too.
        QVERIFY(followed.contains(QStringLiteral("C")));
        QCOMPARE(followed.value(QStringLiteral("C")), QString());
        // A net only a part added now is on is kept.
        Placement added;
        added.id = newObjectId();
        added.padNets = {{QStringLiteral("1"), QStringLiteral("C")}};
        after.placements.append(added);
        QCOMPARE(holeNetsFollowed(before, after).value(QStringLiteral("C")), QStringLiteral("C"));
    }
};

QTEST_APPLESS_MAIN(TestBoardUpdate)
#include "test_boardupdate.moc"
