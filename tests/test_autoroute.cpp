#include "autoroute.h"

#include "outline.h"
#include "paint.h"
#include "ratsnest.h"
#include "trackedit.h"

#include <QtTest>

using namespace ppcb;

namespace {

Point mm(double x, double y)
{
    return Point{mmToNm(x), mmToNm(y)};
}

// A footprint "fp" with pads "1" at (-2.54, 0) and "2" at (2.54, 0) mm,
// 1.6 mm round, through the board.
Library library()
{
    Footprint footprint;
    Pad pad;
    pad.number = QStringLiteral("1");
    pad.at = mm(-2.54, 0);
    pad.sizeX = mmToNm(1.6);
    pad.sizeY = mmToNm(1.6);
    pad.hole = mmToNm(0.8);
    footprint.pads.append(pad);
    pad.number = QStringLiteral("2");
    pad.at = mm(2.54, 0);
    footprint.pads.append(pad);
    Library result;
    result.footprints.insert(QStringLiteral("fp"), footprint);
    return result;
}

Placement placement(const Point &at, const QString &net1, const QString &net2)
{
    Placement result;
    result.id = newObjectId();
    result.footprint = QStringLiteral("fp");
    result.at = at;
    result.padNets = {{QStringLiteral("1"), net1}, {QStringLiteral("2"), net2}};
    return result;
}

// A board 40 by 30 mm with two parts on a row: pad 1 of the left one at
// (7.62, 15.24) joined by net A to pad 1 of the right one at (27.94, 15.24);
// pad 2 of the left one, on net X, lies between them.
Design twoParts()
{
    Design design = newDesign(QStringLiteral("t"));
    design.library = library();
    design.board.outline = rectangleOutline(Point(), mmToNm(40), mmToNm(30));
    design.board.placements = {placement(mm(10.16, 15.24), QStringLiteral("A"), QStringLiteral("X")),
                               placement(mm(30.48, 15.24), QStringLiteral("A"), QStringLiteral("Y"))};
    return design;
}

Track wall(BoardSide side)
{
    Track track;
    track.id = newObjectId();
    track.side = side;
    track.net = QStringLiteral("W");
    track.width = mmToNm(0.5);
    track.from = mm(20, 0);
    track.to = mm(20, 30);
    return track;
}

// The tracks of 'board' not in 'before'.
QList<Track> newTracks(const Board &before, const Board &board)
{
    QList<Track> tracks;
    for (const Track &track : board.tracks) {
        if (findById(before.tracks, track.id) == nullptr)
            tracks.append(track);
    }
    return tracks;
}

// The board of twoParts() with copper on the top only, a wall of net W laid
// by hand across it (wall()), and pad 1 of net A shut in by a ring of
// tracks of net B the autorouter laid.
Design shutInByARing()
{
    Design design = twoParts();
    design.board.copper = CopperSides::Top;
    design.board.tracks = {wall(BoardSide::Top)};
    const QList<Point> ring = {mm(6, 13.64), mm(9.24, 13.64), mm(9.24, 16.84), mm(6, 16.84)};
    for (int i = 0; i < ring.size(); ++i) {
        Track piece;
        piece.id = newObjectId();
        piece.side = BoardSide::Top;
        piece.net = QStringLiteral("B");
        piece.width = mmToNm(0.25);
        piece.from = ring.at(i);
        piece.to = ring.at((i + 1) % ring.size());
        piece.autorouted = true;
        design.board.tracks.append(piece);
    }
    return design;
}

// Every end of the new tracks of 'board' lies on copper of its net: a pad,
// the pad of a wire link, a via or another track.
bool newTracksJoined(const Board &before, const Board &board, const Library &library)
{
    const QList<NetPad> pads = netPads(board, library);
    for (const Track &track : newTracks(before, board)) {
        for (const Point &end : {track.from, track.to}) {
            const QPointF at = toMm(end);
            bool joined = false;
            for (const NetPad &pad : pads) {
                joined = joined
                         || (pad.net == track.net
                             && QLineF(toMm(pad.at), at).length()
                                    <= nmToMm(std::max(pad.sizeX, pad.sizeY)) / 2);
            }
            for (const Via &via : board.vias)
                joined = joined || (via.net == track.net && via.at == end);
            for (const Track &other : board.tracks) {
                joined = joined
                         || (other.id != track.id && other.net == track.net
                             && QLineF(nearestOnPiece(toMm(other.from), toMm(other.to), at), at)
                                    .length()
                                    <= nmToMm(other.width) / 2);
            }
            if (!joined)
                return false;
        }
    }
    return true;
}

} // namespace

class TestAutoroute : public QObject
{
    Q_OBJECT

private slots:
    void aConnectionIsLaidOnOneSideAroundAPad()
    {
        const Design design = twoParts();
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.total, 1);
        QCOMPARE(result.routed, 1);
        QCOMPARE(result.unrouted, 0);
        QCOMPARE(result.vias, 0);
        QVERIFY(!result.stopped);
        QVERIFY(ratsnest(result.board, design.library).isEmpty());
        QVERIFY(result.board.vias.isEmpty());

        // Clear of pad 2 of the left part, of net X, by the clearance.
        const QPointF pad(12.7, 15.24);
        const double clearance = nmToMm(design.project.rules.clearance);
        for (const Track &track : newTracks(design.board, result.board)) {
            QCOMPARE(track.net, QStringLiteral("A"));
            QVERIFY(track.autorouted);
            const double apart =
                QLineF(nearestOnPiece(toMm(track.from), toMm(track.to), pad), pad).length();
            QVERIFY(apart >= 0.8 + nmToMm(track.width) / 2 + clearance - 1e-6);
        }
    }

    void aTrackKeepsAwayFromAnotherNetWhenThereIsRoom()
    {
        // Pad 2 of the left part and pad 1 of the right one on net A, at
        // (12.7, 15.24) and (27.94, 15.24); a track of net W runs along
        // above the straight way, 1.27 mm off it.
        Design design = twoParts();
        design.board.placements[0].padNets = {{QStringLiteral("1"), QStringLiteral("X")},
                                              {QStringLiteral("2"), QStringLiteral("A")}};
        Track near = wall(BoardSide::Top);
        near.width = mmToNm(0.25);
        near.from = mm(15, 13.97);
        near.to = mm(25, 13.97);
        design.board.tracks = {near};
        design.board.copper = CopperSides::Top;
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.unrouted, 0);
        // It goes round below, not straight along the other track.
        bool below = false;
        for (const Track &track : newTracks(design.board, result.board))
            below = below || track.from.y > mm(0, 15.24).y || track.to.y > mm(0, 15.24).y;
        QVERIFY(below);
    }

    void aConnectionGoesOnFromATrackInsteadOfBesideIt()
    {
        // A third pad of net A below between the two: its connection goes
        // on from the copper laid, no piece over another.
        Design design = twoParts();
        design.board.placements.append(
            placement(mm(20.32, 25.4), QStringLiteral("A"), QStringLiteral("Z")));
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.unrouted, 0);
        const QList<Track> laid = newTracks(design.board, result.board);
        for (int i = 0; i < laid.size(); ++i) {
            for (int j = i + 1; j < laid.size(); ++j) {
                const Track &a = laid.at(i);
                const Track &b = laid.at(j);
                if (a.side != b.side)
                    continue;
                // On one line and sharing more than a point.
                const QLineF one(toMm(a.from), toMm(a.to));
                const QLineF other(toMm(b.from), toMm(b.to));
                const bool inLine =
                    QLineF(one.p1(), other.p1()).length() + QLineF(one.p2(), other.p1()).length()
                            - one.length()
                        < 1e-6;
                const bool inLine2 =
                    QLineF(one.p1(), other.p2()).length() + QLineF(one.p2(), other.p2()).length()
                            - one.length()
                        < 1e-6;
                QVERIFY2(!(inLine && inLine2), "a piece lies over another");
            }
        }
    }

    void aWallOnTheTopIsPassedBelowWithoutVias()
    {
        Design design = twoParts();
        design.board.tracks = {wall(BoardSide::Top)};
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.unrouted, 0);
        QVERIFY(result.board.vias.isEmpty());
        for (const Track &track : newTracks(design.board, result.board))
            QVERIFY(track.side == BoardSide::Bottom);
    }

    void aWallOnBothSidesIsNotCrossed()
    {
        Design design = twoParts();
        design.board.tracks = {wall(BoardSide::Top), wall(BoardSide::Bottom)};
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.routed, 0);
        QCOMPARE(result.unrouted, 1);
        QCOMPARE(result.board.tracks.size(), 2);

        // On the only side of a board of one, a wire link leaps it.
        design.board.copper = CopperSides::Top;
        design.board.tracks = {wall(BoardSide::Top)};
        const AutorouteResult leapt = autorouted(design);
        QCOMPARE(leapt.unrouted, 0);
        QCOMPARE(leapt.links, 1);
        const WireLink &link = leapt.board.links.first();
        QVERIFY(link.autorouted);
        // Named as the wire link parts of the schematic are.
        QCOMPARE(link.designator, QStringLiteral("JP1"));
        QCOMPARE(link.net, QStringLiteral("A"));
        QVERIFY(link.from.y == link.to.y || link.from.x == link.to.x);
        const Coord length = std::max(std::llabs(link.to.x - link.from.x),
                                      std::llabs(link.to.y - link.from.y));
        QCOMPARE(length % mmToNm(2.54), 0);
        QVERIFY(length >= mmToNm(5.08) && length <= mmToNm(30.48));
        // Its ends on either side of the wall, outside the parts; its wire
        // clear of every pad.
        QVERIFY((link.from.x < mmToNm(20)) != (link.to.x < mmToNm(20)));
        const QLineF wireLine(toMm(link.from), toMm(link.to));
        for (const Placement &placement : leapt.board.placements) {
            const QRectF body = placementRect(placement, design.library);
            QVERIFY(!body.contains(wireLine.p1()) && !body.contains(wireLine.p2()));
            for (const NetPad &pad : netPads(leapt.board, design.library)) {
                if (pad.at == link.from || pad.at == link.to)
                    continue;
                const QPointF at = toMm(pad.at);
                QVERIFY(QLineF(nearestOnPiece(wireLine.p1(), wireLine.p2(), at), at).length()
                        >= 0.8 + nmToMm(WIRE_LINK_WIRE) / 2);
            }
        }
        QVERIFY(ratsnest(leapt.board, design.library).isEmpty());
        // Asked for no links, it does not leap.
        const AutorouteResult kept = autorouted(design, {}, {}, false);
        QCOMPARE(kept.links, 0);
        QCOMPARE(kept.unrouted, 1);
    }

    // A bare link keeps from under a part when a short way round finds one
    // clear of it: a part with pads 6 mm apart stands across the wall on
    // the straight way.
    void aWireLinkKeepsFromUnderAPart()
    {
        Design design = twoParts();
        design.board.copper = CopperSides::Top;
        design.board.tracks = {wall(BoardSide::Top)};
        Footprint upright;
        Pad pad;
        pad.number = QStringLiteral("1");
        pad.at = mm(0, -3);
        pad.sizeX = pad.sizeY = mmToNm(1.6);
        pad.hole = mmToNm(0.8);
        upright.pads.append(pad);
        pad.number = QStringLiteral("2");
        pad.at = mm(0, 3);
        upright.pads.append(pad);
        design.library.footprints.insert(QStringLiteral("upright"), upright);
        Placement raised;
        raised.id = newObjectId();
        raised.footprint = QStringLiteral("upright");
        raised.at = mm(20, 15.24);
        design.board.placements.append(raised);
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.unrouted, 0);
        QCOMPARE(result.links, 1);
        const WireLink &link = result.board.links.first();
        const QRectF body = placementRect(raised, design.library);
        const double half = nmToMm(WIRE_LINK_WIRE) / 2;
        const QRectF wire =
            QRectF(toMm(link.from), toMm(link.to)).normalized().adjusted(-half, -half, half, half);
        QVERIFY(!body.intersects(wire));
    }

    void anInsulatedLinkCrossesWhatABareOneCannot()
    {
        // A board of one side 80 mm wide; pad 1 of net A at (7.62, 15.24) and
        // at (68.58, 15.24); between them a band of tracks of net W 35 mm
        // wide, too wide for a bare link.
        Design design = twoParts();
        design.board.copper = CopperSides::Top;
        design.board.outline = rectangleOutline(Point(), mmToNm(80), mmToNm(30));
        design.board.placements[1].at = mm(71.12, 15.24);
        for (int x = 15; x <= 50; ++x) {
            Track band = wall(BoardSide::Top);
            band.from = mm(x, 0);
            band.to = mm(x, 30);
            design.board.tracks.append(band);
        }
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.unrouted, 0);
        QCOMPARE(result.links, 1);
        const WireLink &link = result.board.links.first();
        QVERIFY(link.insulated);
        QVERIFY(QLineF(toMm(link.from), toMm(link.to)).length() <= 150);
        QVERIFY(std::min(link.from.x, link.to.x) < mmToNm(15)
                && std::max(link.from.x, link.to.x) > mmToNm(50));
    }

    // Pad 1 of net A shut in by a ring the autorouter laid for net B, and a
    // wall of net W laid by hand across a board of one side: A is laid by
    // taking the ring up and leaping the wall by a wire link, which the
    // board keeps wherever the routing stops.
    void aWayFoundByTakingUpKeepsItsWireLinks()
    {
        const Design design = shutInByARing();
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.unrouted, 0);
        QVERIFY(result.links >= 1);
        QVERIFY(ratsnest(result.board, design.library).isEmpty());
        QVERIFY(findById(result.board.tracks, design.board.tracks.first().id) != nullptr);
        QVERIFY(newTracksJoined(design.board, result.board, design.library));

        // Stopped at any point, what is laid is joined: no track ends where
        // a link was to be.
        for (int stopAt = 1;; ++stopAt) {
            QVERIFY(stopAt < 10000);
            int asked = 0;
            const AutorouteResult stopped =
                autorouted(design, {}, [&asked, stopAt](int, int) { return ++asked < stopAt; });
            QVERIFY2(newTracksJoined(design.board, stopped.board, design.library),
                     qPrintable(QString::number(stopAt)));
            if (!stopped.stopped)
                break;
        }
    }

    // Routing A alone leaves the ring of B that shuts it in, and says A is
    // unrouted, wherever it stops.
    void noneButTheNetsAskedForAreTakenUp()
    {
        const Design design = shutInByARing();
        for (int stopAt = 1;; ++stopAt) {
            QVERIFY(stopAt < 10000);
            int asked = 0;
            const AutorouteResult result = autorouted(
                design, {QStringLiteral("A")}, [&asked, stopAt](int, int) { return ++asked < stopAt; });
            QCOMPARE(result.board.tracks, design.board.tracks);
            QCOMPARE(result.board.links, design.board.links);
            QCOMPARE(result.unrouted, 1);
            if (!result.stopped)
                break;
        }
    }

    void onlyTheNetsAskedForAreLaid()
    {
        const Design design = twoParts();
        const AutorouteResult result = autorouted(design, {QStringLiteral("Z")});
        QCOMPARE(result.routed, 0);
        QCOMPARE(result.unrouted, 0);
        QCOMPARE(result.board, design.board);
    }

    void routingStopsWhenAsked()
    {
        const Design design = twoParts();
        int asked = 0;
        const AutorouteResult result = autorouted(design, {}, [&asked](int done, int total) {
            ++asked;
            return done < 0 || total < 0;
        });
        QCOMPARE(asked, 1);
        QVERIFY(result.stopped);
        QCOMPARE(result.routed, 0);
        QCOMPARE(result.unrouted, 1);
        QCOMPARE(result.board, design.board);
    }

    // Stop asked while a connection is being searched: it is left as it was,
    // and nothing else is tried.
    void routingStopsInTheMiddleOfASearch()
    {
        const Design design = twoParts();
        int asked = 0;
        const AutorouteResult result = autorouted(design, {}, [&asked](int, int) {
            ++asked;
            return asked < 2;
        });
        QCOMPARE(asked, 2);
        QVERIFY(result.stopped);
        QCOMPARE(result.routed, 0);
        QCOMPARE(result.unrouted, 1);
        QCOMPARE(result.board, design.board);
    }
};

QTEST_APPLESS_MAIN(TestAutoroute)
#include "test_autoroute.moc"
