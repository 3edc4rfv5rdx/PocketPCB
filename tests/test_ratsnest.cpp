#include "ratsnest.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

// A footprint "fp" with pads "1" at (-5, 0) and "2" at (5, 0) mm.
Library library()
{
    Footprint footprint;
    Pad pad;
    pad.number = QStringLiteral("1");
    pad.at = mm(-5, 0);
    pad.sizeX = 2 * MM;
    pad.sizeY = 2 * MM;
    pad.hole = MM;
    footprint.pads.append(pad);
    pad.number = QStringLiteral("2");
    pad.at = mm(5, 0);
    footprint.pads.append(pad);
    Library result;
    result.footprints.insert(QStringLiteral("fp"), footprint);
    return result;
}

Placement placement(const Point &at, const QString &net1, const QString &net2)
{
    Placement result;
    result.id = newObjectId();
    result.component = newObjectId();
    result.footprint = QStringLiteral("fp");
    result.designator = QStringLiteral("R");
    result.at = at;
    if (!net1.isEmpty())
        result.padNets.insert(QStringLiteral("1"), net1);
    if (!net2.isEmpty())
        result.padNets.insert(QStringLiteral("2"), net2);
    return result;
}

using Lines = QList<RatsnestLine>;

} // namespace

class TestRatsnest : public QObject
{
    Q_OBJECT

private slots:
    void padsArePlacedAsTheBoardDrawsThem()
    {
        Placement turned = placement(mm(10, 10), QString(), QString());
        QCOMPARE(placedPadPoint(turned, mm(5, 0)), mm(15, 10));
        turned.rotation = 90;
        QCOMPARE(placedPadPoint(turned, mm(5, 0)), mm(10, 15));
        turned.rotation = 0;
        turned.side = BoardSide::Bottom;
        QCOMPARE(placedPadPoint(turned, mm(5, 0)), mm(5, 10));
    }

    // Three parts in a row on net A by their pads 2 and 1 in turn: each pad
    // is tied to the nearest one of the net, not all to the first.
    void eachNetIsTiedByItsShortestTree()
    {
        Board board;
        board.placements = {placement(mm(0, 0), QString(), QStringLiteral("A")),
                            placement(mm(20, 0), QStringLiteral("A"), QStringLiteral("B")),
                            placement(mm(40, 0), QStringLiteral("A"), QString())};
        // A: (5, 0), (15, 0), (35, 0); B alone at (25, 0).
        QCOMPARE(ratsnest(board, library()),
                 (Lines{{mm(5, 0), mm(15, 0), QStringLiteral("A")},
                        {mm(15, 0), mm(35, 0), QStringLiteral("A")}}));
        // A footprint missing from the library adds nothing.
        board.placements[2].footprint = QStringLiteral("gone");
        QCOMPARE(ratsnest(board, library()), (Lines{{mm(5, 0), mm(15, 0), QStringLiteral("A")}}));
        QCOMPARE(netPads(board, library()).size(), 3);
    }

    // Pads joined by tracks and vias need no line; a track ending on a pad
    // of another side joins nothing.
    void copperJoinsPads()
    {
        Board board;
        board.placements = {placement(mm(0, 0), QString(), QStringLiteral("A")),
                            placement(mm(20, 0), QStringLiteral("A"), QString())};
        // A: (5, 0) and (15, 0).
        QCOMPARE(ratsnest(board, library()).size(), 1);
        const auto track = [](BoardSide side, const Point &from, const Point &to) {
            Track result;
            result.id = newObjectId();
            result.side = side;
            result.net = QStringLiteral("A");
            result.width = MM / 2;
            result.from = from;
            result.to = to;
            return result;
        };
        board.tracks = {track(BoardSide::Bottom, mm(5, 0), mm(15, 0))};
        QVERIFY(ratsnest(board, library()).isEmpty());
        QCOMPARE(routingProgress(board, library()).unrouted, 0);
        QCOMPARE(routingProgress(board, library()).total, 1);
        // Top to a via, then bottom on, ending on the middle of a track there.
        board.tracks = {track(BoardSide::Top, mm(5, 0), mm(10, 5)),
                        track(BoardSide::Bottom, mm(10, 5), mm(15, 5)),
                        track(BoardSide::Bottom, mm(15, 5), mm(15, 0))};
        QCOMPARE(ratsnest(board, library()).size(), 1);
        Via via;
        via.id = newObjectId();
        via.at = mm(10, 5);
        via.size = MM;
        via.drill = MM / 2;
        board.vias = {via};
        QVERIFY(ratsnest(board, library()).isEmpty());
        // A track on the bottom ending on a pad on the top only joins nothing.
        Library oneSided = library();
        Footprint &footprint = oneSided.footprints[QStringLiteral("fp")];
        for (Pad &pad : footprint.pads)
            pad.layers = PadLayers::Front;
        board.vias.clear();
        board.tracks = {track(BoardSide::Bottom, mm(5, 0), mm(15, 0))};
        QCOMPARE(ratsnest(board, oneSided).size(), 1);
        const QList<int> groups = copperGroups(board, netPads(board, library()));
        QCOMPARE(groups.size(), 2);
        QCOMPARE(groups.at(0), groups.at(1));
    }

    // A mounting hole with a pad on a net is one of its pads; one without
    // a pad, or without a net, is not.
    void holePadsAreOnTheirNets()
    {
        Board board;
        board.placements = {placement(mm(0, 0), QString(), QStringLiteral("GND"))};
        MountingHole hole;
        hole.id = newObjectId();
        hole.at = mm(30, 0);
        hole.drill = 3 * NM_PER_MM;
        hole.ring = NM_PER_MM;
        hole.net = QStringLiteral("GND");
        board.holes.append(hole);
        hole.id = newObjectId();
        hole.at = mm(60, 0);
        hole.net.clear();
        board.holes.append(hole);
        QCOMPARE(ratsnest(board, library()),
                 (Lines{{mm(5, 0), mm(30, 0), QStringLiteral("GND")}}));
        const QList<NetPad> pads = netPads(board, library());
        QCOMPARE(pads.size(), 2);
        QCOMPARE(pads.at(1).sizeX, 5 * NM_PER_MM);
    }
};

QTEST_APPLESS_MAIN(TestRatsnest)
#include "test_ratsnest.moc"
