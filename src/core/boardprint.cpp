#include "boardprint.h"

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

// The gap between the board and the arm of a registration mark nearest to
// it, in mm.
const double MARK_GAP = 2;

// The room around the board the marks take, their lines included.
double markRoom()
{
    return MARK_GAP + 2 * REGISTRATION_MARK_ARM + REGISTRATION_MARK_LINE / 2;
}

// How many copies of 'length' fit in 'room' with gaps between them.
int fitting(double length, double room)
{
    if (length <= 0 || room < length)
        return 0;
    return 1 + int(std::floor((room - length) / (length + BOARD_PRINT_GAP)));
}

} // namespace

bool printMirroredByDefault(BoardSide side)
{
    return side == BoardSide::Top;
}

bool silkscreenUsed(const Board &board, BoardSide side)
{
    const auto onSide = [side](const auto &item) { return item.side == side; };
    return std::any_of(board.placements.cbegin(), board.placements.cend(), onSide)
           || std::any_of(board.graphics.cbegin(), board.graphics.cend(), onSide)
           || (side == BoardSide::Top && !board.links.isEmpty());
}

QList<QPointF> registrationMarkCentres(const QRectF &area)
{
    const double away = MARK_GAP + REGISTRATION_MARK_ARM;
    return {area.topLeft() + QPointF(-away, -away), area.topRight() + QPointF(away, -away),
            area.bottomRight() + QPointF(away, away), area.bottomLeft() + QPointF(-away, away)};
}

QRectF boardPrintCopyRect(const QRectF &boardArea, bool marks)
{
    if (!marks)
        return boardArea;
    const double room = markRoom();
    return boardArea.adjusted(-room, -room, room, room);
}

double boardPrintHeader(int drillLines)
{
    return BOARD_PRINT_HEADER + std::max(drillLines, 0) * BOARD_PRINT_DRILL_LINE;
}

BoardPrintLayout boardPrintLayout(const QSizeF &copy, const QSizeF &page, int copies,
                                  double header)
{
    BoardPrintLayout layout;
    copies = std::max(copies, 1);
    const double width = page.width() - 2 * BOARD_PRINT_MARGIN;
    const double height = page.height() - 2 * BOARD_PRINT_MARGIN - header;
    const int columns = fitting(copy.width(), width);
    const int rows = fitting(copy.height(), height);
    const int turnedColumns = fitting(copy.height(), width);
    const int turnedRows = fitting(copy.width(), height);
    layout.turned = turnedColumns * turnedRows > columns * rows;
    const QSizeF placed = layout.turned ? copy.transposed() : copy;
    const int across = std::max(layout.turned ? turnedColumns : columns, 1);
    const int down = std::max(layout.turned ? turnedRows : rows, 1);
    layout.fits = (layout.turned ? turnedColumns * turnedRows : columns * rows) > 0;
    const int perPage = layout.fits ? across * down : 1;

    const QPointF start(BOARD_PRINT_MARGIN, BOARD_PRINT_MARGIN + header);
    for (int index = 0; index < copies; ++index) {
        const int onPage = index % perPage;
        if (onPage == 0)
            layout.pages.append(QList<QPointF>());
        const int column = onPage % across;
        const int row = onPage / across;
        layout.pages.last().append(start
                                   + QPointF(column * (placed.width() + BOARD_PRINT_GAP),
                                             row * (placed.height() + BOARD_PRINT_GAP)));
    }
    return layout;
}

} // namespace ppcb
