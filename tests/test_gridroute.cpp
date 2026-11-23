#include "gridroute.h"

#include <QtTest>

#include <QSet>

using namespace ppcb;

namespace {

GridNode node(int column, int row, int layer = 0) { return GridNode{column, row, layer}; }

// The rules of a grid 'columns' by 'rows' where the nodes listed in 'walls'
// of layer 0 cannot be stepped on.
GridRouteRules walled(int columns, int rows, const QList<GridNode> &walls)
{
    GridRouteRules rules;
    rules.columns = columns;
    rules.rows = rows;
    rules.stepCost = [walls](const GridNode &, const GridNode &to) {
        return walls.contains(to) ? -1 : 0;
    };
    return rules;
}

// The cost of 'path' under 'rules' with no extra step costs: its length and
// its bends.
int lengthAndBends(const GridRouteRules &rules, const QList<GridNode> &path)
{
    int cost = 0;
    int lastColumn = 0;
    int lastRow = 0;
    for (int i = 1; i < path.size(); ++i) {
        const int dc = path.at(i).column - path.at(i - 1).column;
        const int dr = path.at(i).row - path.at(i - 1).row;
        cost += dc != 0 && dr != 0 ? rules.diagonalCost : rules.straightCost;
        if (i > 1 && (dc != lastColumn || dr != lastRow))
            cost += rules.bend90Cost;
        lastColumn = dc;
        lastRow = dr;
    }
    return cost;
}

// True when every two nodes of 'path' that follow each other are a step
// apart: one of the four nodes around, or of the eight with 'diagonal'.
bool isConnected(const QList<GridNode> &path, bool diagonal)
{
    for (int i = 1; i < path.size(); ++i) {
        const int dc = qAbs(path.at(i).column - path.at(i - 1).column);
        const int dr = qAbs(path.at(i).row - path.at(i - 1).row);
        const int dl = qAbs(path.at(i).layer - path.at(i - 1).layer);
        const bool step = dl == 0 && dc <= 1 && dr <= 1 && dc + dr >= 1 && (diagonal || dc + dr == 1);
        const bool layer = dl >= 1 && dc == 0 && dr == 0;
        if (!step && !layer)
            return false;
    }
    return true;
}

} // namespace

class TestGridRoute : public QObject
{
    Q_OBJECT

private slots:
    void aJumpLeapsAWall()
    {
        // A wall across column 5 of a grid one row high: no way, but for a
        // jump of three columns, which leaps it.
        GridRouteRules rules = walled(10, 1, {node(5, 0)});
        QVERIFY(gridRoute(rules, {node(0, 0)}, {node(9, 0)}).isEmpty());
        rules.jumps = [](const GridNode &at) {
            return QList<GridJump>{{GridNode{at.column + 3, at.row, at.layer}, 100}};
        };
        const QList<GridNode> path = gridRoute(rules, {node(0, 0)}, {node(9, 0)});
        QVERIFY(!path.isEmpty());
        QCOMPARE(path.last(), node(9, 0));
        bool leapt = false;
        for (int i = 1; i < path.size(); ++i)
            leapt = leapt || std::abs(path.at(i).column - path.at(i - 1).column) > 1;
        QVERIFY(leapt);
        // Its ends are corners.
        const QList<GridNode> corners = gridCorners(path);
        QVERIFY(corners.size() >= 3);
    }

    void straightOnAnEmptyGrid()
    {
        const GridRouteRules rules = walled(10, 5, {});
        const QList<GridNode> path = gridRoute(rules, {node(1, 2)}, {node(7, 2)});
        QCOMPARE(path.size(), 7);
        QCOMPARE(path.first(), node(1, 2));
        QCOMPARE(path.last(), node(7, 2));
        QCOMPARE(gridCorners(path), (QList<GridNode>{node(1, 2), node(7, 2)}));
    }

    void startOnAGoal()
    {
        const GridRouteRules rules = walled(4, 4, {});
        QCOMPARE(gridRoute(rules, {node(2, 2)}, {node(0, 0), node(2, 2)}),
                 QList<GridNode>{node(2, 2)});
    }

    // A bend costs, so the way turns once instead of going in steps.
    void bendsAreAvoided()
    {
        GridRouteRules rules = walled(10, 10, {});
        rules.bend90Cost = 5;
        const QList<GridNode> path = gridRoute(rules, {node(0, 0)}, {node(6, 4)});
        QCOMPARE(gridCorners(path).size(), 3);
        QCOMPARE(lengthAndBends(rules, path), 10 * 10 + 5);
    }

    // A wall with one gap: the way goes through the gap, round the wall's
    // end being longer.
    void throughTheGap()
    {
        QList<GridNode> walls;
        for (int row = 0; row < 9; ++row) {
            if (row != 6)
                walls.append(node(4, row));
        }
        const GridRouteRules rules = walled(9, 9, walls);
        const QList<GridNode> path = gridRoute(rules, {node(1, 2)}, {node(7, 2)});
        QVERIFY(path.contains(node(4, 6)));
        QVERIFY(isConnected(path, false));
        for (const GridNode &wall : walls)
            QVERIFY(!path.contains(wall));
        QCOMPARE(path.size() - 1, 6 + 4 + 4);
    }

    void noWayThroughAClosedWall()
    {
        QList<GridNode> walls;
        for (int row = 0; row < 5; ++row)
            walls.append(node(2, row));
        const GridRouteRules rules = walled(5, 5, walls);
        QVERIFY(gridRoute(rules, {node(0, 2)}, {node(4, 2)}).isEmpty());
    }

    // The step cost is extra: a dear stretch is gone round when that is
    // cheaper, crossed when it is not.
    void dearStepsAreGoneRound()
    {
        GridRouteRules rules = walled(9, 5, {});
        int dear = 100;
        rules.stepCost = [&dear](const GridNode &, const GridNode &to) {
            return to.column == 4 && to.row <= 2 ? dear : 0;
        };
        // Round the end at row 3, three steps down and three up, costs 60
        // more than straight across, which costs 'dear' more.
        QList<GridNode> path = gridRoute(rules, {node(0, 0)}, {node(8, 0)});
        QVERIFY(std::none_of(path.cbegin(), path.cend(), [](const GridNode &n) {
            return n.column == 4 && n.row <= 2;
        }));
        dear = 3;
        path = gridRoute(rules, {node(0, 0)}, {node(8, 0)});
        QCOMPARE(gridCorners(path), (QList<GridNode>{node(0, 0), node(8, 0)}));
    }

    void theNearestGoal()
    {
        const GridRouteRules rules = walled(20, 3, {});
        const QList<GridNode> path =
            gridRoute(rules, {node(5, 1)}, {node(19, 1), node(0, 1), node(12, 1)});
        QCOMPARE(path.last(), node(0, 1));
    }

    // Of several starts the one nearest a goal.
    void theNearestStart()
    {
        const GridRouteRules rules = walled(20, 3, {});
        const QList<GridNode> path = gridRoute(rules, {node(0, 1), node(15, 1)}, {node(10, 1)});
        QCOMPARE(path.first(), node(15, 1));
    }

    // A start that costs more is taken only while the way from it is cheaper
    // by more than that; a start with a negative cost is left out.
    void startCosts()
    {
        GridRouteRules rules = walled(20, 3, {});
        int far = 0;
        rules.startCost = [&far](const GridNode &start) { return start.column == 15 ? far : 0; };
        const QList<GridNode> starts = {node(0, 1), node(15, 1)};
        QCOMPARE(gridRoute(rules, starts, {node(10, 1)}).first(), node(15, 1));
        far = 60;
        QCOMPARE(gridRoute(rules, starts, {node(10, 1)}).first(), node(0, 1));
        far = 40;
        QCOMPARE(gridRoute(rules, starts, {node(10, 1)}).first(), node(15, 1));
        far = -1;
        QCOMPARE(gridRoute(rules, starts, {node(14, 1)}).first(), node(0, 1));
    }

    void offTheGridIsLeftOut()
    {
        const GridRouteRules rules = walled(4, 4, {});
        QVERIFY(gridRoute(rules, {node(-1, 0)}, {node(3, 3)}).isEmpty());
        QVERIFY(gridRoute(rules, {node(0, 0)}, {node(4, 0)}).isEmpty());
        QCOMPARE(gridRoute(rules, {node(9, 9), node(0, 0)}, {node(0, 0, 1), node(1, 0)}).last(),
                 node(1, 0));
        QVERIFY(gridRoute(rules, {node(0, 0)}, {}).isEmpty());
        QVERIFY(gridRoute(GridRouteRules{}, {node(0, 0)}, {node(0, 0)}).isEmpty());
    }

    void slantedSteps()
    {
        GridRouteRules rules = walled(10, 10, {});
        rules.diagonal = true;
        const QList<GridNode> path = gridRoute(rules, {node(0, 0)}, {node(5, 5)});
        QCOMPARE(gridCorners(path), (QList<GridNode>{node(0, 0), node(5, 5)}));
        QCOMPARE(path.size(), 6);
        QVERIFY(isConnected(path, true));
    }

    // In four directions the same way has no slanted step.
    void levelAndUprightOnly()
    {
        const GridRouteRules rules = walled(10, 10, {});
        const QList<GridNode> path = gridRoute(rules, {node(0, 0)}, {node(5, 5)});
        QVERIFY(isConnected(path, false));
        QCOMPARE(path.size(), 11);
    }

    // A turn sharper than 90 degrees is never taken: a lane along row 4
    // whose only way down to the goal (3, 5) is a slant back from (4, 4),
    // a turn of 135 degrees, has no way; going back along the lane is a turn
    // of 180. Square down from (3, 4) it has one.
    void noSharpTurns()
    {
        GridRouteRules rules = walled(9, 9, {});
        rules.diagonal = true;
        bool square = false;
        rules.stepCost = [&square](const GridNode &from, const GridNode &to) {
            if (to.row == 4)
                return 0;
            if (to == GridNode{3, 5, 0})
                return from == GridNode{square ? 3 : 4, 4, 0} ? 0 : -1;
            return -1;
        };
        QVERIFY(gridRoute(rules, {node(0, 4)}, {node(3, 5)}).isEmpty());
        square = true;
        QCOMPARE(gridCorners(gridRoute(rules, {node(0, 4)}, {node(3, 5)})),
                 (QList<GridNode>{node(0, 4), node(3, 4), node(3, 5)}));
    }

    // In eight directions a turn by 45 degrees has its own cost: dear, the
    // way turns square instead of slanting.
    void bendBy45()
    {
        GridRouteRules rules = walled(10, 10, {});
        rules.diagonal = true;
        rules.bend45Cost = 1;
        rules.bend90Cost = 1;
        QCOMPARE(gridCorners(gridRoute(rules, {node(0, 0)}, {node(6, 2)})).size(), 3);
        QVERIFY(!isConnected(gridRoute(rules, {node(0, 0)}, {node(6, 2)}), false));
        rules.bend45Cost = 1000;
        const QList<GridNode> path = gridRoute(rules, {node(0, 0)}, {node(6, 2)});
        QVERIFY(isConnected(path, false));
        QCOMPARE(gridCorners(path).size(), 3);
    }

    // A wall across layer 0 alone is passed on layer 1, changing layer
    // where it is allowed; with no change allowed there is no way.
    void layers()
    {
        GridRouteRules rules;
        rules.columns = 7;
        rules.rows = 3;
        rules.layers = 2;
        rules.stepCost = [](const GridNode &, const GridNode &to) {
            return to.layer == 0 && to.column == 3 ? -1 : 0;
        };
        QVERIFY(gridRoute(rules, {node(0, 1)}, {node(6, 1)}).isEmpty());
        rules.layerCost = [](const GridNode &at, int) {
            return at.column == 1 || at.column == 5 ? 50 : -1;
        };
        const QList<GridNode> path = gridRoute(rules, {node(0, 1)}, {node(6, 1)});
        QVERIFY(isConnected(path, false));
        QCOMPARE(gridCorners(path),
                 (QList<GridNode>{node(0, 1), node(1, 1), node(1, 1, 1), node(5, 1, 1),
                                  node(5, 1), node(6, 1)}));
    }

    // The goal is a node of a layer: reaching its column and row on another
    // layer is not enough.
    void goalOnItsLayer()
    {
        GridRouteRules rules;
        rules.columns = 5;
        rules.rows = 1;
        rules.layers = 2;
        rules.layerCost = [](const GridNode &, int) { return 7; };
        const QList<GridNode> path = gridRoute(rules, {node(0, 0, 0)}, {node(4, 0, 1)});
        QCOMPARE(path.last(), node(4, 0, 1));
        QVERIFY(isConnected(path, false));
        QCOMPARE(path.size(), 6);
    }

    // A search that has to look further than 'maxExpanded' gives up.
    void givesUp()
    {
        GridRouteRules rules = walled(50, 50, {});
        rules.maxExpanded = 10;
        QVERIFY(gridRoute(rules, {node(0, 0)}, {node(49, 49)}).isEmpty());
        rules.maxExpanded = 1000000;
        QVERIFY(!gridRoute(rules, {node(0, 0)}, {node(49, 49)}).isEmpty());
    }

    // The same call gives the same way, and so does the call with the goals
    // in another order.
    void sameWayEachTime()
    {
        GridRouteRules rules = walled(12, 12, {node(5, 5), node(6, 6)});
        rules.diagonal = true;
        const QList<GridNode> a = gridRoute(rules, {node(0, 0)}, {node(11, 11), node(11, 10)});
        const QList<GridNode> b = gridRoute(rules, {node(0, 0)}, {node(11, 11), node(11, 10)});
        const QList<GridNode> c = gridRoute(rules, {node(0, 0)}, {node(11, 10), node(11, 11)});
        QCOMPARE(a, b);
        QCOMPARE(a, c);
    }

    void corners()
    {
        QCOMPARE(gridCorners({}), QList<GridNode>{});
        QCOMPARE(gridCorners({node(1, 1)}), QList<GridNode>{node(1, 1)});
        QCOMPARE(gridCorners({node(0, 0), node(1, 0), node(2, 0), node(2, 1), node(2, 2),
                              node(2, 2, 1), node(3, 2, 1)}),
                 (QList<GridNode>{node(0, 0), node(2, 0), node(2, 2), node(2, 2, 1),
                                  node(3, 2, 1)}));
    }
};

QTEST_GUILESS_MAIN(TestGridRoute)
#include "test_gridroute.moc"
