#include "trackedit.h"

#include "boardedit.h"
#include "ratsnest.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

// A footprint "fp" with a pad "1" through the board at (-5, 0) and a pad
// "2" on the front only at (5, 0), both 2 mm across.
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
    pad.hole = 0;
    pad.layers = PadLayers::Front;
    footprint.pads.append(pad);
    Library result;
    result.footprints.insert(QStringLiteral("fp"), footprint);
    return result;
}

Board boardWithPart()
{
    Placement placement;
    placement.id = newObjectId();
    placement.footprint = QStringLiteral("fp");
    placement.padNets = {{QStringLiteral("1"), QStringLiteral("A")},
                         {QStringLiteral("2"), QStringLiteral("B")}};
    Board board;
    board.placements = {placement};
    return board;
}

} // namespace

class TestTrackEdit : public QObject
{
    Q_OBJECT

private slots:
    void cornersFollowTheAnglesOfTheBoard()
    {
        // 10 across, 4 down: 6 straight and 4 slanted.
        QCOMPARE(trackCorners(mm(0, 0), mm(10, 4), false),
                 (QList<Point>{mm(0, 0), mm(6, 0), mm(10, 4)}));
        QCOMPARE(trackCorners(mm(0, 0), mm(10, 4), true),
                 (QList<Point>{mm(0, 0), mm(4, 4), mm(10, 4)}));
        // Upwards and to the left.
        QCOMPARE(trackCorners(mm(0, 0), mm(-3, -8), false),
                 (QList<Point>{mm(0, 0), mm(0, -5), mm(-3, -8)}));
        // One piece when they line up, none twice.
        QCOMPARE(trackCorners(mm(0, 0), mm(7, 0), true), (QList<Point>{mm(0, 0), mm(7, 0)}));
        QCOMPARE(trackCorners(mm(0, 0), mm(5, 5), false), (QList<Point>{mm(0, 0), mm(5, 5)}));
        QCOMPARE(trackCorners(mm(2, 2), mm(2, 2), false), (QList<Point>{mm(2, 2)}));
    }

    void padSidesFollowThePart()
    {
        QVERIFY(padCopperSides(PadLayers::Both, BoardSide::Bottom) == CopperSides::Both);
        QVERIFY(padCopperSides(PadLayers::Front, BoardSide::Top) == CopperSides::Top);
        QVERIFY(padCopperSides(PadLayers::Front, BoardSide::Bottom) == CopperSides::Bottom);
        QVERIFY(padCopperSides(PadLayers::Back, BoardSide::Top) == CopperSides::Bottom);
    }

    void copperIsFoundUnderTheCursor()
    {
        Board board = boardWithPart();
        // A pad through the board, found within its size and the reach.
        std::optional<CopperPoint> point = copperAt(board, library(), QPointF(-4.5, 0.8), 0);
        QVERIFY(point.has_value());
        QCOMPARE(point->at, mm(-5, 0));
        QCOMPARE(point->net, QStringLiteral("A"));
        QVERIFY(point->top && point->bottom);
        QVERIFY(!copperAt(board, library(), QPointF(-3.5, 0), 0.2).has_value());
        QVERIFY(copperAt(board, library(), QPointF(-3.5, 0), 0.6).has_value());
        // A pad on the front of a part on the top.
        point = copperAt(board, library(), QPointF(5, 0), 0);
        QVERIFY(point.has_value() && point->top && !point->bottom);
        // A board with copper on the bottom only has none of that pad.
        board.copper = CopperSides::Bottom;
        QVERIFY(!copperAt(board, library(), QPointF(5, 0), 0).has_value());
        board.copper = CopperSides::Both;

        // A track: its end, then a point along it.
        Track track;
        track.id = newObjectId();
        track.side = BoardSide::Bottom;
        track.net = QStringLiteral("C");
        track.width = MM;
        track.from = mm(0, 10);
        track.to = mm(20, 10);
        board.tracks.append(track);
        point = copperAt(board, library(), QPointF(19.8, 10.3), 0);
        QVERIFY(point.has_value());
        QCOMPARE(point->at, mm(20, 10));
        QVERIFY(!point->top && point->bottom);
        point = copperAt(board, library(), QPointF(7, 10.4), 0);
        QVERIFY(point.has_value());
        QCOMPARE(point->at, mm(7, 10));
        QCOMPARE(point->net, QStringLiteral("C"));
        QVERIFY(!copperAt(board, library(), QPointF(7, 12), 0.5).has_value());

        // A via, over the track.
        Via via;
        via.id = newObjectId();
        via.at = mm(7, 10);
        via.size = 2 * MM;
        via.drill = MM;
        via.net = QStringLiteral("C");
        board.vias.append(via);
        point = copperAt(board, library(), QPointF(7.5, 10), 0);
        QVERIFY(point.has_value() && point->top && point->bottom);
        QCOMPARE(point->at, mm(7, 10));
    }

    void tracksAreChosenAndEdited()
    {
        Board board;
        const auto track = [&board](BoardSide side, const Point &from, const Point &to) {
            Track result;
            result.id = newObjectId();
            result.side = side;
            result.width = MM;
            result.from = from;
            result.to = to;
            board.tracks.append(result);
            return result.id;
        };
        // A run from (0, 0) to (20, 0) on the top, through a via to the
        // bottom, and on to (20, 10); and a track apart.
        const ObjectId a = track(BoardSide::Top, mm(0, 0), mm(10, 0));
        const ObjectId b = track(BoardSide::Top, mm(10, 0), mm(20, 0));
        const ObjectId c = track(BoardSide::Bottom, mm(20, 0), mm(20, 10));
        const ObjectId apart = track(BoardSide::Bottom, mm(10, 0), mm(10, 10));
        Via via;
        via.id = newObjectId();
        via.at = mm(20, 0);
        via.size = 2 * MM;
        via.drill = MM;
        board.vias.append(via);

        // The bottom one first where both are, then the top.
        QCOMPARE(trackAt(board, QPointF(10, 0.2), 0, BoardSide::Bottom), apart);
        QCOMPARE(trackAt(board, QPointF(10.1, 0.2), 0, BoardSide::Top), b);
        QVERIFY(trackAt(board, QPointF(5, 3), 0.5, BoardSide::Top).isNull());
        QCOMPARE(viaAt(board, QPointF(20.5, 0), 0), via.id);

        // The run: end to end on one side, through the via, not to the
        // track of the other side that only touches it.
        QCOMPARE(trackRun(board, Library(), a), (QSet<ObjectId>{a, b, c, via.id}));
        QCOMPARE(trackRun(board, Library(), apart), QSet<ObjectId>{apart});
        QVERIFY(trackRun(board, Library(), ObjectId()).isEmpty());

        QCOMPARE(tracksInside(board, QRectF(-1, -1, 12, 2)), QSet<ObjectId>{a});
        QCOMPARE(tracksInside(board, QRectF(18.9, -1.1, 2.2, 2.2)), QSet<ObjectId>{via.id});

        const Board fewer = withoutTracks(board, {a, via.id});
        QCOMPARE(fewer.tracks.size(), 3);
        QVERIFY(fewer.vias.isEmpty());
        const Board wider = withTracksWidth(board, {b}, 2 * MM);
        QCOMPARE(wider.tracks.at(1).width, 2 * MM);
        QCOMPARE(wider.tracks.at(0).width, MM);
        QVERIFY(withTracksSide(board, {a}, BoardSide::Bottom).tracks.at(0).side
                == BoardSide::Bottom);

        // A pad of a part is found on its own.
        Board withPart = boardWithPart();
        QVERIFY(padAt(withPart, library(), QPointF(-5, 0), 0).has_value());
        QVERIFY(!padAt(withPart, library(), QPointF(0, 0), 0).has_value());
    }

    void runsStopAtPadsAndForks()
    {
        // The part's pad 1 at (-5, 0) and pad 2 at (5, 0), through and
        // front; a track from pad 1 left to (-15, 0), on to (-15, 10).
        Board board = boardWithPart();
        const auto track = [&board](const Point &from, const Point &to) {
            Track result;
            result.id = newObjectId();
            result.net = QStringLiteral("A");
            result.from = from;
            result.to = to;
            board.tracks.append(result);
            return result.id;
        };
        const ObjectId left = track(mm(-5, 0), mm(-15, 0));
        const ObjectId down = track(mm(-15, 0), mm(-15, 10));
        // Through pad 1 on to the right, and to pad 2.
        const ObjectId across = track(mm(-5, 0), mm(0, 0));
        const ObjectId toPad = track(mm(0, 0), mm(5, 0));
        QCOMPARE(trackRun(board, library(), left), (QSet<ObjectId>{left, down}));
        QCOMPARE(trackRun(board, library(), across), (QSet<ObjectId>{across, toPad}));

        // Three ends at (-15, 10): a fork.
        const ObjectId forkA = track(mm(-15, 10), mm(-25, 10));
        const ObjectId forkB = track(mm(-15, 10), mm(-15, 20));
        QCOMPARE(trackRun(board, library(), left), (QSet<ObjectId>{left, down}));
        QCOMPARE(trackRun(board, library(), forkA), QSet<ObjectId>{forkA});

        // A branch ending on the middle of 'forkB': the run of the branch
        // stops there, and one coming into 'forkB' stops before it.
        const ObjectId branch = track(mm(-15, 15), mm(-10, 15));
        QCOMPARE(trackRun(board, library(), branch), QSet<ObjectId>{branch});
        const ObjectId tail = track(mm(-15, 20), mm(-15, 30));
        QCOMPARE(trackRun(board, library(), tail), QSet<ObjectId>{tail});
        QCOMPARE(trackRun(board, library(), forkB), (QSet<ObjectId>{forkB, tail}));

        // A via with a track not in the run stays out of it.
        Via via;
        via.id = newObjectId();
        via.at = mm(-25, 10);
        board.vias.append(via);
        QCOMPARE(trackRun(board, library(), forkA), (QSet<ObjectId>{forkA, via.id}));
        Track other;
        other.id = newObjectId();
        other.side = BoardSide::Bottom;
        other.from = mm(-25, 10);
        other.to = mm(-35, 10);
        board.tracks.append(other);
        QCOMPARE(trackRun(board, library(), forkA), (QSet<ObjectId>{forkA, other.id, via.id}));
        board.tracks.last().to = mm(-25, 0);
        Track third = other;
        third.id = newObjectId();
        third.to = mm(-25, 20);
        board.tracks.append(third);
        QCOMPARE(trackRun(board, library(), forkA), QSet<ObjectId>{forkA});

        // The tracks of the part: those from its pads, up to the fork.
        const QSet<ObjectId> part{board.placements.at(0).id};
        QCOMPARE(tracksOfPads(board, library(), part),
                 (QSet<ObjectId>{left, down, across, toPad}));
        QVERIFY(tracksOfPads(board, library(), {}).isEmpty());
    }

    void tracksFollowTheirPads()
    {
        // A track on pad 1 (through, net A) and one on pad 2 (front, net B).
        Board board = boardWithPart();
        Track onThrough;
        onThrough.id = newObjectId();
        onThrough.net = QStringLiteral("A");
        onThrough.side = BoardSide::Bottom;
        onThrough.from = mm(-5, 0);
        onThrough.to = mm(-5, 10);
        Track onFront;
        onFront.id = newObjectId();
        onFront.net = QStringLiteral("B");
        onFront.from = mm(5, 10);
        onFront.to = mm(5, 0);
        board.tracks = {onThrough, onFront};
        const QSet<ObjectId> part{board.placements.at(0).id};

        // Moved: the ends on the pads go with them, the others stay.
        const Board moved = withTracksFollowingPads(
            board, withPlacementsMoved(board, part, mm(2, 0)), library());
        QCOMPARE(moved.tracks.at(0).from, mm(-3, 0));
        QCOMPARE(moved.tracks.at(0).to, mm(-5, 10));
        QCOMPARE(moved.tracks.at(1).from, mm(5, 10));
        QCOMPARE(moved.tracks.at(1).to, mm(7, 0));

        // Turned about the part's point: the end goes where pad 1 is now.
        const Board rotated = withPlacementsRotated(board, part, Point());
        const Board turned = withTracksFollowingPads(board, rotated, library());
        QCOMPARE(turned.tracks.at(0).from, placedPadPoint(rotated.placements.at(0), mm(-5, 0)));
        QVERIFY(turned.tracks.at(0).from != mm(-5, 0));

        // A track of another net, or on a side the pad lacks, stays.
        Board others = board;
        others.tracks[0].net = QStringLiteral("B");
        others.tracks[1].side = BoardSide::Bottom;
        const Board kept = withTracksFollowingPads(
            others, withPlacementsMoved(others, part, mm(2, 0)), library());
        QCOMPARE(kept.tracks.at(0).from, mm(-5, 0));
        QCOMPARE(kept.tracks.at(1).to, mm(5, 0));

        // A piece whose pad lands on its other end goes.
        const Board shrunk = withTracksFollowingPads(
            board, withPlacementsMoved(board, part, mm(0, 10)), library());
        QVERIFY(shrunk.tracks.isEmpty());

        // A pad left where the moved one was holds the ends on it.
        Board twin = board;
        Placement still = board.placements.at(0);
        still.id = newObjectId();
        twin.placements.append(still);
        const Board held = withTracksFollowingPads(
            twin, withPlacementsMoved(twin, part, mm(2, 0)), library());
        QCOMPARE(held.tracks.at(0).from, mm(-5, 0));

        // A track carried whole with the part is not stretched again.
        const QSet<ObjectId> carried{onThrough.id};
        const Board along = withTracksFollowingPads(
            board,
            withTracksMoved(withPlacementsMoved(board, part, mm(2, 0)), carried, mm(2, 0)),
            library(), carried);
        QCOMPARE(along.tracks.at(0).from, mm(-3, 0));
        QCOMPARE(along.tracks.at(0).to, mm(-3, 10));
        QCOMPARE(along.tracks.at(1).to, mm(7, 0));
    }

    void tracksTakeTheClassOfTheirNet()
    {
        Design design = newDesign(QStringLiteral("t"));
        const NetClass *netClass = trackClass(design, QString());
        QVERIFY(netClass != nullptr);
        QCOMPARE(netClass->name, design.project.defaultClass);
        // A net the schematic lacks takes the default class too.
        QCOMPARE(trackClass(design, QStringLiteral("nowhere"))->name, design.project.defaultClass);
    }
};

QTEST_APPLESS_MAIN(TestTrackEdit)
#include "test_trackedit.moc"
