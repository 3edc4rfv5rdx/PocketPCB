#pragma once

#include "document.h"
#include "paint.h"

#include <QLineF>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QSet>

class QLineEdit;

namespace ppcb {

class CanvasView;

// Editing of the sheet shown on the canvas: the selection, choosing with
// the mouse (a click, Ctrl and a click, a rectangle; a click on one of the
// objects chosen keeps it alone), dragging the
// selection along the grid with the wires on it following (Shift tears them
// off), turning and mirroring the selection with the wires on it following
// too, and deleting it. A click on a
// wire chooses the segment under it: a drag moves it, or one end of the
// wire when taken near it, unless that end is on a pin; Delete removes it. A double click chooses the
// whole wire, Shift and a click every wire joined to it. A component's
// designator or value is chosen alone by a click and dragged alone, its
// component staying; a double click on the value edits it in place.
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
    enum class Action {
        Move,
        Rotate,
        Mirror,
        Delete,
        Place,
        Copy,
        Cut,
        Paste,
        Duplicate,
        Wire,
        Value,
        Designator,
        Footprint,
        OffBoard,
        OnBoard
    };
    static QString actionName(Action action);

    // The sheet being edited, or -1 when the page shown is not a sheet.
    int sheet() const { return m_sheet; }
    // Drops the selection and any drag in progress.
    void setSheet(int sheet);

    const QSet<ObjectId> &selection() const { return m_selection; }
    // The segment of a wire chosen alone, in millimetres; a null line when
    // none is, or while it is dragged.
    QLineF chosenSegment() const;
    // While a component's text is dragged: from the component's point to
    // the text's; a null line otherwise.
    QLineF textGuide() const;
    // The rectangle of the text chosen alone, in millimetres; empty when
    // none is, or while it is dragged.
    QRectF chosenText() const;
    // Keeps only the ids of objects on the sheet.
    void setSelection(const QSet<ObjectId> &ids);

    // What the canvas shows instead of the sheet while dragging or placing;
    // null when neither is going on.
    const Sheet *preview() const
    {
        return m_dragging || m_placing || isDrawingWire() ? &m_preview : nullptr;
    }
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

    // Drawing wires, until cancel() with no wire begun: a click begins a
    // wire and every next one sets a corner; the wire follows the cursor
    // with one corner. A click on a pin or a wire, or a second click on the
    // same point, ends it; cancel() drops the wire begun.
    void startWiring();
    bool isWiring() const { return m_wiring; }
    // The corner of the wire that follows the cursor: the other way round,
    // and slanted at 45 degrees or square.
    void swapWireCorner();
    void toggleWireSlant();
    // The rectangle being drawn to choose with; empty when there is none.
    QRectF rubberBand() const { return m_rubberBanding ? m_rubberBand : QRectF(); }

    // The left mouse button. 'adding' is Ctrl held: the selection grows or
    // shrinks instead of being replaced. 'net' is Shift held: a wire taken
    // chooses every wire joined to it. 'detached' is Shift held while
    // dragging: what is dragged leaves the wires on it where they are.
    void press(const QPointF &at, bool adding, bool net = false);
    void move(const QPointF &at, bool detached);
    void release(const QPointF &at, bool detached);
    // A second click in a row: the press it also is, and a component under
    // the cursor is opened (see componentOpened), or its value edited in
    // place when that is under it. Not while placing.
    void doubleClick(const QPointF &at, bool adding);
    // Esc: placing ends, or a drag is called off, or the selection is dropped.
    void cancel();

    // The chosen components of the sheet edited, in the order of the sheet;
    // the component of the text chosen alone.
    QList<Component> chosenComponents() const;
    // Properties of the components 'ids' of the sheet edited, each change one
    // step; nothing happens when it changes nothing (see sheetedit.h).
    void setComponentValue(const QSet<ObjectId> &ids, const QString &value);
    void setComponentDesignator(const ObjectId &id, const QString &designator);
    void setComponentFootprint(const QSet<ObjectId> &ids, const QString &footprint);
    void setComponentOffBoard(const QSet<ObjectId> &ids, bool offBoard);

    void selectAll();
    void rotateSelection();
    void mirrorSelection();
    void deleteSelection();

signals:
    // The selection, the preview or the rubber band changed: redraw.
    void changed();
    // A component was double-clicked: its part and its footprint.
    void componentOpened(const QString &part, const QString &footprint);

protected:
    // Keys and focus of the value being edited in place.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Opens a line on the canvas over the value of the component 'id': Enter
    // or leaving it sets the value as one step, Esc drops it.
    void editValue(const ObjectId &id);
    // Closes the line; 'keep' sets what it holds as the value.
    void finishValueEdit(bool keep);
    const Sheet *currentSheet() const;
    // True when a segment of a wire is chosen alone: no whole object is,
    // and the sheet has the segment.
    bool hasSegment() const;
    void dropSegment() { m_segmentWire = ObjectId(); }
    // True when a text of a component is chosen alone: no whole object is,
    // and the sheet has the component.
    bool hasText() const;
    // Drops the segment and the text chosen alone.
    void dropPieces()
    {
        dropSegment();
        m_textComponent = ObjectId();
    }
    // What a step is named by: the chosen objects, or the segment's wire.
    QSet<ObjectId> chosenIds() const;
    QList<SheetObjectShape> shapes() const;
    // The drag takes the selection whole, not a wire's end, a segment or a
    // component's text alone: only then does it turn and mirror on the way.
    bool dragsWhole() const;
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
    // Builds the preview of the drag: the base moved, with the wires on it
    // following unless torn off.
    void updateDrag();
    // Builds the preview of what follows the cursor, numbered on.
    void updateFloating();
    void placeFloating();
    bool isDrawingWire() const { return m_wiring && !m_wirePoints.isEmpty(); }
    void wireClick(const Point &at);
    // Puts the wire begun on the sheet as one step, when it has a length.
    void finishWire();
    // Builds the preview of the wire begun, up to the cursor.
    void updateWire();
    // The document changed under what follows the cursor: build it again.
    void refreshPreview();
    // Deletes the selection as one step named 'action'.
    void removeSelection(Action action);
    // Makes 'edited' the sheet as one step of the history named 'action'.
    void apply(const QString &action, const Sheet &edited);
    // The same for a change of the components 'ids', named after them.
    void applyToComponents(Action action, const QSet<ObjectId> &ids, const Sheet &edited);
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
    // The segment chosen by a click on a wire: its wire and its index. It
    // counts only while 'm_selection' is empty (hasSegment()).
    ObjectId m_segmentWire;
    int m_segmentIndex = -1;

    // The press that may start a drag or a rectangle.
    bool m_pressed = false;
    QPointF m_pressAt;
    bool m_pressAdding = false;
    // The press was on a selected object: moving the mouse drags.
    bool m_pressOnSelection = false;
    // The press took an object that was chosen already: a release that did
    // not drag keeps it chosen alone. Set by every press.
    ObjectId m_narrowTo;
    // The point of what the press took that a drag lands on the grid.
    Point m_dragAnchor;

    bool m_dragging = false;
    // The press took a wire near one of its ends that is on no pin, which a
    // drag moves alone:
    // the wire, and its last end or its first. Set by every press.
    ObjectId m_endWire;
    bool m_endIsLast = false;
    // The text chosen alone by a click on it, which a drag moves: its
    // component and which text. It counts only while 'm_selection' is empty
    // (hasText()).
    ObjectId m_textComponent;
    ComponentText m_text = ComponentText::Designator;
    // The sheet the drag started from, and the same with what was turned
    // on the way.
    Sheet m_dragStart;
    Sheet m_dragBase;
    bool m_dragDetached = false;
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

    // Drawing wires: the points set so far (none before the first click),
    // where the cursor is, and the shape of the corner that follows it.
    bool m_wiring = false;
    QList<Point> m_wirePoints;
    Point m_wireCursor;
    bool m_wireSwapped = false;
    bool m_wireSlanted = false;

    // The value being edited in place: the line, and its component.
    QPointer<QLineEdit> m_valueEdit;
    ObjectId m_valueComponent;

    // The clipboard: objects about the origin, and the library their parts are in.
    Sheet m_clipboard;
    Library m_clipboardLibrary;
};

} // namespace ppcb
