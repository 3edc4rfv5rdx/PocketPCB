#pragma once

#include "document.h"
#include "paint.h"

#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QSet>

namespace ppcb {

class CanvasView;

// Editing of the sheet shown on the canvas: the selection, choosing with
// the mouse (a click, Ctrl and a click, a rectangle), dragging the
// selection along the grid, and turning, mirroring and deleting it.
// Positions come in scene millimetres.
class SheetEditor : public QObject
{
    Q_OBJECT

public:
    SheetEditor(Document *document, CanvasView *canvas, QObject *parent = nullptr);

    // How a sheet is called on its tab and in a step's name: its title, or
    // "Sheet <number>" when it has none.
    static QString sheetName(const Sheet &sheet, int index);

    // What is done to a selection: the name of the menu item and of the step.
    enum class Action { Move, Rotate, Mirror, Delete, Place, Copy, Cut, Paste, Duplicate };
    static QString actionName(Action action);

    // The sheet being edited, or -1 when the page shown is not a sheet.
    int sheet() const { return m_sheet; }
    // Drops the selection and any drag in progress.
    void setSheet(int sheet);

    const QSet<ObjectId> &selection() const { return m_selection; }
    // Keeps only the ids of objects on the sheet.
    void setSelection(const QSet<ObjectId> &ids);

    // What the canvas shows instead of the sheet while dragging or placing;
    // null when neither is going on.
    const Sheet *preview() const { return m_dragging || m_placing ? &m_preview : nullptr; }
    // The library to draw the preview with: the project's, with the parts
    // being placed; null when nothing is placed.
    const Library *previewLibrary() const { return m_placing ? &m_placeLibrary : nullptr; }

    // Placing: the part 'part' of 'source' with 'footprint' follows the
    // cursor; every click places one more, numbered on, until cancel().
    // R and X turn and mirror the one that follows. Does nothing when no
    // sheet is edited or 'source' lacks the part.
    void startPlacing(const Library &source, const QString &part, const QString &footprint);
    void stopPlacing();
    // Placing, pasting or duplicating: something follows the cursor.
    bool isPlacing() const { return m_placing; }
    // Where the cursor is, on the grid: what is placed follows it.
    void hover(const Point &at);

    // The selection goes to the editor's clipboard, with the parts of its
    // components; it stays there across sheets and projects.
    void copySelection();
    // Copies the selection, then deletes it as one step.
    void cutSelection();
    // A copy of the clipboard, or of the selection, follows the cursor like
    // a part being placed, with new ids and components numbered on; a
    // click puts it down once and chooses it.
    void paste();
    void duplicateSelection();
    // The rectangle being drawn to choose with; empty when there is none.
    QRectF rubberBand() const { return m_rubberBanding ? m_rubberBand : QRectF(); }

    // The left mouse button. 'adding' is Ctrl held: the selection grows or
    // shrinks instead of being replaced.
    void press(const QPointF &at, bool adding);
    void move(const QPointF &at);
    void release(const QPointF &at);
    // A second click in a row: the press it also is, and a component under
    // the cursor is opened (see componentOpened). Not while placing.
    void doubleClick(const QPointF &at, bool adding);
    // Esc: placing ends, or a drag is called off, or the selection is dropped.
    void cancel();

    void selectAll();
    void rotateSelection();
    void mirrorSelection();
    void deleteSelection();

signals:
    // The selection, the preview or the rubber band changed: redraw.
    void changed();
    // A component was double-clicked: its part and its footprint.
    void componentOpened(const QString &part, const QString &footprint);

private:
    const Sheet *currentSheet() const;
    QList<SheetObjectShape> shapes() const;
    // The grid point the selection on 'sheet' is turned and mirrored about:
    // a single component's own point, else the middle of the selection.
    Point selectionCenter(const Sheet &sheet) const;
    // The selected objects of 'sheet', placed about the point they are
    // turned about, which comes to lie at the origin.
    Sheet selectionPiece(const Sheet &sheet) const;
    // Makes 'piece', placed about the origin, follow the cursor; the parts
    // of its components come from 'source'. 'again': every click places
    // one more, else the first ends placing.
    void startFloating(const Sheet &piece, const Library &source, Action action, bool again);
    // Turns or mirrors what is dragged, on the way, without a step of its own:
    // the drop makes one step of it all.
    void transformDragged(Sheet (*transform)(const Sheet &, const QSet<ObjectId> &, const Point &));
    // Builds the preview of what follows the cursor, numbered on.
    void updateFloating();
    void placeFloating();
    // Deletes the selection as one step named 'action'.
    void removeSelection(Action action);
    // Makes 'edited' the sheet as one step of the history named 'action'.
    void apply(const QString &action, const Sheet &edited);
    // "Move R1, C2 (Sheet 1)": the step's name in the Edit menu, for the
    // objects 'ids' of 'objects', on the sheet edited.
    QString stepName(const QString &action, const Sheet &objects,
                     const QSet<ObjectId> &ids) const;
    QString stepName(const QString &action) const;
    // A distance on screen, in millimetres at the current zoom.
    double pixelsToMm(double pixels) const;
    Point snapped(const QPointF &at) const;

    Document *m_document;
    CanvasView *m_canvas;
    int m_sheet = -1;
    QSet<ObjectId> m_selection;

    // The press that may start a drag or a rectangle.
    bool m_pressed = false;
    QPointF m_pressAt;
    bool m_pressAdding = false;
    // The press was on a selected object: moving the mouse drags.
    bool m_pressOnSelection = false;

    bool m_dragging = false;
    // The sheet the drag started from, with what was turned on the way.
    Sheet m_dragBase;
    Sheet m_preview;
    Point m_dragDelta;

    bool m_rubberBanding = false;
    QRectF m_rubberBand;

    // Placing: what follows the cursor, about the origin; where its parts
    // come from; where the cursor is; and what is done.
    bool m_placing = false;
    Sheet m_floating;
    Library m_floatSource;
    Point m_floatAt;
    Action m_floatAction = Action::Place;
    bool m_floatAgain = false;
    // The project's library with the parts placed, and what a click puts
    // down: the floating objects where the cursor is, numbered on.
    Library m_placeLibrary;
    Sheet m_placed;

    // The clipboard: objects about the origin, and the library their parts are in.
    Sheet m_clipboard;
    Library m_clipboardLibrary;
};

} // namespace ppcb
