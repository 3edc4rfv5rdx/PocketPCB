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

} // namespace

class TestAutoroute : public QObject
{
    Q_OBJECT

private slots:
    void aConnectionIsLaidOnOneSideAroundAPad()
    {
        const Design design = twoParts();
        const AutorouteResult result = autorouted(design);
        QCOMPARE(result.routed, 1);
        QCOMPARE(result.unrouted, 0);
        QVERIFY(!result.stopped);
        QVERIFY(ratsnest(result.board, design.library).isEmpty());
        QVERIFY(result.board.vias.isEmpty());

        // Clear of pad 2 of the left part, of net X, by the clearance.
        const QPointF pad(12.7, 15.24);
        const double clearance = nmToMm(design.project.rules.clearance);
        for (const Track &track : newTracks(design.board, result.board)) {
            QCOMPARE(track.net, QStringLiteral("A"));
            const double apart =
                QLineF(nearestOnPiece(toMm(track.from), toMm(track.to), pad), pad).length();
            QVERIFY(apart >= 0.8 + nmToMm(track.width) / 2 + clearance - 1e-6);
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

        // Nor one on the only side of a board of one.
        design.board.copper = CopperSides::Top;
        design.board.tracks = {wall(BoardSide::Top)};
        QCOMPARE(autorouted(design).unrouted, 1);
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
};

QTEST_APPLESS_MAIN(TestAutoroute)
#include "test_autoroute.moc"
