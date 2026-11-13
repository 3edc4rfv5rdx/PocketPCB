#include "grid.h"

#include <QtTest>

#include <algorithm>

using namespace ppcb;

class TestGrid : public QObject
{
    Q_OBJECT

private slots:
    void stepsAreAscendingAndContainDefault()
    {
        const std::vector<Coord> &steps = gridSteps();
        QVERIFY(!steps.empty());
        QVERIFY(std::is_sorted(steps.begin(), steps.end()));
        QVERIFY(std::adjacent_find(steps.begin(), steps.end()) == steps.end());
        QVERIFY(std::find(steps.begin(), steps.end(), DEFAULT_GRID_STEP) != steps.end());
        QCOMPARE(steps.front(), mmToNm(0.635));
        QCOMPARE(steps.back(), mmToNm(10.16));
    }

    void nextAndPrevStep()
    {
        QCOMPARE(nextGridStep(DEFAULT_GRID_STEP), mmToNm(5.08));
        QCOMPARE(prevGridStep(DEFAULT_GRID_STEP), mmToNm(1.27));
        // Ends of the list stay put.
        QCOMPARE(nextGridStep(gridSteps().back()), gridSteps().back());
        QCOMPARE(prevGridStep(gridSteps().front()), gridSteps().front());
        // A value not in the list moves to its neighbours.
        QCOMPARE(nextGridStep(mmToNm(2.0)), DEFAULT_GRID_STEP);
        QCOMPARE(prevGridStep(mmToNm(2.0)), mmToNm(1.27));
        // Beyond the ends.
        QCOMPARE(nextGridStep(mmToNm(100.0)), gridSteps().back());
        QCOMPARE(prevGridStep(0), gridSteps().front());
    }

    void snapPositive()
    {
        const Coord s = DEFAULT_GRID_STEP;
        QCOMPARE(snapToGrid(0, s), Coord(0));
        QCOMPARE(snapToGrid(1, s), Coord(0));
        QCOMPARE(snapToGrid(s / 2 - 1, s), Coord(0));
        QCOMPARE(snapToGrid(s / 2, s), s); // halfway goes up
        QCOMPARE(snapToGrid(s, s), s);
        QCOMPARE(snapToGrid(3 * s / 2 + 1, s), 2 * s);
    }

    // C++ '/' rounds towards zero; snapping must not.
    void snapNegative()
    {
        const Coord s = DEFAULT_GRID_STEP;
        QCOMPARE(snapToGrid(-1, s), Coord(0));
        QCOMPARE(snapToGrid(-s / 2, s), Coord(0)); // halfway goes up (towards +inf)
        QCOMPARE(snapToGrid(-s / 2 - 1, s), -s);
        QCOMPARE(snapToGrid(-s, s), -s);
        QCOMPARE(snapToGrid(-s - 1, s), -s);
        QCOMPARE(snapToGrid(-3 * s / 2 - 1, s), -2 * s);
    }

    // Shifting the input by whole steps shifts the result by the same amount.
    void snapIsTranslationInvariant()
    {
        const Coord s = mmToNm(1.27);
        const Coord values[] = {0, 1, s / 2 - 1, s / 2, s / 2 + 1, s - 1};
        for (Coord v : values) {
            const Coord base = snapToGrid(v, s);
            for (Coord k = -3; k <= 3; ++k)
                QCOMPARE(snapToGrid(v + k * s, s), base + k * s);
        }
    }

    void snapOddStep()
    {
        QCOMPARE(snapToGrid(1, 3), Coord(0));
        QCOMPARE(snapToGrid(2, 3), Coord(3));
        QCOMPARE(snapToGrid(-1, 3), Coord(0));
        QCOMPARE(snapToGrid(-2, 3), Coord(-3));
    }
};

QTEST_APPLESS_MAIN(TestGrid)
#include "test_grid.moc"
