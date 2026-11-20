#pragma once

#include "board.h"

#include <QPointF>

#include <optional>

namespace ppcb {

// The board outline: its corners lie on the grid, its sides run any way.
//
// Side 'i' runs from corner 'i' to the next one; the last side closes the
// outline at corner 0. 'grid' is the grid step, greater than zero.

// A rectangle 'width' by 'height' with its top left corner at 'topLeft'.
QList<OutlineCorner> rectangleOutline(const Point &topLeft, Coord width, Coord height);

enum class OutlineProblem {
    None,
    // Fewer than three corners.
    TooFewCorners,
    // Two corners next to each other coincide.
    ZeroSide,
    // Sides cross or touch, or a side turns back over the one before.
    Crossing,
};
OutlineProblem outlineProblem(const QList<OutlineCorner> &outline);

// The corner 'corner' moved to 'to'; nothing when the outline would go wrong.
std::optional<QList<OutlineCorner>> withOutlineCornerMoved(const QList<OutlineCorner> &outline,
                                                           int corner, const Point &to);

// The side 'side' moved by 'delta', both of its corners alike, the sides
// next to it stretching after them; a level side moves only up or down, an
// upright one only sideways. Nothing when the outline would go wrong.
std::optional<QList<OutlineCorner>> withOutlineSideMoved(const QList<OutlineCorner> &outline,
                                                         int side, const Point &delta);

enum class OutlineCutProblem {
    None,
    // The rectangle has no width or height, or takes nothing off the board.
    Misses,
    // The rectangle lies inside the board without reaching its edge.
    Inside,
    // What is left would be in several pieces, or nothing would be.
    Splits,
};

struct OutlineCut
{
    // The outline with the cut taken off; the outline as it was when the
    // cut cannot be made.
    QList<OutlineCorner> outline;
    OutlineCutProblem problem = OutlineCutProblem::None;
};

// The rectangle between the grid points 'a' and 'b' cut off the board.
// Corners that stay keep their radii; the new ones are sharp, and where the
// rectangle crosses a slanted side they go to the grid.
OutlineCut withOutlineCut(const QList<OutlineCorner> &outline, const Point &a, const Point &b,
                          Coord grid);

// The corner nearest to 'at' within 'reach', or -1; all in millimetres.
int outlineCornerAt(const QList<OutlineCorner> &outline, const QPointF &at, double reach);
// The same for a side.
int outlineSideAt(const QList<OutlineCorner> &outline, const QPointF &at, double reach);

} // namespace ppcb
