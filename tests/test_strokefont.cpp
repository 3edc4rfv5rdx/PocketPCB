#include "strokefont.h"

#include <QtTest>

using namespace ppcb;

namespace {

// True when every point of 'laid' lies within the rectangle 'box'.
bool inside(const StrokeText &laid, const QRectF &box)
{
    for (const QPolygonF &stroke : laid.strokes) {
        for (const QPointF &point : stroke) {
            if (point.x() < box.left() || point.x() > box.right() || point.y() < box.top()
                || point.y() > box.bottom())
                return false;
        }
    }
    return true;
}

} // namespace

class TestStrokeFont : public QObject
{
    Q_OBJECT

private slots:
    // The font reaches from the space to the end of Cyrillic.
    void theGlyphsReachTheEndOfCyrillic()
    {
        QCOMPARE(STROKE_GLYPH_COUNT, 0x500 - 0x20);
    }

    // A capital stands on the baseline, one unit high, within its width.
    void capitalsStandOnTheBaseline()
    {
        const StrokeText h = strokeText(QStringLiteral("H"));
        QCOMPARE(h.strokes.size(), 3);
        QVERIFY(h.width > 0);
        QVERIFY(inside(h, QRectF(0, -1, h.width, 1)));
        QCOMPARE(h.strokes.first().first(), QPointF(h.strokes.first().first().x(), 0));
    }

    // Letters follow each other, each moved on by the widths before it.
    void lettersFollowEachOther()
    {
        const StrokeText one = strokeText(QStringLiteral("H"));
        const StrokeText two = strokeText(QStringLiteral("HH"));
        QCOMPARE(two.width, 2 * one.width);
        QCOMPARE(two.strokes.size(), 2 * one.strokes.size());
        QCOMPARE(two.strokes.at(3), one.strokes.at(0).translated(one.width, 0));
        QCOMPARE(strokeText(QString()).width, 0.0);
    }

    // Russian and Ukrainian letters, the micro and degree signs are there.
    void cyrillicAndSignsAreDrawn()
    {
        for (const QString &letter : {QStringLiteral("Ж"), QStringLiteral("ї"),
                                      QStringLiteral("Є"), QStringLiteral("ґ"),
                                      QStringLiteral("µ"), QStringLiteral("°")})
            QVERIFY2(!strokeText(letter).strokes.isEmpty(), qPrintable(letter));
    }

    // The ohm sign is the omega; a letter the font lacks is a question mark.
    void lettersStandIn()
    {
        QCOMPARE(strokeText(QStringLiteral("Ω")).strokes,
                 strokeText(QStringLiteral("Ω")).strokes);
        QCOMPARE(strokeText(QStringLiteral("一")).strokes,
                 strokeText(QStringLiteral("?")).strokes);
    }
};

QTEST_GUILESS_MAIN(TestStrokeFont)
#include "test_strokefont.moc"
