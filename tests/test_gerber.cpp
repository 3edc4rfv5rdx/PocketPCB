#include "boardcopper.h"
#include "gerber.h"
#include "objectid.h"
#include "zipfile.h"

#include <QtTest>

using namespace ppcb;

namespace {

QStringList lines(const QByteArray &data)
{
    return QString::fromLatin1(data).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

// A board 20 by 10 mm with a via, a track, and a hole without a pad.
Design smallDesign()
{
    Design design;
    for (const auto &[x, y] : {std::pair{0, 0}, std::pair{20, 0}, std::pair{20, 10},
                               std::pair{0, 10}}) {
        OutlineCorner corner;
        corner.at = Point{mmToNm(x), mmToNm(y)};
        design.board.outline.append(corner);
    }
    Via via;
    via.id = newObjectId();
    via.at = Point{mmToNm(5), mmToNm(5)};
    via.size = mmToNm(1.2);
    via.drill = mmToNm(0.6);
    design.board.vias.append(via);
    Track track;
    track.id = newObjectId();
    track.side = BoardSide::Bottom;
    track.width = mmToNm(0.5);
    track.from = Point{mmToNm(5), mmToNm(5)};
    track.to = Point{mmToNm(15), mmToNm(5)};
    design.board.tracks.append(track);
    MountingHole hole;
    hole.id = newObjectId();
    hole.at = Point{mmToNm(18), mmToNm(8)};
    hole.drill = mmToNm(3.2);
    design.board.holes.append(hole);
    return design;
}

} // namespace

class TestGerber : public QObject
{
    Q_OBJECT

private slots:
    void pointsAreNanometresWithYUp()
    {
        QCOMPARE(gerberPoint(QPointF(12.7, 5.08)), QStringLiteral("X12700000Y-5080000"));
    }

    // An aperture is defined once and chosen only when it changes; a line
    // goes on from where the last one ended.
    void writerReusesAperturesAndPosition()
    {
        GerberWriter writer(QStringLiteral("Copper,L1,Top"));
        writer.line(QPointF(0, 0), QPointF(1, 0), 0.25);
        writer.line(QPointF(1, 0), QPointF(1, 1), 0.25);
        writer.flash(PadShape::Rect, 1.5, 2, QPointF(3, 4));
        writer.flash(PadShape::Rect, 1.5, 2, QPointF(5, 4));
        const QStringList out = lines(writer.data());
        QVERIFY(out.contains(QStringLiteral("%TF.FileFunction,Copper,L1,Top*%")));
        QVERIFY(out.contains(QStringLiteral("%TF.FilePolarity,Positive*%")));
        QVERIFY(out.contains(QStringLiteral("%FSLAX46Y46*%")));
        QVERIFY(out.contains(QStringLiteral("%MOMM*%")));
        QCOMPARE(out.count(QStringLiteral("%ADD10C,0.250000*%")), 1);
        QCOMPARE(out.count(QStringLiteral("%ADD11R,1.500000X2.000000*%")), 1);
        QCOMPARE(out.count(QStringLiteral("D10*")), 1);
        QCOMPARE(out.count(QStringLiteral("X0Y0D02*")), 1);
        QVERIFY(!out.contains(QStringLiteral("X1000000Y0D02*")));
        QVERIFY(out.contains(QStringLiteral("X1000000Y-1000000D01*")));
        QVERIFY(out.contains(QStringLiteral("X5000000Y-4000000D03*")));
        QCOMPARE(out.last(), QStringLiteral("M02*"));
    }

    void regionIsClosed()
    {
        GerberWriter writer(QStringLiteral("Legend,Top"));
        writer.region(QPolygonF{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1)});
        const QStringList out = lines(writer.data());
        const int start = int(out.indexOf(QStringLiteral("G36*")));
        QVERIFY(start >= 0);
        QCOMPARE(out.at(start + 1), QStringLiteral("X0Y0D02*"));
        QCOMPARE(out.at(start + 4), QStringLiteral("X0Y0D01*"));
        QCOMPARE(out.at(start + 5), QStringLiteral("G37*"));
    }

    void drillsRoundUpOnlyWhenTheSetIsAll()
    {
        Rules rules;
        rules.drills = {mmToNm(0.8), mmToNm(1.0), mmToNm(3.2)};
        QCOMPARE(drilledDiameter(mmToNm(0.9), rules), mmToNm(0.9));
        rules.drillsOnly = true;
        QCOMPARE(drilledDiameter(mmToNm(0.9), rules), mmToNm(1.0));
        QCOMPARE(drilledDiameter(mmToNm(1.0), rules), mmToNm(1.0));
        QCOMPARE(drilledDiameter(mmToNm(5), rules), mmToNm(5));
    }

    void excellonHasToolsAndHoles()
    {
        const QList<DrillHole> holes{{Point{mmToNm(1), mmToNm(2)}, mmToNm(0.8), true},
                                     {Point{mmToNm(3), mmToNm(4)}, mmToNm(0.6), true},
                                     {Point{mmToNm(5), mmToNm(6)}, mmToNm(3.2), false}};
        const QStringList plated = lines(excellonFile(holes, true));
        QCOMPARE(plated.first(), QStringLiteral("M48"));
        QVERIFY(plated.contains(QStringLiteral("; #@! TF.FileFunction,Plated,1,2,PTH")));
        QVERIFY(plated.contains(QStringLiteral("METRIC")));
        QVERIFY(plated.contains(QStringLiteral("; FORMAT={-:-/ absolute / metric / decimal}")));
        // Tools by ascending diameter.
        QVERIFY(plated.contains(QStringLiteral("T1C0.600")));
        QVERIFY(plated.contains(QStringLiteral("T2C0.800")));
        QVERIFY(plated.contains(QStringLiteral("X3.000Y-4.000")));
        QVERIFY(!plated.contains(QStringLiteral("X5.000Y-6.000")));
        QCOMPARE(plated.last(), QStringLiteral("M30"));
        const QStringList bare = lines(excellonFile(holes, false));
        QVERIFY(bare.contains(QStringLiteral("T1C3.200")));
        QVERIFY(bare.contains(QStringLiteral("X5.000Y-6.000")));
    }

    // A board of two sides: copper and mask of both, the outline, both
    // drill files; no silkscreen when nothing is on it.
    void filesOfABoard()
    {
        const Design design = smallDesign();
        QStringList names;
        for (const OutputFile &file : gerberFiles(design, QStringLiteral("amp")))
            names.append(file.name);
        QCOMPARE(names, (QStringList{QStringLiteral("amp.GTL"), QStringLiteral("amp.GTS"),
                                     QStringLiteral("amp.GBL"), QStringLiteral("amp.GBS"),
                                     QStringLiteral("amp.GKO"), QStringLiteral("amp-PTH.DRL"),
                                     QStringLiteral("amp-NPTH.DRL")}));
    }

    // The track only on its side; a covered via has no mask opening, the
    // hole without a pad has one.
    void copperAndMaskContent()
    {
        Design design = smallDesign();
        design.project.rules.viasCovered = true;
        design.project.rules.maskExpansion = mmToNm(0.05);
        const QList<OutputFile> files = gerberFiles(design, QStringLiteral("amp"));
        const auto find = [&files](const QString &name) {
            for (const OutputFile &file : files) {
                if (file.name == name)
                    return lines(file.data);
            }
            return QStringList();
        };
        QVERIFY(find(QStringLiteral("amp.GBL")).contains(QStringLiteral("X15000000Y-5000000D01*")));
        QVERIFY(!find(QStringLiteral("amp.GTL")).contains(QStringLiteral("X15000000Y-5000000D01*")));
        QVERIFY(find(QStringLiteral("amp.GTL")).contains(QStringLiteral("X5000000Y-5000000D03*")));
        const QStringList mask = find(QStringLiteral("amp.GTS"));
        QVERIFY(mask.contains(QStringLiteral("%TF.FilePolarity,Negative*%")));
        QVERIFY(!mask.contains(QStringLiteral("X5000000Y-5000000D03*")));
        QVERIFY(mask.contains(QStringLiteral("%ADD10C,3.300000*%")));
        QVERIFY(mask.contains(QStringLiteral("X18000000Y-8000000D03*")));
    }

    // The silkscreen is what the board paints on it: a line of the
    // author's drawing becomes a line of its width.
    void silkscreenFromThePainting()
    {
        Design design = smallDesign();
        BoardGraphic item;
        item.id = newObjectId();
        item.side = BoardSide::Top;
        item.graphic.kind = GraphicKind::Line;
        item.graphic.a = Point{mmToNm(1), mmToNm(1)};
        item.graphic.b = Point{mmToNm(4), mmToNm(1)};
        item.graphic.width = mmToNm(0.2);
        design.board.graphics.append(item);
        QStringList silkscreen;
        for (const OutputFile &file : gerberFiles(design, QStringLiteral("amp"))) {
            if (file.name == QStringLiteral("amp.GTO"))
                silkscreen = lines(file.data);
        }
        QVERIFY(silkscreen.contains(QStringLiteral("%TF.FileFunction,Legend,Top*%")));
        QVERIFY(silkscreen.contains(QStringLiteral("%ADD10C,0.200000*%")));
        QVERIFY(silkscreen.contains(QStringLiteral("X1000000Y-1000000D02*")));
        QVERIFY(silkscreen.contains(QStringLiteral("X4000000Y-1000000D01*")));
    }

    void crcOfTheCheckString()
    {
        QCOMPARE(crc32(QByteArrayLiteral("123456789")), quint32(0xcbf43926));
    }

    // Each file's name and sizes in the central directory, then its end.
    void zipListsItsFiles()
    {
        const QList<OutputFile> files{{QStringLiteral("a.GTL"), QByteArray(1000, 'x')},
                                      {QStringLiteral("b.GKO"), QByteArrayLiteral("M02*\n")}};
        const QByteArray zip = zipArchive(files, QDateTime(QDate(2026, 11, 25), QTime(12, 0)));
        QVERIFY(zip.startsWith(QByteArrayLiteral("PK\x03\x04")));
        QCOMPARE(zip.count(QByteArrayLiteral("PK\x01\x02")), 2);
        QVERIFY(zip.contains(QByteArrayLiteral("a.GTL")));
        QVERIFY(zip.contains(QByteArrayLiteral("b.GKO")));
        const int end = int(zip.lastIndexOf(QByteArrayLiteral("PK\x05\x06")));
        QCOMPARE(end, int(zip.size()) - 22);
        // Two entries, on this disk and in all.
        QCOMPARE(quint8(zip.at(end + 8)), quint8(2));
        QCOMPARE(quint8(zip.at(end + 10)), quint8(2));
        // The repeated text compresses.
        QVERIFY(zip.size() < 1000);
    }
};

QTEST_MAIN(TestGerber)
#include "test_gerber.moc"
