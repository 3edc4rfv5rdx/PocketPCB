#include "boardprintdialog.h"
#include "document.h"
#include "objectid.h"
#include "paint.h"

#include <QBuffer>
#include <QCheckBox>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QtTest>

using namespace ppcb;

namespace {

const double DOTS_PER_MM = 10;

// Layer 'print.layer' of 'board' printed as 'print' asks onto a white picture
// 20 mm square, 1:1 at DOTS_PER_MM.
QImage printed(const Board &board, const BoardPrint &print)
{
    QImage image(int(20 * DOTS_PER_MM), int(20 * DOTS_PER_MM), QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.scale(DOTS_PER_MM, DOTS_PER_MM);
    paintBoardLayerPrint(painter, board, Library(), print, QRectF(2, 2, 16, 16));
    painter.end();
    return image;
}

QColor at(const QImage &image, double xMm, double yMm)
{
    return image.pixelColor(int(xMm * DOTS_PER_MM), int(yMm * DOTS_PER_MM));
}

} // namespace

class TestBoardPrintDialog : public QObject
{
    Q_OBJECT

private slots:
    // A via is copper with a centre hole; a track of the other side is
    // not printed.
    void viaHasItsCentreHoleOtherSideLeftOut()
    {
        Board board;
        Via via;
        via.id = newObjectId();
        via.at = Point{mmToNm(10), mmToNm(10)};
        via.size = mmToNm(2);
        via.drill = mmToNm(1);
        board.vias.append(via);
        Track track;
        track.id = newObjectId();
        track.side = BoardSide::Bottom;
        track.width = mmToNm(1);
        track.from = Point{mmToNm(4), mmToNm(5)};
        track.to = Point{mmToNm(16), mmToNm(5)};
        board.tracks.append(track);

        BoardPrint print;
        print.side = BoardSide::Top;
        print.registrationMarks = false;
        QImage image = printed(board, print);
        QCOMPARE(at(image, 10, 10), QColor(Qt::white));
        QCOMPARE(at(image, 10.75, 10), QColor(Qt::black));
        QCOMPARE(at(image, 10, 5), QColor(Qt::white));

        print.inverted = true;
        image = printed(board, print);
        QCOMPARE(at(image, 10, 10), QColor(Qt::black));
        QCOMPARE(at(image, 10.75, 10), QColor(Qt::white));
        QCOMPARE(at(image, 10, 5), QColor(Qt::black));

        print.side = BoardSide::Bottom;
        print.inverted = false;
        image = printed(board, print);
        QCOMPARE(at(image, 10, 5), QColor(Qt::black));
    }

    // The silkscreen of the top has the wire of a wire link; its copper
    // has only the link's pads.
    void silkscreenHasTheLinkWire()
    {
        Board board;
        WireLink link;
        link.id = newObjectId();
        link.from = Point{mmToNm(4), mmToNm(15)};
        link.to = Point{mmToNm(16), mmToNm(15)};
        link.size = mmToNm(2);
        link.drill = mmToNm(1);
        board.links.append(link);
        BoardPrint print;
        print.registrationMarks = false;
        QImage image = printed(board, print);
        QCOMPARE(at(image, 10, 15), QColor(Qt::white));
        QCOMPARE(at(image, 4.75, 15), QColor(Qt::black));
        print.layer = BoardPrintLayer::Silkscreen;
        image = printed(board, print);
        QCOMPARE(at(image, 10, 15), QColor(Qt::black));
    }

    // A hole without a pad: a circle at its edge and a cross over its
    // middle, on either side.
    void bareHoleHasCircleAndCross()
    {
        Board board;
        MountingHole hole;
        hole.id = newObjectId();
        hole.at = Point{mmToNm(10), mmToNm(10)};
        hole.drill = mmToNm(3);
        board.holes.append(hole);
        BoardPrint print;
        print.registrationMarks = false;
        for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
            print.side = side;
            const QImage image = printed(board, print);
            QCOMPARE(at(image, 11.4, 10), QColor(Qt::black));
            QCOMPARE(at(image, 10, 10), QColor(Qt::black));
            QCOMPARE(at(image, 10, 8.65), QColor(Qt::black));
            QCOMPARE(at(image, 10.7, 10.7), QColor(Qt::white));
            QCOMPARE(at(image, 12, 10), QColor(Qt::white));
        }
    }

    // Copies that do not share a page go on pages of their own.
    void copiesGoOnPages()
    {
        Document document;
        Design design = document.design();
        for (const auto &[x, y] : {std::pair{0, 0}, std::pair{150, 0}, std::pair{150, 150},
                                   std::pair{0, 150}}) {
            OutlineCorner corner;
            corner.at = Point{mmToNm(x), mmToNm(y)};
            design.board.outline.append(corner);
        }
        document.change(QStringLiteral("Outline"), design);

        QBuffer buffer;
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QPdfWriter writer(&buffer);
        writer.setPageLayout(QPageLayout(QPageSize(QPageSize::A4), QPageLayout::Portrait,
                                         QMarginsF()));
        BoardPrint print;
        print.copies = 3;
        bool fits = false;
        QVERIFY(paintBoardPages(writer, QSizeF(210, 297), writer.resolution() / 25.4, document,
                                print, fits));
        QVERIFY(fits);
        buffer.close();
        const QString text = QString::fromLatin1(buffer.data());
        QCOMPARE(text.count(QRegularExpression(QStringLiteral("/Type\\s*/Page\\b"))), 3);
    }

    // The dialog shows a picture of a copy, which follows the choices.
    void thePreviewFollowsTheChoices()
    {
        Design design = newDesign(QStringLiteral("amp"));
        for (const Point &corner : {Point{0, 0}, Point{mmToNm(40), 0},
                                    Point{mmToNm(40), mmToNm(20)}, Point{0, mmToNm(20)}})
            design.board.outline.append(OutlineCorner{corner, 0});
        BoardPrintDialog dialog(design);
        auto *preview = dialog.findChild<QLabel *>(QStringLiteral("printPreview"));
        QVERIFY(preview != nullptr);
        const QImage plain = preview->pixmap().toImage();
        QVERIFY(!plain.isNull());
        // Twice as wide as high, as the board.
        QVERIFY(plain.width() > plain.height());
        dialog.findChild<QCheckBox *>(QStringLiteral("printInverted"))->setChecked(true);
        const QImage negative = preview->pixmap().toImage();
        QVERIFY(negative != plain);
        // The middle of the board is black in the negative.
        QCOMPARE(negative.pixelColor(negative.width() / 2, negative.height() / 2),
                 QColor(Qt::black));
    }
};

QTEST_MAIN(TestBoardPrintDialog)
#include "test_boardprintdialog.moc"
