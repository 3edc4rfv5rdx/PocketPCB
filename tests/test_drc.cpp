#include "drc.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(double x, double y)
{
    return Point{mmToNm(x), mmToNm(y)};
}

// A design with a 40 x 40 mm board, copper on both sides, and rules of
// round numbers: clearance 0.3 mm, edge 0.5 mm, ring 0.2 mm, tracks from
// 0.3 mm, the default class asking for 0.5 mm tracks.
Design design()
{
    Design result = newDesign(QStringLiteral("t"));
    Rules &rules = result.project.rules;
    rules.clearance = mmToNm(0.3);
    rules.edgeClearance = mmToNm(0.5);
    rules.annularRing = mmToNm(0.2);
    rules.trackWidths = {mmToNm(0.3), mmToNm(0.5)};
    rules.drills = {mmToNm(0.8)};
    rules.drillsOnly = false;
    for (NetClass &netClass : result.project.netClasses) {
        netClass.clearance = mmToNm(0.3);
        netClass.trackWidth = mmToNm(0.5);
    }
    result.board.copper = CopperSides::Both;
    result.board.outline = {OutlineCorner{mm(0, 0), 0}, OutlineCorner{mm(40, 0), 0},
                            OutlineCorner{mm(40, 40), 0}, OutlineCorner{mm(0, 40), 0}};
    return result;
}

Track track(quint64 id, const QString &net, Point from, Point to, double width = 0.5,
            BoardSide side = BoardSide::Top)
{
    Track result;
    result.id = ObjectId{id};
    result.net = net;
    result.from = from;
    result.to = to;
    result.width = mmToNm(width);
    result.side = side;
    return result;
}

QList<DrcProblem> ofKind(const QList<DrcProblem> &problems, DrcKind kind)
{
    QList<DrcProblem> found;
    for (const DrcProblem &problem : problems) {
        if (problem.kind == kind)
            found.append(problem);
    }
    return found;
}

} // namespace

class TestDrc : public QObject
{
    Q_OBJECT

private slots:
    // Nothing on the board breaks nothing.
    void anEmptyBoardIsClean()
    {
        QVERIFY(drcProblems(design()).isEmpty());
    }

    // Two tracks of two nets: crossing is a short, near is a clearance
    // problem at the place between them, apart is fine; on two sides they
    // do not meet; of one net they may touch.
    void tracksOfTwoNetsKeepApart()
    {
        Design d = design();
        d.board.tracks = {track(1, QStringLiteral("A"), mm(5, 10), mm(30, 10)),
                          track(2, QStringLiteral("B"), mm(10, 5), mm(10, 20))};
        QList<DrcProblem> problems = drcProblems(d);
        QCOMPARE(ofKind(problems, DrcKind::Short).size(), 1);
        QCOMPARE(problems.first().kind, DrcKind::Short);
        QVERIFY(problems.first().error);
        QCOMPARE(problems.first().at, mm(10, 10));

        // 0.2 mm of room between their edges, 0.3 mm asked.
        d.board.tracks[1] = track(2, QStringLiteral("B"), mm(5, 10.7), mm(30, 10.7));
        problems = drcProblems(d);
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().kind, DrcKind::Clearance);
        QCOMPARE(problems.first().actual, mmToNm(0.2));
        QCOMPARE(problems.first().required, mmToNm(0.3));
        QCOMPARE(problems.first().items.size(), 2);

        d.board.tracks[1] = track(2, QStringLiteral("B"), mm(5, 11), mm(30, 11));
        QVERIFY(drcProblems(d).isEmpty());
        d.board.tracks[1] =
            track(2, QStringLiteral("B"), mm(10, 5), mm(10, 20), 0.5, BoardSide::Bottom);
        QVERIFY(drcProblems(d).isEmpty());
        d.board.tracks[1] = track(2, QStringLiteral("A"), mm(10, 5), mm(10, 20));
        QVERIFY(drcProblems(d).isEmpty());

        // A check set off finds nothing, but a short is always found.
        d.board.tracks[1] = track(2, QStringLiteral("B"), mm(5, 10.7), mm(30, 10.7));
        d.project.rules.clearanceLevel = CheckLevel::Off;
        QVERIFY(drcProblems(d).isEmpty());
        d.board.tracks[1] = track(2, QStringLiteral("B"), mm(10, 5), mm(10, 20));
        QCOMPARE(ofKind(drcProblems(d), DrcKind::Short).size(), 1);
    }

    // Copper near the edge or out of it; a hole without a pad keeps copper
    // off but shorts nothing.
    void edgeAndHolesKeepCopperOff()
    {
        Design d = design();
        d.board.tracks = {track(1, QStringLiteral("A"), mm(0.5, 5), mm(0.5, 20))};
        QList<DrcProblem> problems = drcProblems(d);
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().kind, DrcKind::EdgeClearance);
        QVERIFY(!problems.first().error);
        QCOMPARE(problems.first().actual, mmToNm(0.25));
        d.board.tracks = {track(1, QStringLiteral("A"), mm(50, 5), mm(60, 5))};
        QCOMPARE(ofKind(drcProblems(d), DrcKind::EdgeClearance).first().actual, 0);

        d.board.tracks = {track(1, QStringLiteral("A"), mm(5, 20), mm(30, 20))};
        MountingHole hole;
        hole.id = ObjectId{9};
        hole.at = mm(20, 21.5);
        hole.drill = mmToNm(2);
        d.board.holes = {hole};
        problems = drcProblems(d);
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().kind, DrcKind::Clearance);
        QCOMPARE(problems.first().items.at(1).kind, DrcItem::Kind::Hole);
        d.board.holes[0].at = mm(20, 20);
        QCOMPARE(ofKind(drcProblems(d), DrcKind::Short).size(), 0);
    }

    // Rings, widths and drills.
    void ringsWidthsAndDrills()
    {
        Design d = design();
        Via via;
        via.id = ObjectId{3};
        via.at = mm(20, 20);
        via.net = QStringLiteral("A");
        via.drill = mmToNm(0.8);
        via.size = mmToNm(1.0);
        d.board.vias = {via};
        d.board.tracks = {track(1, QStringLiteral("A"), mm(5, 5), mm(15, 5), 0.2),
                          track(2, QStringLiteral("A"), mm(5, 30), mm(15, 30), 0.4)};
        QList<DrcProblem> problems = drcProblems(d);
        const QList<DrcProblem> rings = ofKind(problems, DrcKind::AnnularRing);
        QCOMPARE(rings.size(), 1);
        QCOMPARE(rings.first().actual, mmToNm(0.1));
        QCOMPARE(ofKind(problems, DrcKind::MinimumWidth).size(), 1);
        QCOMPARE(ofKind(problems, DrcKind::MinimumWidth).first().items.first().id, ObjectId{1});
        QCOMPARE(ofKind(problems, DrcKind::ClassWidth).size(), 1);
        QCOMPARE(ofKind(problems, DrcKind::ClassWidth).first().items.first().id, ObjectId{2});
        // Errors come first.
        QCOMPARE(problems.first().kind, DrcKind::MinimumWidth);
        QVERIFY(ofKind(problems, DrcKind::DrillNotInSet).isEmpty());

        d.board.vias[0].drill = mmToNm(0.6);
        d.project.rules.drillsOnly = true;
        const QList<DrcProblem> drills = ofKind(drcProblems(d), DrcKind::DrillNotInSet);
        QCOMPARE(drills.size(), 1);
        QCOMPARE(drills.first().actual, mmToNm(0.6));
        QCOMPARE(drills.first().count, 1);
    }

    // The pads of one part are not checked against each other, those of
    // two are; a part wholly off the outline is one warning and nothing
    // else.
    void padsOfPartsAndPartsOffTheBoard()
    {
        Design d = design();
        Pad pad;
        pad.number = QStringLiteral("1");
        pad.sizeX = pad.sizeY = mmToNm(1.6);
        pad.hole = mmToNm(0.8);
        Pad near = pad;
        near.number = QStringLiteral("2");
        near.at = mm(1.5, 0);
        Footprint footprint;
        footprint.pads = {pad, near};
        d.library.footprints.insert(QStringLiteral("close"), footprint);
        Placement placement;
        placement.id = ObjectId{10};
        placement.footprint = QStringLiteral("close");
        placement.designator = QStringLiteral("U1");
        placement.at = mm(10, 10);
        placement.padNets = {{QStringLiteral("1"), QStringLiteral("A")},
                             {QStringLiteral("2"), QStringLiteral("B")}};
        d.board.placements = {placement};
        QVERIFY(drcProblems(d).isEmpty());

        Placement other = placement;
        other.id = ObjectId{11};
        other.designator = QStringLiteral("U2");
        other.at = mm(10, 11);
        other.padNets = {{QStringLiteral("1"), QStringLiteral("C")},
                         {QStringLiteral("2"), QStringLiteral("D")}};
        d.board.placements.append(other);
        const QList<DrcProblem> shorts = ofKind(drcProblems(d), DrcKind::Short);
        QVERIFY(!shorts.isEmpty());
        QCOMPARE(shorts.first().items.first().name, QStringLiteral("U1.1"));

        d.board.placements[1].at = mm(60, 10);
        const QList<DrcProblem> problems = drcProblems(d);
        QCOMPARE(problems.size(), 1);
        QCOMPARE(problems.first().kind, DrcKind::OffBoard);
        QCOMPARE(problems.first().name, QStringLiteral("U2"));
    }

    // A connection still to lay is a warning about its net.
    void unroutedConnectionsAreWarned()
    {
        Design d = design();
        WireLink link;
        link.id = ObjectId{4};
        link.from = mm(5, 5);
        link.to = mm(15, 5);
        link.net = QStringLiteral("A");
        link.drill = mmToNm(0.8);
        link.size = mmToNm(1.8);
        d.board.links = {link};
        // A link's two pads are one net and joined: nothing to report.
        QVERIFY(drcProblems(d).isEmpty());
        // A second link of the net, apart: one connection still to lay.
        link.id = ObjectId{5};
        link.from = mm(5, 25);
        link.to = mm(15, 25);
        d.board.links.append(link);
        const QList<DrcProblem> unrouted = ofKind(drcProblems(d), DrcKind::Unrouted);
        QCOMPARE(unrouted.size(), 1);
        QVERIFY(!unrouted.first().error);
        QCOMPARE(unrouted.first().name, QStringLiteral("A"));
    }
};

QTEST_GUILESS_MAIN(TestDrc)
#include "test_drc.moc"
