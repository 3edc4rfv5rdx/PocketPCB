#include "isometric.h"

#include <QImage>
#include <QPainter>
#include <QtTest>

using namespace ppcb;

namespace {

// The faces of 'faces' turned to a viewer above, in front and to the right.
int seen(const QList<SolidFace> &faces)
{
    int count = 0;
    for (const SolidFace &face : faces) {
        if (face.normal.x + face.normal.y + face.normal.z > 0)
            ++count;
    }
    return count;
}

} // namespace

class TestIsometric : public QObject
{
    Q_OBJECT

private slots:
    void aBoxShowsThreeFaces()
    {
        const QList<SolidFace> box = boxFaces(QRectF(0, 0, 4, 2), 0, 3, Qt::black);
        QCOMPARE(box.size(), 6);
        QCOMPARE(seen(box), 3);
    }

    void cylindersShowTheirTopOrNearEndAndHalfTheirSide()
    {
        const QList<SolidFace> upright = uprightFaces(QRectF(0, 0, 4, 4), 0, 6, Qt::black);
        // The round side in pieces, the top and the bottom.
        QCOMPARE(upright.size(), 34);
        const QList<SolidFace> lying = lyingFaces(QRectF(0, 0, 6, 2), 0, Qt::black);
        QCOMPARE(lying.size(), 34);
        QVERIFY(seen(lying) > 10 && seen(lying) < 24);
        // Its axis along the longer side, as high as it is thick.
        double top = 0;
        for (const SolidFace &face : lying) {
            for (const Point3 &corner : face.corners)
                top = std::max(top, corner.z);
        }
        QVERIFY(std::abs(top - 2) < 1e-9);
    }

    void aFootprintStandsOnABoardWithItsBody()
    {
        Footprint footprint;
        Pad pad;
        pad.at = Point{0, 0};
        pad.sizeX = pad.sizeY = 1800000;
        pad.hole = 1000000;
        footprint.pads = {pad};
        // The board, the pad and its hole.
        QCOMPARE(footprintFaces(footprint).size(), 8);
        Body body;
        body.shape = BodyShape::Box;
        body.sizeX = body.sizeY = 3000000;
        body.height = 2000000;
        body.color = QStringLiteral("#c03030");
        footprint.body = body;
        const QList<SolidFace> faces = footprintFaces(footprint);
        QCOMPARE(faces.size(), 14);
        QCOMPARE(faces.last().color, QColor(QStringLiteral("#c03030")));

        // Painted: something of the body's colour is there.
        QImage image(200, 150, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        paintIsometric(painter, faces, QRectF(0, 0, 200, 150));
        painter.end();
        bool red = false;
        for (int y = 0; y < image.height() && !red; ++y) {
            for (int x = 0; x < image.width() && !red; ++x) {
                const QColor color = image.pixelColor(x, y);
                red = color.red() > 2 * color.green() + 20 && color.red() > 60;
            }
        }
        QVERIFY(red);
    }
};

QTEST_GUILESS_MAIN(TestIsometric)
#include "test_isometric.moc"
