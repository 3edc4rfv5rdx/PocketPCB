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
    enum class Action { Move, Rotate, Mirror, Delete, Place };
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
    // The library to draw the preview with: the project's, with the part
    // being placed; null when nothing is placed.
    const Library *previewLibrary() const { return m_placing ? &m_placeLibrary : nullptr; }

    // Placing: the part 'part' of 'source' with 'footprint' follows the
    // cursor; every click places one more, numbered on, until cancel().
    // R and X turn and mirror the one that follows. Does nothing when no
    // sheet is edited or 'source' lacks the part.
    void startPlacing(const Library &source, const QString &part, const QString &footprint);
    void stopPlacing();
    bool isPlacing() const { return m_placing; }
    // Where the cursor is, on the grid: the part being placed follows it.
    void hover(const Point &at);
    // The rectangle being drawn to choose with; empty when there is none.
    QRectF rubberBand() const { return m_rubberBanding ? m_rubberBand : QRectF(); }

    // The left mouse button. 'adding' is Ctrl held: the selection grows or
    // shrinks instead of being replaced.
    void press(const QPointF &at, bool adding);
    void move(const QPointF &at);
    void release(const QPointF &at);
    // Esc: placing ends, or a drag is called off, or the selection is dropped.
    void cancel();

    void selectAll();
    void rotateSelection();
    void mirrorSelection();
    void deleteSelection();

signals:
    // The selection, the preview or the rubber band changed: redraw.
    void changed();

private:
    const Sheet *currentSheet() const;
    QList<SheetObjectShape> shapes() const;
    // The grid point the selection on 'sheet' is turned and mirrored about:
    // a single component's own point, else the middle of the selection.
    Point selectionCenter(const Sheet &sheet) const;
    // Turns or mirrors what is dragged, on the way, without a step of its own:
    // the drop makes one step of it all.
    void transformDragged(Sheet (*transform)(const Sheet &, const QSet<ObjectId> &, const Point &));
    // Builds the preview of the part following the cursor, numbered on.
    void updateGhost();
    void placeGhost();
    // Makes 'edited' the sheet as one step of the history named 'action'.
    void apply(const QString &action, const Sheet &edited);
    // "Move R1, C2 (Sheet 1)": the step's name in the Edit menu.
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

    // Placing: the part, where it comes from, and the next one to place.
    bool m_placing = false;
    Library m_placeSource;
    QString m_placePart;
    Component m_ghost;
    Library m_placeLibrary;
};

} // namespace ppcb
