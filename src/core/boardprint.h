#pragma once

#include "board.h"

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace ppcb {

// A layer of one side of the board printed 1:1, for a board made at home:
// its copper, for toner transfer or photoresist, or its silkscreen, also
// carried over by toner transfer.

enum class BoardPrintLayer { Copper, Silkscreen };

// What is printed and how.
struct BoardPrint
{
    BoardPrintLayer layer = BoardPrintLayer::Copper;
    BoardSide side = BoardSide::Top;
    // Left for right, as the top side is laid face down onto the board.
    bool mirrored = true;
    // Copper white on black, for photoresist; the copper only.
    bool inverted = false;
    // The small hole left in the middle of each pad with a hole, so that
    // the drill does not wander; 0 for none. The copper only.
    Coord centreHole = 500000;
    // Crosses outside the outline that put the prints of the two sides
    // over each other: copper and copper, or copper and silkscreen.
    bool registrationMarks = true;
    int copies = 1;
};

// The mirrored setting a side starts with, for either layer: the top side
// is mirrored, the bottom is not.
bool printMirroredByDefault(BoardSide side);

// True when 'board' has anything on the silkscreen of 'side': a part on
// that side, the author's drawing there, or on the top a wire link.
bool silkscreenUsed(const Board &board, BoardSide side);

// The crosses of the registration marks around 'area', the rectangle of
// the board, each given by its centre: one outside each corner.
QList<QPointF> registrationMarkCentres(const QRectF &area);
// Half the length of a cross's arm and the width of its lines, in mm.
constexpr double REGISTRATION_MARK_ARM = 2.5;
constexpr double REGISTRATION_MARK_LINE = 0.2;

// The rectangle one copy takes, in the board's millimetres: 'boardArea'
// (boardBounds()), with room for the registration marks when 'marks'.
QRectF boardPrintCopyRect(const QRectF &boardArea, bool marks);

// The copies laid out on pages of paper 'page' (millimetres): as many in a
// row and in a column as fit inside the margins, under the band at the top
// 'header' mm high (boardPrintHeader()), the rest on the next pages.
struct BoardPrintLayout
{
    // Each copy a quarter turned, when more of them fit on a page so.
    bool turned = false;
    // False when not even one copy fits on the paper: each is printed on a
    // page of its own all the same, its corner at the margins.
    bool fits = true;
    // For each page, the top left corner of each copy on it, in mm from
    // the paper's corner: where the copy, turned when 'turned', starts.
    QList<QList<QPointF>> pages;
};
BoardPrintLayout boardPrintLayout(const QSizeF &copy, const QSizeF &page, int copies,
                                  double header);

// The paper's margins, the gap between copies, the band at the top of each
// page with the caption and the scale ruler, and each line of the drill
// table under it, in mm.
constexpr double BOARD_PRINT_MARGIN = 10;
constexpr double BOARD_PRINT_GAP = 5;
constexpr double BOARD_PRINT_HEADER = 14;
constexpr double BOARD_PRINT_DRILL_LINE = 4;
// The band at the top of each page with 'drillLines' lines of the drill
// table.
double boardPrintHeader(int drillLines);
// The scale ruler: 100 mm, with a tick every 10 mm, so that the print can
// be checked against a real ruler.
constexpr double SCALE_RULER_LENGTH = 100;
constexpr double SCALE_RULER_TICK = 10;

} // namespace ppcb
