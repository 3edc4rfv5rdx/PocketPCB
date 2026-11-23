#pragma once

#include <QList>

#include <functional>

namespace ppcb {

// A search for the cheapest way across a grid of nodes, A*, that knows
// nothing of what lies on the grid: the caller says what each step costs.
// The schematic lays wires with it on one layer in four directions; the
// board will lay tracks with it on two layers in eight, with vias between.

// A node of the grid: its column and row (the row grows downwards, like y
// on a sheet) and its layer.
struct GridNode
{
    int column = 0;
    int row = 0;
    int layer = 0;
};

inline bool operator==(const GridNode &a, const GridNode &b)
{
    return a.column == b.column && a.row == b.row && a.layer == b.layer;
}
inline bool operator!=(const GridNode &a, const GridNode &b) { return !(a == b); }

// A step of more than one node on a layer, over what lies between, such as a
// wire link: the node it lands on and what it costs.
struct GridJump
{
    GridNode to;
    int cost = 0;
};

// What the grid is and what a way across it costs. A way costs the length
// of its steps, the bends between them, what stepCost adds to each step and
// what layerCost asks for each change of layer.
struct GridRouteRules
{
    int columns = 0;
    int rows = 0;
    int layers = 1;
    // Steps at 45 degrees too, not only level and upright ones.
    bool diagonal = false;
    // The length of a level or upright step and of a slanted one. The search
    // is sure to find the cheapest way only while a slanted step costs no
    // less than a level one and no more than two.
    int straightCost = 10;
    int diagonalCost = 14;
    // A turn by 45 or by 90 degrees between two steps; a sharper turn is never
    // taken. A change of layer starts afresh: the step after it is no turn.
    int bend45Cost = 0;
    int bend90Cost = 0;
    // What the step from 'from' to the next node 'to' on the same layer adds
    // to its length; a negative value forbids the step. Unset: nothing.
    // A slanted step past two corners of other nodes is the caller's to judge.
    std::function<int(const GridNode &from, const GridNode &to)> stepCost;
    // What it costs to go from 'at' to the node of the layer 'layer' at the
    // same column and row; a negative value forbids it. Unset: the layer is
    // never changed.
    std::function<int(const GridNode &at, int layer)> layerCost;
    // What a way that starts at 'start' costs before its first step, for a
    // start that goes on from something already there; a negative value
    // leaves the start out. Unset: nothing.
    std::function<int(const GridNode &start)> startCost;
    // The jumps a way may take from 'at' (GridJump); the node a jump lands
    // on is the caller's to judge. The step after a jump is no turn. The search is sure to find the cheapest way only while a
    // jump costs no less than the steps it leaps. Unset: none.
    std::function<QList<GridJump>(const GridNode &at)> jumps;
    // How many steps a way takes after a jump before it may jump again, so
    // that what a jump leaves, such as the pads of a wire link, keeps apart.
    int jumpSpacing = 0;
    // How many nodes the search looks on from before it gives up.
    int maxExpanded = 1000000;
};

// The nodes of the cheapest way from one of 'starts' to one of 'goals', both
// ends included: a start alone when it is a goal too. A step on the same
// layer goes to one of the four (eight with 'diagonal') nodes around; a
// change of layer stays at the column and row. Of ways that cost the same,
// the same one each time. Empty when no way is found within
// 'maxExpanded' nodes or there is no goal. Starts and goals off the grid
// are left out.
QList<GridNode> gridRoute(const GridRouteRules &rules, const QList<GridNode> &starts,
                          const QList<GridNode> &goals);

// The nodes of the way 'path' where it starts, turns, changes layer (both
// nodes of the change) and ends: what a wire or a track is drawn through.
QList<GridNode> gridCorners(const QList<GridNode> &path);

} // namespace ppcb
