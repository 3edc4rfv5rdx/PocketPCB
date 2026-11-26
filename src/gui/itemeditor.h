#pragma once

#include "alerts.h"
#include "graphic.h"
#include "library.h"
#include "libraryfile.h"
#include "toolicons.h"

#include <QHash>
#include <QLineF>
#include <QSet>
#include <QUndoStack>
#include <QWidget>

#include <functional>
#include <optional>

class QAction;
class QComboBox;
class QKeySequence;
class QMenu;
class QPainter;
class QStackedWidget;
class QToolBar;

namespace ppcb {

class CanvasView;
class GraphicPanel;
class InPlaceEdit;

// What is chosen in an editor of the library: its terminals (the pads of a
// footprint, the pins of a symbol) and its drawings by index, and the
// places of the designator and of the value.
struct ItemChoice
{
    QSet<int> terminals;
    QSet<int> graphics;
    bool designator = false;
    bool value = false;

    bool isEmpty() const
    {
        return terminals.isEmpty() && graphics.isEmpty() && !designator && !value;
    }
};

bool operator==(const ItemChoice &a, const ItemChoice &b);
inline bool operator!=(const ItemChoice &a, const ItemChoice &b) { return !(a == b); }

// A panel beside the canvas of an item editor; shown again whenever the
// editor changes. A field being typed in is left alone while the same
// things stay chosen, unless 'force'.
class ItemPanel : public QWidget
{
public:
    using QWidget::QWidget;
    virtual void refresh(bool force = false) = 0;
};

// A tab of the library editor window editing one item of the library — a
// footprint or a symbol — with its own strip of tools and its own Undo: its
// terminals placed, drawings drawn, both chosen, dragged on the grid,
// turned, deleted and given their properties in the panels beside, the
// places of the designator and the value dragged, the origin moved and
// distances measured. It saves into the user's library, where an item of
// the built-in one's name replaces it (see mergeLibraries()). Its keys
// work while the keyboard is in it.
//
// This class holds what does not depend on the kind of item;
// ItemEditorOf<Item> the rest, FootprintEditor and SymbolEditor their
// terminals.
class ItemEditor : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        // Clicks choose, drags move what is chosen, a corner of a drawing
        // or draw a frame.
        Select,
        // Each click places a terminal like the last one, numbered on.
        Place,
        // A click places the row set up in the Row dialog, then Select;
        // footprints only.
        Row,
        // A click puts the origin there, then Select.
        Origin,
        // A line from click to click, until Esc or a double click; a
        // rectangle by two corners, with Shift an oval in them; a circle by
        // its centre and a point on it; an arc by its start, a point in its
        // middle and its end; a polygon by its corners, closed on its first
        // one or by a double click; a text typed where it is clicked.
        Line,
        Rect,
        Circle,
        Arc,
        Polygon,
        Text,
        // Click, then click again: the distance between, across and up and
        // down, shown till Esc; a point near a terminal is taken at its
        // point.
        Measure,
    };

    ~ItemEditor() override;

    // The step that sets the properties of drawings in the panel.
    static QString drawingPropertiesStep();
    // "Footprint editor", "Symbol editor".
    virtual QString editorTitle() const = 0;
    // "New footprint", "New symbol".
    virtual QString newItemTitle() const = 0;

    QString name() const { return m_name; }
    CanvasView *canvas() const { return m_canvas; }
    QUndoStack *undoStack() { return &m_undoStack; }
    // True when there is nothing unsaved.
    bool isSaved() const { return m_undoStack.isClean(); }
    // The name on its tab: the item's, or newItemTitle(), with a star
    // while something is not saved.
    QString tabTitle() const;
    // What its tab says when the mouse rests on it: the kind, the name, and
    // whether it is still the built-in item.
    QString tabTip() const;
    // The last message it put into the status bar.
    QString status() const { return m_status; }
    // Asks what to do with unsaved changes before it closes: save them, drop
    // them or stay. True when it may close; always true without changes.
    bool confirmClose();

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    const ItemChoice &chosen() const { return m_chosen; }
    // What does not exist is left out.
    void setChosen(const ItemChoice &chosen);
    // The line width and dashes of the drawings drawn next.
    const Graphic &nextGraphic() const { return m_nextGraphic; }
    void setNextGraphic(const Graphic &graphic);
    // The drawings of the item.
    virtual const QList<Graphic> &graphics() const = 0;
    // One step of Undo named 'text': the drawings become 'changed', the
    // choice staying.
    virtual void changeGraphics(const QString &text, const QList<Graphic> &changed) = 0;

    // Writes the item into the user's library, asking for its name first
    // when it has none. False, with the reason told, when it is not written.
    bool save();
    // The same under a name asked for: from then on the item of that name
    // is edited; the one it was opened as stays as it was.
    bool saveUnderNewName();
    // The same under 'name', asking nothing; for a name already taken
    // only with 'replace'.
    bool saveAs(const QString &name, bool replace);
    // An item of the user's library under a new name asked for: saved so,
    // its old file gone and the parts of the user's library listing it
    // given the new name. Built-in parts cannot follow: when some list it,
    // the user is asked first. False when it is not renamed.
    bool renameItem();
    // The same to 'name', asking nothing.
    bool renameTo(const QString &name);
    // Only one of the user's library can be renamed.
    bool canRename() const { return m_inUserLibrary && !m_name.isEmpty(); }

    // The left mouse button and the mouse, in scene millimetres; with
    // 'adding' (Ctrl) what is clicked is added to the chosen or taken away,
    // and a frame adds what it holds; with 'free' (Shift) a line goes at
    // any angle.
    virtual void press(const QPointF &at, bool adding, bool free = false) = 0;
    void move(const QPointF &at, bool free = false);
    virtual void release(const QPointF &at) = 0;
    virtual void doubleClick(const QPointF &at) = 0;
    void hover(const QPointF &at, bool free = false);

    // The drawing being drawn, to where the cursor is; none before its
    // first click.
    QList<Graphic> floatingGraphics() const;
    // The origin where a click of the Origin mode would put it.
    std::optional<Point> floatingOrigin() const;
    // The frame being drawn; null when none is.
    QRectF frame() const;
    // What the Measure mode measures, in millimetres: from its first click
    // to its second, or to the cursor until then; none before the first.
    std::optional<QLineF> measurement() const;

    // R: what is chosen turns a quarter about its middle, or what is about
    // to be placed turns.
    virtual void rotate() = 0;
    // Del: the terminals and drawings chosen go.
    virtual void deleteChosen() = 0;
    void chooseAll();
    // Esc: a drag or a frame is called off, a line or a polygon being
    // drawn ends, else the mode ends, else nothing stays chosen.
    void escape();
    // The origin put in the middle of the item, on the grid.
    virtual void originAtMiddle() = 0;
    // Puts 'text' into the status bar of the window.
    void showStatus(const QString &text);
    // An error or a warning in a box as well (Alerts::show()).
    void alert(const Alert &alert);
    // Draws the item and what the editor shows over it, in scene
    // millimetres.
    virtual void paintEditor(QPainter &painter) const = 0;

signals:
    // The item, the choice, the mode or what follows the cursor changed:
    // the drawing and the panels are stale.
    void changed();
    // The item was written into the user's library as 'name'.
    void saved(const QString &name);
    // What its tab shows changed: its name or whether all is saved.
    void titleChanged();
    // A message for the status bar of the window.
    void statusChanged(const QString &text);

protected:
    // 'name' is the item's name in the library; empty for a new one, which
    // is named when it is first saved. 'inUserLibrary' tells whether the
    // user's library has it already, or only the built-in one or the
    // project does.
    ItemEditor(const QString &name, bool inUserLibrary, QWidget *parent);

    // Lays out the strip, the canvas and the panels beside it: the panel of
    // the terminals or the drawings, whichever are chosen, and 'itemPanel'
    // under it; the canvas on the grid 'gridStep' at first, its terminals
    // perhaps on a grid of their own (terminalGrid()). Called once, at the
    // end of the constructor of an editor of a kind.
    void build(ItemPanel *terminalPanel, ItemPanel *itemPanel, Coord gridStep);
    // The buttons that place terminals, put on the strip after Select.
    virtual void addPlacingTools(QToolBar *bar) = 0;
    // An action of the tab with its key in its tip; on the strip when it
    // has a picture.
    QAction *addTool(QToolBar *bar, const QString &text, const char *name, const QKeySequence &key,
                     std::optional<ToolIcon> icon, const std::function<void()> &slot);
    // A button of the strip with the action 'main' and 'menu' under its
    // arrow.
    void addMenuButton(QToolBar *bar, QAction *main, QMenu *menu, const char *name);
    void updateModeButtons();

    virtual int terminalCount() const = 0;
    // The grid every terminal stays on; 0 for the grid of the canvas.
    virtual Coord terminalGrid() const { return 0; }
    // The rectangle the view is fitted to.
    virtual QRectF shownBounds() const = 0;
    // With some chosen, a word on them in the status bar.
    virtual void describeChoice() {}

    // What the file of the item is, in the user's library folder.
    virtual const char *libraryFolder() const = 0;
    virtual LibraryItem libraryItem() const = 0;
    // Why the item cannot be written; empty when it can.
    virtual QString saveProblem() const = 0;
    // The parts of 'library' that list the item 'name', and one of them
    // listing 'to' in its place.
    virtual QStringList partsUsing(const Library &library, const QString &name) const = 0;
    virtual Part partRenamed(const Part &part, const QString &from, const QString &to) const = 0;
    // The words of the kind: the label the name is asked with, the
    // question before a name taken is replaced, before built-in parts are
    // left with the old name, before unsaved changes are lost, and what
    // is said once it is renamed.
    virtual QString nameLabel() const = 0;
    virtual QString takenQuestion(const QString &name) const = 0;
    virtual QString strandedQuestion(const QString &name, const QStringList &parts) const = 0;
    virtual QString unsavedQuestion() const = 0;
    virtual QString renamedMessage(const QString &from, const QString &to) const = 0;
    // True when the user's library has an item 'name' in 'folder'.
    static bool userLibraryHas(const char *folder, const QString &name);

    Point snapped(const QPointF &at) const;
    // 'at' on the grid of the terminals, terminalGrid(), or of the canvas.
    Point terminalSnapped(const QPointF &at) const;
    // The point a drawing mode puts at 'cursor': across, up and down or at
    // 45 degrees from the last one for a line, unless 'm_free'.
    Point drawPoint(const Point &cursor) const;
    // The pad panel, or the drawing panel while only drawings are chosen.
    void showPanel();
    // The square on a point that can be dragged by itself, the same size
    // at any zoom.
    void paintHandle(QPainter &painter, const Point &at) const;

    QString m_name;
    bool m_inUserLibrary = false;
    QUndoStack m_undoStack;
    CanvasView *m_canvas = nullptr;
    // A text typed on the canvas: a terminal's number, a drawn text.
    InPlaceEdit *m_textEdit = nullptr;
    QHash<int, QAction *> m_modeActions;

    Mode m_mode = Mode::Select;
    ItemChoice m_chosen;
    Graphic m_nextGraphic;
    // Where the cursor is, on the grid, and whether Shift is down.
    Point m_cursor;
    bool m_cursorKnown = false;
    bool m_free = false;
    // The points clicked of the drawing being drawn.
    QList<Point> m_drawPoints;
    // Where the mouse is, off the grid, and the ends of a measurement.
    QPointF m_hoverAt;
    std::optional<Point> m_measureFrom;
    std::optional<Point> m_measureTo;
    // A drag of what is chosen from 'm_dragAnchor', the point taken, which
    // lands on the grid; by 'm_dragDelta' so far. With 'm_dragGraphic' set,
    // only its corner 'm_dragCorner' moves; with 'm_dragTerminal' set, only
    // that terminal's end (ItemEditorOf::terminalEnd()).
    bool m_dragging = false;
    Point m_dragAnchor;
    Point m_dragDelta;
    int m_dragGraphic = -1;
    int m_dragCorner = -1;
    int m_dragTerminal = -1;
    QPointF m_pressAt;
    // What the press took of several chosen: chosen alone when the mouse
    // is let go without a drag.
    std::optional<ItemChoice> m_pressAlone;
    // A frame from 'm_frameFrom' to 'm_frameTo'.
    bool m_framing = false;
    bool m_frameAdding = false;
    QPointF m_frameFrom;
    QPointF m_frameTo;

private:
    QWidget *createToolBar();
    // A name typed for the item, asked with 'title'; empty when none is or
    // the user keeps it from replacing another.
    QString askName(const QString &title);
    // A point the Measure mode takes at 'at': a terminal's point there,
    // else the grid point.
    virtual Point measurePoint(const QPointF &at) const = 0;

    QString m_status;
    QStackedWidget *m_panels = nullptr;
    ItemPanel *m_terminalPanel = nullptr;
    GraphicPanel *m_graphicPanel = nullptr;
    ItemPanel *m_itemPanel = nullptr;
    QComboBox *m_gridBox = nullptr;
    QAction *m_renameAction = nullptr;
};

// The part of an item editor that works on the item itself: a Footprint or
// a Symbol, whose 'graphics', 'refAt' and 'valueAt' it edits. Its undo
// steps keep the whole item, so an index lasts as long as the item it was
// taken from.
template <class Item>
class ItemEditorOf : public ItemEditor
{
public:
    const Item &item() const { return m_item; }
    // What the drawing shows: the item with what is chosen where a drag
    // puts it.
    Item shownItem() const;
    // One step of Undo named 'text': the item becomes 'changed', with
    // 'chosen' chosen. Nothing is done when it is the same. 'chosen' is a
    // copy: the step clears m_chosen, which callers pass.
    void change(const QString &text, const Item &changed, ItemChoice chosen);
    // Undo and Redo put 'item' back; what was chosen may be gone.
    void applyStep(const Item &item);

    const QList<Graphic> &graphics() const override { return m_item.graphics; }
    void changeGraphics(const QString &text, const QList<Graphic> &changed) override;
    void press(const QPointF &at, bool adding, bool free = false) override;
    void release(const QPointF &at) override;
    void doubleClick(const QPointF &at) override;
    void rotate() override;
    void deleteChosen() override;
    void originAtMiddle() override;
    void paintEditor(QPainter &painter) const override;

protected:
    ItemEditorOf(const QString &name, const Item &item, bool inUserLibrary, QWidget *parent);

    // The terminals of the item, by index.
    virtual int terminalAt(const QPointF &at, double reach) const = 0;
    virtual QSet<int> terminalsInside(const QRectF &rect) const = 0;
    virtual QRectF terminalsBounds(const Item &item, const QSet<int> &terminals) const = 0;
    virtual Point terminalPoint(int terminal) const = 0;
    virtual Item withTerminalsMoved(const Item &item, const QSet<int> &terminals,
                                    const Point &delta) const = 0;
    virtual Item withTerminalsRotated(const Item &item, const QSet<int> &terminals,
                                      const Point &center) const = 0;
    virtual Item withoutTerminals(const Item &item, const QSet<int> &terminals) const = 0;
    // The end of 'terminal' that is dragged by itself, changing it; none
    // for a kind without one. Over the corners of the drawings while the
    // terminal is chosen, under them while it is not.
    virtual std::optional<Point> terminalEnd(int terminal) const
    {
        Q_UNUSED(terminal);
        return std::nullopt;
    }
    // 'item' with the end of 'terminal' dragged to 'to'.
    virtual Item withTerminalEnd(const Item &item, int terminal, const Point &to) const
    {
        Q_UNUSED(terminal);
        Q_UNUSED(to);
        return item;
    }
    // A click of the Place or the Row mode at 'at'.
    virtual void placeAt(const QPointF &at) = 0;
    // R while placing.
    virtual void rotatePlacing() = 0;
    // A double click on a terminal: its number typed in place.
    virtual void editTerminal(int terminal) = 0;
    // The item became another, by a step or by Undo.
    virtual void itemReplaced() {}
    // The middle of the item, not on any grid.
    virtual Point itemMiddle() const = 0;
    // The item as it is shown, and over it what belongs to its kind.
    virtual void paintShown(QPainter &painter, const Item &shown) const = 0;
    // The chosen terminals of 'shown' framed, with the pen set.
    virtual void paintChosenTerminals(QPainter &painter, const Item &shown) const = 0;
    // What follows the cursor, half seen already: the terminals about to be
    // placed and 'graphics', the drawing being drawn.
    virtual void paintFloating(QPainter &painter, const QList<Graphic> &graphics) const = 0;

    Item m_item;

private:
    Point measurePoint(const QPointF &at) const override;
    // The rectangle round what is chosen; null when nothing is.
    QRectF choiceBounds() const;
    Item withChoiceMoved(const Item &item, const Point &delta) const;
    // A drawing added as one step, with 'm_nextGraphic''s line.
    void addGraphic(const QString &text, Graphic graphic);
    // A click of a drawing mode at the grid point 'at'.
    void drawClick(const Point &at);
    // The line or the polygon being drawn ends; a polygon of three corners
    // or more is added.
    void finishDrawing();
    // A text typed at 'at': a new one, or the text of drawing 'graphic'.
    void editText(const Point &at, int graphic);
    // The terminal whose end (terminalEnd()) is within 'reach' of 'at', and
    // within the third of the terminal nearest it, among the chosen ones
    // only with 'chosenOnly'; -1 for none.
    int terminalEndNear(const QPointF &at, double reach, bool chosenOnly) const;
    // Starts a drag of what press() found at a point: the corner 'corner'
    // of drawing 'graphic', or the end of 'terminal'.
    void dragCorner(int graphic, int corner);
    void dragTerminalEnd(int terminal);
};

// Made once, in itemeditor.cpp.
extern template class ItemEditorOf<Footprint>;
extern template class ItemEditorOf<Symbol>;

} // namespace ppcb
