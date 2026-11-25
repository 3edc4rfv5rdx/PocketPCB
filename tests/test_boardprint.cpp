#include "boardprint.h"

#include <QtTest>

using namespace ppcb;

class TestBoardPrint : public QObject
{
    Q_OBJECT

private slots:
    void topIsMirroredBottomIsNot()
    {
        QVERIFY(printMirroredByDefault(BoardSide::Top));
        QVERIFY(!printMirroredByDefault(BoardSide::Bottom));
    }

    void silkscreenUsedBySide()
    {
        Board board;
        QVERIFY(!silkscreenUsed(board, BoardSide::Top));
        Placement placement;
        placement.side = BoardSide::Bottom;
        board.placements.append(placement);
        QVERIFY(!silkscreenUsed(board, BoardSide::Top));
        QVERIFY(silkscreenUsed(board, BoardSide::Bottom));
        board.links.append(WireLink());
        QVERIFY(silkscreenUsed(board, BoardSide::Top));
    }

    // Each mark lies outside the board and inside the copy's rectangle.
    void marksAreOutsideTheBoardInsideTheCopy()
    {
        const QRectF area(10, 20, 50, 30);
        const QRectF copy = boardPrintCopyRect(area, true);
        const QList<QPointF> centres = registrationMarkCentres(area);
        QCOMPARE(centres.size(), 4);
        for (const QPointF &centre : centres) {
            const QRectF cross(centre - QPointF(REGISTRATION_MARK_ARM, REGISTRATION_MARK_ARM),
                               QSizeF(2 * REGISTRATION_MARK_ARM, 2 * REGISTRATION_MARK_ARM));
            QVERIFY(!cross.intersects(area));
            QVERIFY(copy.contains(cross));
        }
        QCOMPARE(boardPrintCopyRect(area, false), area);
    }

    // Copies in rows and columns inside the margins, under the header, the
    // rest on the next page.
    void copiesFillThePageThenTheNext()
    {
        const QSizeF a4(210, 297);
        // 190 wide: 3 of 60 with gaps; 263 high: 4.
        const BoardPrintLayout layout = boardPrintLayout(QSizeF(60, 60), a4, 17, BOARD_PRINT_HEADER);
        QVERIFY(layout.fits);
        QVERIFY(!layout.turned);
        QCOMPARE(layout.pages.size(), 2);
        QCOMPARE(layout.pages.at(0).size(), 12);
        QCOMPARE(layout.pages.at(1).size(), 5);
        const QPointF first(BOARD_PRINT_MARGIN, BOARD_PRINT_MARGIN + BOARD_PRINT_HEADER);
        QCOMPARE(layout.pages.at(0).at(0), first);
        QCOMPARE(layout.pages.at(0).at(1), first + QPointF(60 + BOARD_PRINT_GAP, 0));
        QCOMPARE(layout.pages.at(0).at(3), first + QPointF(0, 60 + BOARD_PRINT_GAP));
        QCOMPARE(layout.pages.at(1).at(0), first);
        for (const QList<QPointF> &page : layout.pages) {
            for (const QPointF &corner : page) {
                QVERIFY(corner.x() + 60 <= a4.width() - BOARD_PRINT_MARGIN);
                QVERIFY(corner.y() + 60 <= a4.height() - BOARD_PRINT_MARGIN);
            }
        }
    }

    // A long board too wide for the paper upright goes turned.
    void aLongBoardIsTurned()
    {
        const BoardPrintLayout layout = boardPrintLayout(QSizeF(250, 50), QSizeF(210, 297), 1, BOARD_PRINT_HEADER);
        QVERIFY(layout.fits);
        QVERIFY(layout.turned);
        QCOMPARE(layout.pages.size(), 1);
    }

    // A board larger than the paper either way: a page each, all the same.
    void aBoardTooLargeHasAPageEach()
    {
        const BoardPrintLayout layout = boardPrintLayout(QSizeF(300, 300), QSizeF(210, 297), 2, BOARD_PRINT_HEADER);
        QVERIFY(!layout.fits);
        QCOMPARE(layout.pages.size(), 2);
        QCOMPARE(layout.pages.at(1).size(), 1);
    }

    void atLeastOneCopy()
    {
        QCOMPARE(boardPrintLayout(QSizeF(10, 10), QSizeF(210, 297), 0, BOARD_PRINT_HEADER)
                     .pages.size(),
                 1);
    }

    // Each line of the drill table makes the band at the top higher, and
    // the copies start under it: 60 mm copies fit four down under a band of
    // one line (277 - 18 = 259 >= 4 * 60 + 3 * 5), three under five (243).
    void drillLinesPushTheCopiesDown()
    {
        QCOMPARE(boardPrintHeader(0), BOARD_PRINT_HEADER);
        QCOMPARE(boardPrintHeader(2), BOARD_PRINT_HEADER + 2 * BOARD_PRINT_DRILL_LINE);
        const QSizeF a4(210, 297);
        const BoardPrintLayout one = boardPrintLayout(QSizeF(60, 60), a4, 12, boardPrintHeader(1));
        QCOMPARE(one.pages.at(0).at(0),
                 QPointF(BOARD_PRINT_MARGIN, BOARD_PRINT_MARGIN + boardPrintHeader(1)));
        QCOMPARE(one.pages.size(), 1);
        const BoardPrintLayout five = boardPrintLayout(QSizeF(60, 60), a4, 12, boardPrintHeader(5));
        QCOMPARE(five.pages.size(), 2);
        QCOMPARE(five.pages.at(0).size(), 9);
    }
};

QTEST_GUILESS_MAIN(TestBoardPrint)
#include "test_boardprint.moc"
