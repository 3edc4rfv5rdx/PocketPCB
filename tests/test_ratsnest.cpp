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
