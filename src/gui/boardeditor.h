#pragma once

#include "board.h"
#include "boardedit.h"
#include "document.h"
#include "inplaceedit.h"

#include <QObject>
#include <QPolygonF>
#include <QRectF>

#include <optional>

namespace ppcb {

class CanvasView;

// Editing of the board shown on the canvas: its outline (see outline.h)
// and its placements (see boardedit.h). Choosing: a corner of the outline is
// dragged along the grid, and so is a side, the sides beside it stretching
// after it; a double click on a corner types its radius in place. A click
// on a placement chooses it, with Ctrl adds it to what is chosen or takes
// it away, and a drag moves what is chosen along the grid; a frame drawn
// on the empty board chooses the placements wholly inside it. A click on a
// placement's designator or value chooses that text alone, and a drag
// moves it away from the placement. Drawing the outline: every click sets
// a corner on the grid; a click on the first corner closes it. Cutting
// out: a rectangle drawn across the edge is taken off the board. Each
// change is one step. Positions come in scene millimetres.
class BoardEditor : public QObject
{
    Q_OBJECT

public:
    BoardEditor(Document *document, CanvasView *canvas, QObject *parent = nullptr);

    // What is done to the board: the name of the menu item and of the step.
    enum class Action {
        DrawOutline,
        CutOut,
        RectangleBoard,
        MoveCorner,
        MoveSide,
        MoveOutline,
        RoundCorner,
        LockOutline,
        UnlockOutline,
        MoveParts,
        RotateParts,
        MovePartText,
        LockParts,
        UnlockParts,
    };
    static QString actionName(Action action);

    // While the board is shown. Leaving it drops what is being drawn or
    // dragged and goes back to choosing.
    bool isActive() const { return m_active; }
    void setActive(bool active);

    // What the mouse does now.
    enum class Tool { Select, Outline, CutOut };
    Tool tool() const { return m_tool; }
    void chooseSelectTool();
    // Drawing a new outline, which takes the place of the old one once
    // closed.
    void startDrawingOutline();
    void startCuttingOut();
    // Drawing the outline or a cut-out is going on: a short right click ends
    // it, as Esc does.
    bool isDrawing() const { return !m_outlinePoints.isEmpty() || m_cutting; }
    // The outline becomes a rectangle 'width' by 'height' with its top left
    // corner at the origin, as one step; choosing comes back.
    void makeRectangleBoard(Coord width, Coord height);
    // The outline's size; empty for a board without one.
    QRectF outlineBounds() const;
    // The outline may not be changed or moved (Board::outlineLocked).
    bool isOutlineLocked() const { return m_document->design().board.outlineLocked; }

    // The outline is chosen whole: by a double click on a side or by
    // chooseAll(); a drag on any of its sides or corners then moves it all,
    // with the placements chosen. Esc or a click off it drops it.
    bool outlineChosen() const { return m_active && m_outlineChosen && !outline().isEmpty(); }
    // The placements chosen, of those the board has.
    QSet<ObjectId> chosenPlacements() const;
    // The text chosen alone; null when none is.
    PlacementText chosenText() const { return m_active ? m_chosenText : PlacementText(); }
    // Something is chosen that may be locked: the outline or placements.
    bool hasChosen() const { return outlineChosen() || !chosenPlacements().isEmpty(); }
    // The outline and every placement.
    void chooseAll();
    // The placements 'ids' and nothing else, as after an Undo of a step
    // that touched them.
    void setChosen(const QSet<ObjectId> &ids);
    // Everything chosen is locked (the lock button is pressed); locking or
    // unlocking it is one step.
    bool chosenLocked() const;
    void setChosenLocked(bool locked);
    // The placements chosen turned by 90 degrees clockwise as one step:
    // one about its own point, several about the middle of them on the grid.
    // Locked ones stay.
    void rotateChosen();

    // The left mouse button; with 'adding' (Ctrl) a placement clicked is
    // added to what is chosen or taken away, and a frame adds what it holds.
    // A second click in a row is a press as well; in choosing, a corner
    // under it gets its radius typed and a side under it chooses the
    // outline whole.
    void press(const QPointF &at, bool adding = false);
    void move(const QPointF &at);
    void release(const QPointF &at);
    void doubleClick(const QPointF &at);
    // Where the cursor is, on the grid: the side being drawn follows it.
    void hover(const Point &at);
    // The mouse over the board with no button held: the corner or the side
    // under it is lit.
    void hoverMouse(const QPointF &at);
    // Esc: a drag is called off, or the outline or the cut-out being drawn
    // is dropped, or the outline chosen whole; else drawing ends and
    // choosing comes back.
    void cancel();

    // What the canvas shows: the board as a drag would leave it.
    Board shownBoard() const;
    // The frame drawn to choose placements; empty when none is.
    QRectF chooserFrame() const { return m_framing ? m_frame : QRectF(); }
    // The outline being drawn up to the cursor; empty when none is.
    QPolygonF drawnOutline() const;
    // The cut-out being drawn; empty when none is.
    QRectF cutRect() const;
    // Where the next corner of the outline or of the cut-out goes, or where
    // a dragged corner is, so that its coordinates can be shown: the cursor
    // while drawing, from before the first click on; the dragged corner, or
    // the end of a dragged side nearer to where it was taken; nothing
    // otherwise.
    std::optional<Point> drawnPoint() const;
    // The corners are marked while choosing; one of them or a side is lit
    // when under the mouse or dragged: an index, or -1.
    bool showsCorners() const
    {
        return m_active && m_tool == Tool::Select && !isOutlineLocked();
    }
    int litCorner() const { return m_litCorner; }
    int litSide() const { return m_litSide; }

signals:
    // What the canvas shows of the editing changed: redraw.
    void changed();
    // Why something could not be done, for the status bar.
    void message(const QString &text);

private:
    const QList<OutlineCorner> &outline() const { return m_document->design().board.outline; }
    int boardPage() const;
    // Makes 'outline' the board's as one step named after 'action'.
    void apply(Action action, const QList<OutlineCorner> &outline);
    // A click while drawing the outline, at the grid point 'at'.
    void outlineClick(const Point &at);
    void cutOut(const Point &a, const Point &b);
    // Opens the line to type the radius of 'corner' in.
    void editRadius(int corner);
    void radiusTyped(const QString &text);
    // Says in the status bar that the outline is locked; true when it is.
    bool refuseLocked();
    // Makes 'board' the board as one step named after 'action', with the
    // placements 'ids' as those it touched.
    void applyBoard(Action action, const Board &board, const QSet<ObjectId> &ids);
    // A press in choosing on a placement's text or on a placement; false
    // when there is neither under 'at'.
    bool pressPlacement(const QPointF &at, bool adding);
    // The board as a drag of placements or of a text to 'at' leaves it.
    Board draggedBoard(const QPointF &at) const;
    // Drops what is chosen: the outline, the placements, the text.
    void dropChosen();
    // Chooses the outline whole, and nothing else.
    void chooseOutline();
    // Drops a drag and what is lit: the board changed under them.
    void dropDrag();
    void setLit(int corner, int side);

    Document *m_document;
    CanvasView *m_canvas;
    bool m_active = false;
    Tool m_tool = Tool::Select;

    // Drawing the outline: the corners set so far, and the cursor.
    QList<Point> m_outlinePoints;
    Point m_cursor;

    // Cutting out: the rectangle's corners while the button is held.
    bool m_cutting = false;
    Point m_cutFrom;
    Point m_cutTo;

    // Choosing: the press, what it took, and the outline as dragged so far.
    bool m_pressed = false;
    bool m_dragging = false;
    QPointF m_pressAt;
    Point m_pressGrid;
    int m_dragCorner = -1;
    int m_dragSide = -1;
    // The side was taken nearer to its end corner than to its start.
    bool m_dragSideEnd = false;
    // The press took the outline chosen whole: a drag moves it all.
    bool m_dragWhole = false;
    // A drag of the locked outline was refused once since the press.
    bool m_lockTold = false;
    bool m_outlineChosen = false;
    std::optional<QList<OutlineCorner>> m_preview;

    // The placements chosen, and a text chosen alone.
    QSet<ObjectId> m_chosen;
    PlacementText m_chosenText;
    // The press took a placement chosen, or a text: a drag moves them from
    // 'm_dragAnchor', the point of the one taken, which lands on the grid.
    bool m_dragPlacements = false;
    bool m_dragText = false;
    Point m_dragAnchor;
    std::optional<Board> m_movedBoard;
    // The press was on the empty board: a drag draws a frame choosing the
    // placements inside it, added to those chosen with 'm_frameAdding'.
    bool m_framing = false;
    bool m_frameAdding = false;
    QRectF m_frame;
    bool m_pressEmpty = false;

    int m_litCorner = -1;
    int m_litSide = -1;

    InPlaceEdit m_radiusEdit;
    int m_radiusCorner = -1;
};

} // namespace ppcb
