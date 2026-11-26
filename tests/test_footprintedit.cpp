#include "footprintedit.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

Pad pad(const QString &number, const Point &at)
{
    Pad result;
    result.number = number;
    result.at = at;
    result.sizeX = 2 * MM;
    result.sizeY = 2 * MM;
    result.hole = MM;
    return result;
}

QStringList numbers(const QList<Pad> &pads)
{
    QStringList result;
    for (const Pad &p : pads)
        result.append(p.number);
    return result;
}

QList<Point> places(const QList<Pad> &pads)
{
    QList<Point> result;
    for (const Pad &p : pads)
        result.append(p.at);
    return result;
}

} // namespace

class TestFootprintEdit : public QObject
{
    Q_OBJECT

private slots:
    void theNextNumberFillsAGap()
    {
        Footprint footprint;
        QCOMPARE(nextPadNumber(footprint), QStringLiteral("1"));
        footprint.pads = {pad(QStringLiteral("1"), mm(0, 0)), pad(QStringLiteral("3"), mm(5, 0)),
                          pad(QStringLiteral("A"), mm(10, 0)), pad(QStringLiteral("02"), mm(15, 0))};
        // "02" and "A" are no numbers 1 counts by.
        QCOMPARE(nextPadNumber(footprint), QStringLiteral("2"));
        footprint.pads.append(pad(QStringLiteral("2"), mm(20, 0)));
        QCOMPARE(nextPadNumber(footprint), QStringLiteral("4"));
    }

    void padsAreFoundAtAPointAndInsideARectangle()
    {
        Footprint footprint;
        footprint.pads = {pad(QStringLiteral("1"), mm(0, 0)), pad(QStringLiteral("2"), mm(0, 0)),
                          pad(QStringLiteral("3"), mm(10, 0))};
        // The one drawn last of two.
        QCOMPARE(padAtPoint(footprint, QPointF(0.5, 0.5)), 1);
        QCOMPARE(padAtPoint(footprint, QPointF(10, 0)), 2);
        QCOMPARE(padAtPoint(footprint, QPointF(5, 0)), -1);
        QCOMPARE(padAtPoint(footprint, QPointF(1.5, 0), 0.6), 1);
        QCOMPARE(padsInside(footprint, QRectF(-2, -2, 4, 4)), QSet<int>({0, 1}));
        QCOMPARE(padsInside(footprint, QRectF(-0.5, -2, 20, 4)), QSet<int>({2}));
        QCOMPARE(padsBounds(footprint, {0, 2}), QRectF(-1, -1, 12, 2));
        QVERIFY(padsBounds(footprint, {}).isNull());
    }

    void padsAreMovedTurnedReplacedAndDeleted()
    {
        Footprint footprint;
        footprint.pads = {pad(QStringLiteral("1"), mm(0, 0)), pad(QStringLiteral("2"), mm(4, 0))};
        footprint.pads[1].sizeX = 3 * MM;

        const Footprint moved = withPadsMoved(footprint, {1}, mm(1, 2));
        QCOMPARE(places(moved.pads), QList<Point>({mm(0, 0), mm(5, 2)}));

        // A quarter clockwise on screen: right of the centre goes below it.
        const Footprint turned = withPadsRotated(footprint, {1}, mm(0, 0));
        QCOMPARE(turned.pads.at(1).at, mm(0, 4));
        QCOMPARE(turned.pads.at(1).sizeX, 2 * MM);
        QCOMPARE(turned.pads.at(1).sizeY, 3 * MM);
        QCOMPARE(turned.pads.at(0), footprint.pads.at(0));
        // Pad 1, round and on the centre, stays as it is.
        QCOMPARE(padsRotated(footprint.pads, mm(0, 0)), turned.pads);

        QCOMPARE(numbers(withoutPads(footprint, {0}).pads), QStringList({QStringLiteral("2")}));
        QCOMPARE(numbers(withPadsAdded(footprint, {pad(QStringLiteral("9"), mm(8, 0))}).pads),
                 QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("9")}));

        Pad changed = footprint.pads.at(0);
        changed.number = QStringLiteral("7");
        // An index out of range is passed over.
        QCOMPARE(numbers(withPadsReplaced(footprint, {0, 5}, {changed, changed}).pads),
                 QStringList({QStringLiteral("7"), QStringLiteral("2")}));
    }

    void theOriginMovesEverythingAndTheMiddleIsTheBody()
    {
        Footprint footprint;
        footprint.pads = {pad(QStringLiteral("1"), mm(0, 0)), pad(QStringLiteral("2"), mm(4, 0))};
        // No body: the middle of the pads.
        QCOMPARE(footprintMiddle(footprint), mm(2, 0));
        Graphic body;
        body.kind = GraphicKind::Rect;
        body.a = mm(-2, -6);
        body.b = mm(6, 2);
        body.width = 0;
        footprint.graphics = {body};
        footprint.refAt = mm(0, -8);
        footprint.valueAt = mm(0, 4);
        QCOMPARE(footprintMiddle(footprint), mm(2, -2));
        QCOMPARE(footprintMiddle(Footprint()), Point{});

        const Footprint moved = withOriginAt(footprint, mm(2, -2));
        QCOMPARE(places(moved.pads), QList<Point>({mm(-2, 2), mm(2, 2)}));
        QCOMPARE(moved.graphics.at(0).a, mm(-4, -4));
        QCOMPARE(moved.graphics.at(0).b, mm(4, 4));
        QCOMPARE(moved.refAt, mm(-2, -6));
        QCOMPARE(moved.valueAt, mm(-2, 6));
        QCOMPARE(footprintMiddle(moved), Point{});
    }

    void padsTooNearAreFound()
    {
        Footprint footprint;
        // Round pads 2 mm across, 2.3 mm apart: 0.3 mm of gap.
        footprint.pads = {pad(QStringLiteral("1"), mm(0, 0)), pad(QStringLiteral("2"), Point{2300000, 0})};
        QVERIFY(padsTooNear(footprint, 300000).isEmpty());
        const QList<PadGap> gaps = padsTooNear(footprint, 400000);
        QCOMPARE(gaps.size(), 1);
        QCOMPARE(gaps.first().first, 0);
        QCOMPARE(gaps.first().second, 1);
        QCOMPARE(gaps.first().gap, 300000);
        QCOMPARE(padsApart(footprint.pads.at(0), footprint.pads.at(1)), 300000);
        // Touching: none.
        QCOMPARE(padsApart(footprint.pads.at(0), footprint.pads.at(0)), 0);
        QCOMPARE(gaps.first().at, (Point{1150000, 0}));
        // One number is one pin.
        footprint.pads[1].number = QStringLiteral("1");
        QVERIFY(padsTooNear(footprint, 400000).isEmpty());
        // Pads on two sides apart.
        footprint.pads[1].number = QStringLiteral("2");
        footprint.pads[0].hole = 0;
        footprint.pads[0].layers = PadLayers::Front;
        footprint.pads[1].hole = 0;
        footprint.pads[1].layers = PadLayers::Back;
        QVERIFY(padsTooNear(footprint, 400000).isEmpty());
        // A long pad is measured along its length.
        const PadStroke stroke = padStroke([] {
            Pad p;
            p.sizeX = 4 * MM;
            p.sizeY = 2 * MM;
            return p;
        }());
        QCOMPARE(stroke.from, mm(-1, 0));
        QCOMPARE(stroke.to, mm(1, 0));
        QCOMPARE(stroke.radius, MM);
    }

    void aPadIsCheckedAsTheFileReaderChecksIt()
    {
        Pad good = pad(QStringLiteral("1"), mm(0, 0));
        QVERIFY(padProblem(good).isEmpty());
        Pad bad = good;
        bad.sizeX = 3 * MM;
        QVERIFY(!padProblem(bad).isEmpty());
        bad.shape = PadShape::Oval;
        QVERIFY(padProblem(bad).isEmpty());
        bad.hole = 2 * MM;
        QVERIFY(!padProblem(bad).isEmpty());
        bad.hole = 0;
        QVERIFY(!padProblem(bad).isEmpty());
        bad.layers = PadLayers::Front;
        QVERIFY(padProblem(bad).isEmpty());
        bad.sizeY = 0;
        QVERIFY(!padProblem(bad).isEmpty());
    }

    void rowsOfPadsGoDownFromPadOne()
    {
        const Pad like = pad(QString(), mm(9, 9));
        PadRow row;
        row.count = 3;
        const QList<Pad> single = padRow(row, like);
        QCOMPARE(numbers(single),
                 QStringList({QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}));
        QCOMPARE(places(single), QList<Point>({Point{0, 0}, Point{0, 2540000}, Point{0, 5080000}}));
        QCOMPARE(single.at(0).shape, PadShape::Rect);
        QCOMPARE(single.at(1).shape, PadShape::Circle);

        row.kind = PadRowKind::Double;
        row.count = 4;
        row.spacing = 2540000;
        row.firstNumber = 5;
        row.firstSquare = false;
        const QList<Pad> two = padRow(row, like);
        QCOMPARE(numbers(two), QStringList({QStringLiteral("5"), QStringLiteral("6"),
                                            QStringLiteral("7"), QStringLiteral("8")}));
        QCOMPARE(places(two), QList<Point>({Point{0, 0}, Point{2540000, 0}, Point{0, 2540000},
                                            Point{2540000, 2540000}}));
        QCOMPARE(two.at(0).shape, PadShape::Circle);

        row.kind = PadRowKind::Dip;
        row.firstNumber = 1;
        row.spacing = 7620000;
        const QList<Pad> dip = padRow(row, like);
        // Down the left column, up the right one.
        QCOMPARE(places(dip), QList<Point>({Point{0, 0}, Point{0, 2540000},
                                            Point{7620000, 2540000}, Point{7620000, 0}}));

        row.count = 3;
        QVERIFY(padRow(row, like).isEmpty());
        row.kind = PadRowKind::Single;
        row.count = 0;
        QVERIFY(padRow(row, like).isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestFootprintEdit)
#include "test_footprintedit.moc"
