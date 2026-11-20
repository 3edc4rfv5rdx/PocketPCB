#pragma once

#include "board.h"
#include "document.h"
#include "inplaceedit.h"

#include <QObject>
#include <QPolygonF>
#include <QRectF>

#include <optional>

namespace ppcb {

class CanvasView;

// Editing of the board shown on the canvas, so far its outline (see
// outline.h). Choosing: a corner of the outline is dragged along the grid,
// and so is a side, the sides beside it stretching after it; a
// double click on a corner types its radius in place. Drawing the outline:
// every click sets a corner on the grid; a click on the first corner closes
// it. Cutting out: a
// rectangle drawn across the edge is taken off the board. Each change is
// one step. Positions come in scene millimetres.
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
    // corner at the origin, as one step.
    void makeRectangleBoard(Coord width, Coord height);
    // The outline's size; empty for a board without one.
    QRectF outlineBounds() const;
    // The outline may not be changed or moved (Board::outlineLocked).
    bool isOutlineLocked() const { return m_document->design().board.outlineLocked; }

    // The outline is chosen whole: by a double click on a side or by
    // chooseAll(); a drag on any of its sides or corners then moves it all.
    // Esc or a click off it drops it.
    bool hasChosen() const { return m_active && m_outlineChosen && !outline().isEmpty(); }
    void chooseAll();
    // What is chosen is locked (the lock button is pressed); locking or
    // unlocking it is one step.
    bool chosenLocked() const { return hasChosen() && isOutlineLocked(); }
    void setChosenLocked(bool locked);

    // The left mouse button. A second click in a row is a press as well;
    // in choosing, a corner under it gets its radius typed and a side
    // under it chooses the outline whole.
    void press(const QPointF &at);
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

    // What the canvas shows: the outline as a drag would leave it, or null
    // for the board's own.
    const QList<OutlineCorner> *previewOutline() const
    {
        return m_preview ? &*m_preview : nullptr;
    }
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
    bool outlineChosen() const { return hasChosen(); }
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

    int m_litCorner = -1;
    int m_litSide = -1;

    InPlaceEdit m_radiusEdit;
    int m_radiusCorner = -1;
};

} // namespace ppcb
