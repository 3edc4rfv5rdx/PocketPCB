#include "gridroute.h"

#include <QSet>
#include <QtGlobal>

#include <algorithm>
#include <cstdlib>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace ppcb {

namespace {

// The eight directions, clockwise from the right, with y growing downwards;
// the level and upright ones are the even ones.
constexpr int DIRECTIONS = 8;
constexpr int STEP_COLUMN[DIRECTIONS] = {1, 1, 0, -1, -1, -1, 0, 1};
constexpr int STEP_ROW[DIRECTIONS] = {0, 1, 1, 1, 0, -1, -1, -1};

// No direction yet: at a start, or just after a change of layer.
constexpr int NO_DIRECTION = -1;

// A place of the search: a node and the direction of the step into it.
struct State
{
    GridNode node;
    int direction = NO_DIRECTION;
};

class Search
{
public:
    explicit Search(const GridRouteRules &rules) : rules_(rules) {}

    bool contains(const GridNode &node) const
    {
        return node.column >= 0 && node.column < rules_.columns && node.row >= 0
               && node.row < rules_.rows && node.layer >= 0 && node.layer < rules_.layers;
    }

    qint64 nodeKey(const GridNode &node) const
    {
        return (qint64(node.layer) * rules_.rows + node.row) * rules_.columns + node.column;
    }

    qint64 stateKey(const State &state) const
    {
        return nodeKey(state.node) * (DIRECTIONS + 1) + (state.direction + 1);
    }

    State stateOf(qint64 key) const
    {
        State state;
        state.direction = int(key % (DIRECTIONS + 1)) - 1;
        qint64 node = key / (DIRECTIONS + 1);
        state.node.column = int(node % rules_.columns);
        node /= rules_.columns;
        state.node.row = int(node % rules_.rows);
        state.node.layer = int(node / rules_.rows);
        return state;
    }

    // What turning from 'from' to 'to' costs; negative when it is too sharp.
    qint64 bendCost(int from, int to) const
    {
        if (from == NO_DIRECTION || from == to)
            return 0;
        const int turn = std::abs(from - to);
        switch (qMin(turn, DIRECTIONS - turn)) {
        case 1:
            return rules_.bend45Cost;
        case 2:
            return rules_.bend90Cost;
        default:
            return -1;
        }
    }

    // The least a way from 'node' to a goal can cost: its length were
    // nothing in the way, so that the search finds the cheapest way.
    qint64 estimate(const GridNode &node) const
    {
        qint64 best = -1;
        for (const GridNode &goal : goals_) {
            const qint64 dx = std::abs(goal.column - node.column);
            const qint64 dy = std::abs(goal.row - node.row);
            const qint64 cost = rules_.diagonal
                                    ? qMin(dx, dy) * rules_.diagonalCost
                                          + (qMax(dx, dy) - qMin(dx, dy)) * rules_.straightCost
                                    : (dx + dy) * rules_.straightCost;
            if (best < 0 || cost < best)
                best = cost;
        }
        return best;
    }

    QList<GridNode> run(const QList<GridNode> &starts, const QList<GridNode> &goals)
    {
        if (rules_.columns <= 0 || rules_.rows <= 0 || rules_.layers <= 0)
            return {};
        for (const GridNode &goal : goals) {
            if (contains(goal) && !goalKeys_.contains(nodeKey(goal))) {
                goalKeys_.insert(nodeKey(goal));
                goals_.append(goal);
            }
        }
        if (goals_.isEmpty())
            return {};
        for (const GridNode &start : starts) {
            if (!contains(start))
                continue;
            const qint64 cost = rules_.startCost ? rules_.startCost(start) : 0;
            if (cost >= 0)
                reach(State{start, NO_DIRECTION}, cost, -1);
        }
        int expanded = 0;
        while (!open_.empty()) {
            const auto [estimated, cost, order, key] = open_.top();
            Q_UNUSED(estimated);
            Q_UNUSED(order);
            open_.pop();
            if (cost > best_.at(key).cost)
                continue; // reached cheaper since
            const State state = stateOf(key);
            if (goalKeys_.contains(nodeKey(state.node)))
                return pathTo(key);
            if (++expanded > rules_.maxExpanded)
                return {};
            expand(state, cost, key);
        }
        return {};
    }

private:
    struct Reached
    {
        qint64 cost = 0;
        qint64 from = -1;
    };

    // Estimated total, cost so far, order of reaching, state: the lowest
    // first, and of equals the one reached first, so that the result does
    // not depend on the queue.
    using Entry = std::tuple<qint64, qint64, qint64, qint64>;

    void reach(const State &state, qint64 cost, qint64 from)
    {
        const qint64 key = stateKey(state);
        const auto found = best_.find(key);
        if (found != best_.end() && found->second.cost <= cost)
            return;
        best_[key] = Reached{cost, from};
        open_.push(Entry{cost + estimate(state.node), cost, order_++, key});
    }

    void expand(const State &state, qint64 cost, qint64 key)
    {
        const int step = rules_.diagonal ? 1 : 2;
        for (int direction = 0; direction < DIRECTIONS; direction += step) {
            const GridNode next{state.node.column + STEP_COLUMN[direction],
                                state.node.row + STEP_ROW[direction], state.node.layer};
            if (!contains(next))
                continue;
            const qint64 bend = bendCost(state.direction, direction);
            if (bend < 0)
                continue;
            qint64 extra = 0;
            if (rules_.stepCost) {
                extra = rules_.stepCost(state.node, next);
                if (extra < 0)
                    continue;
            }
            const qint64 length = direction % 2 == 0 ? rules_.straightCost : rules_.diagonalCost;
            reach(State{next, direction}, cost + length + bend + extra, key);
        }
        if (rules_.jumps) {
            for (const GridJump &jump : rules_.jumps(state.node)) {
                if (contains(jump.to) && jump.cost >= 0)
                    reach(State{jump.to, NO_DIRECTION}, cost + jump.cost, key);
            }
        }
        if (!rules_.layerCost)
            return;
        for (int layer = 0; layer < rules_.layers; ++layer) {
            if (layer == state.node.layer)
                continue;
            const qint64 change = rules_.layerCost(state.node, layer);
            if (change < 0)
                continue;
            const GridNode next{state.node.column, state.node.row, layer};
            reach(State{next, NO_DIRECTION}, cost + change, key);
        }
    }

    QList<GridNode> pathTo(qint64 key) const
    {
        QList<GridNode> path;
        for (qint64 at = key; at >= 0; at = best_.at(at).from)
            path.append(stateOf(at).node);
        std::reverse(path.begin(), path.end());
        return path;
    }

    const GridRouteRules &rules_;
    QList<GridNode> goals_;
    QSet<qint64> goalKeys_;
    std::unordered_map<qint64, Reached> best_;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open_;
    qint64 order_ = 0;
};

} // namespace

QList<GridNode> gridRoute(const GridRouteRules &rules, const QList<GridNode> &starts,
                          const QList<GridNode> &goals)
{
    return Search(rules).run(starts, goals);
}

QList<GridNode> gridCorners(const QList<GridNode> &path)
{
    const auto stepOf = [](const GridNode &a, const GridNode &b) {
        return std::make_tuple(b.column - a.column, b.row - a.row, b.layer - a.layer);
    };
    QList<GridNode> corners;
    for (int i = 0; i < path.size(); ++i) {
        if (i == 0 || i == path.size() - 1
            || stepOf(path.at(i - 1), path.at(i)) != stepOf(path.at(i), path.at(i + 1)))
            corners.append(path.at(i));
    }
    return corners;
}

} // namespace ppcb
