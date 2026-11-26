#pragma once

#include "footprintedit.h"
#include "paint.h"

#include <QColor>
#include <QHash>
#include <QLineF>
#include <QSet>
#include <QUndoStack>
#include <QWidget>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QStackedWidget;
class QToolButton;

namespace ppcb {

class BodyPanel;
class CanvasView;
class GraphicPanel;
class InPlaceEdit;
class PadPanel;

// What is chosen in a footprint editor: pads and drawings by index, and the
// places of the designator and of the value.
struct FootprintChoice
{
    QSet<int> pads;
    QSet<int> graphics;
    bool designator = false;
    bool value = false;

    bool isEmpty() const { return pads.isEmpty() && graphics.isEmpty() && !designator && !value; }
};

bool operator==(const FootprintChoice &a, const FootprintChoice &b);
inline bool operator!=(const FootprintChoice &a, const FootprintChoice &b) { return !(a == b); }

// A tab of the library editor window editing one footprint of the library,
// with its own strip of tools and its own Undo: pads placed one by one or in
// a row, the silkscreen drawn, both chosen, dragged on the grid, turned,
// deleted and given their properties in the panel beside, the places of the
// designator and the value dragged, and the origin moved. Pads nearer than
// the clearance of the project are ringed in red. It saves into the user's
// library, where a footprint of the built-in one's name replaces it (see
// mergeLibraries()). Its keys work while the keyboard is in it.
class FootprintEditor : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        // Clicks choose, drags move what is chosen, a corner of a drawing
        // or draw a frame.
        Select,
        // Each click places a pad like the last one, numbered on.
        Pad,
        // A click places the row set up in the Row dialog, then Select.
        Row,
        // A click puts the origin there, then Select.
        Origin,
        // The silkscreen: a line from click to click, until Esc or a double
        // click; a rectangle by two corners, with Shift an oval in them; a
        // circle by its centre and a
        // point on it; an arc by its start, a point in its middle and its
        // end; a polygon by its corners, closed on its first one or by a
        // double click; a text typed where it is clicked.
        Line,
        Rect,
        Circle,
        Arc,
        Polygon,
        Text,
        // Click, then click again: the distance between, across and up and
        // down, shown till Esc; a point near a pad is taken at its centre.
        Measure,
    };

    // 'name' is the footprint's name in the library; empty for a new one,
    // which is named when it is first saved. 'inUserLibrary' tells whether
    // the user's library has it already, or only the built-in one or the
    // project does.
    FootprintEditor(const QString &name, const Footprint &footprint, bool inUserLibrary,
                    QWidget *parent = nullptr);
    ~FootprintEditor() override;

    // "Footprint editor".
    static QString editorTitle();
    // The step that sets the properties of pads, or of drawings, in the panel.
    static QString padPropertiesStep();
    static QString drawingPropertiesStep();
    // "New footprint".
    static QString newFootprintTitle();
    // True when the user's library has a footprint 'name' of its own.
    static bool isInUserLibrary(const QString &name);

    QString name() const { return m_name; }
    const Footprint &footprint() const { return m_footprint; }
    CanvasView *canvas() const { return m_canvas; }
    QUndoStack *undoStack() { return &m_undoStack; }
    // True when there is nothing unsaved.
    bool isSaved() const { return m_undoStack.isClean(); }
    // The name on its tab: the footprint's, or "New footprint", with a star
    // while something is not saved.
    QString tabTitle() const;
    // What its tab says when the mouse rests on it: the name, and whether
    // it is still the built-in footprint.
    QString tabTip() const;
    // The last message it put into the status bar.
    QString status() const { return m_status; }
    // Asks what to do with unsaved changes before it closes: save them, drop
    // them or stay. True when it may close; always true without changes.
    bool confirmClose();

    // The least copper between two pads, of the open project's rules; 0
    // checks nothing.
    void setClearance(Coord clearance);
    Coord clearance() const { return m_clearance; }
    // The pads nearer than the clearance, as the footprint stands.
    const QList<PadGap> &padGaps() const { return m_gaps; }

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    const FootprintChoice &chosen() const { return m_chosen; }
    void setChosen(const FootprintChoice &chosen);
    // The pad the Pad mode places next, but for its place and number.
    const Pad &nextPad() const { return m_nextPad; }
    void setNextPad(const Pad &pad);
    // The line width and dashes of the drawings drawn next.
    const Graphic &nextGraphic() const { return m_nextGraphic; }
    void setNextGraphic(const Graphic &graphic);
    // The row the Row mode places, pad 1 at the origin.
    const QList<Pad> &rowPads() const { return m_rowPads; }

    // One step of Undo named 'text': the footprint becomes 'changed', with
    // 'chosen' chosen. Nothing is done when it is the same.
    void change(const QString &text, const Footprint &changed, const FootprintChoice &chosen);

    // Writes the footprint into the user's library, asking for its name
    // first when it has none. False, with the reason in the status bar,
    // when it is not written.
    bool save();
    // The same under a name asked for: from then on the footprint of that
    // name is edited; the one it was opened as stays as it was.
    bool saveUnderNewName();
    // The same under 'name', asking nothing; for a name already taken
    // only with 'replace'.
    bool saveAs(const QString &name, bool replace);
    // A footprint of the user's library under a new name asked for: saved
    // so, its old file gone and the parts of the user's library listing it
    // given the new name. Built-in parts cannot follow: when some list it,
    // the user is asked first. False when it is not renamed.
    bool renameFootprint();
    // The same to 'name', asking nothing.
    bool renameTo(const QString &name);
    // Only one of the user's library can be renamed.
    bool canRename() const { return m_inUserLibrary && !m_name.isEmpty(); }

    // The left mouse button and the mouse, in scene millimetres; with
    // 'adding' (Ctrl) what is clicked is added to the chosen or taken away,
    // and a frame adds what it holds; with 'free' (Shift) a line goes at
    // any angle.
    void press(const QPointF &at, bool adding, bool free = false);
    void move(const QPointF &at, bool free = false);
    void release(const QPointF &at);
    void doubleClick(const QPointF &at);
    void hover(const QPointF &at, bool free = false);

    // What the drawing shows: the footprint with what is chosen where a
    // drag puts it.
    Footprint shownFootprint() const;
    // The pads that follow the cursor in the Pad and Row modes, where they
    // would be placed; none in the others.
    QList<Pad> floatingPads() const;
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

    // R: what is chosen turns a quarter about its middle, or the pad or the
    // row about to be placed turns.
    void rotate();
    // Del: the pads and drawings chosen go.
    void deleteChosen();
    void chooseAll();
    // Esc: a drag or a frame is called off, a line or a polygon being
    // drawn ends, else the mode ends, else nothing stays chosen.
    void escape();
    // The origin put in the middle of the part (footprintMiddle()), on the
    // grid.
    void originAtMiddle();
    // Asks for a row of pads and starts placing it.
    void askRow();
    // Puts 'text' into the status bar of the window.
    void showStatus(const QString &text);

signals:
    // The footprint, the choice, the mode or what follows the cursor
    // changed: the drawing and the panel are stale.
    void changed();
    // The footprint was written into the user's library as 'name'.
    void saved(const QString &name);
    // What its tab shows changed: its name or whether all is saved.
    void titleChanged();
    // A message for the status bar of the window.
    void statusChanged(const QString &text);

private:
    friend class FootprintStep;
    // Undo and Redo put 'footprint' back; what was chosen may be gone.
    void applyStep(const Footprint &footprint);
    // The strip of tools over the canvas.
    QWidget *createToolBar();
    // A name typed for the footprint, asked with 'title'; empty when none
    // is or the user keeps it from replacing another.
    QString askName(const QString &title);
    void updateModeButtons();
    // The pad panel, or the drawing panel while only drawings are chosen.
    void showPanel();
    // The gaps found again, and the first told in the status bar.
    void checkGaps();
    void editNumber(int pad);
    // A text typed at 'at': a new one, or the text of drawing 'graphic'.
    void editText(const Point &at, int graphic);
    // A click of a drawing mode at the grid point 'at'.
    void drawClick(const Point &at);
    // The line or the polygon being drawn ends; a polygon of three corners
    // or more is added.
    void finishDrawing();
    // The point a drawing mode puts at 'cursor': across, up and down or at
    // 45 degrees from the last one for a line, unless 'm_free'.
    Point drawPoint(const Point &cursor) const;
    // The point the Measure mode takes at 'at': the centre of a pad there,
    // else the grid point.
    Point measurePoint(const QPointF &at) const;
    // With two pads chosen, the copper and the centres between them in the
    // status bar.
    void describeChoice();
    // A drawing added as one step, with 'm_nextGraphic''s line.
    void addGraphic(const QString &text, Graphic graphic);
    Point snapped(const QPointF &at) const;

    QString m_name;
    bool m_inUserLibrary = false;
    Footprint m_footprint;
    QUndoStack m_undoStack;
    CanvasView *m_canvas = nullptr;
    QStackedWidget *m_panels = nullptr;
    PadPanel *m_padPanel = nullptr;
    GraphicPanel *m_graphicPanel = nullptr;
    BodyPanel *m_bodyPanel = nullptr;
    InPlaceEdit *m_numberEdit = nullptr;
    QComboBox *m_gridBox = nullptr;
    QString m_status;
    QHash<int, QAction *> m_modeActions;
    QAction *m_renameAction = nullptr;
    Coord m_clearance = 0;
    QList<PadGap> m_gaps;

    Mode m_mode = Mode::Select;
    FootprintChoice m_chosen;
    Pad m_nextPad;
    Graphic m_nextGraphic;
    QList<Pad> m_rowPads;
    PadRow m_row;
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
    // only its corner 'm_dragCorner' moves.
    bool m_dragging = false;
    Point m_dragAnchor;
    Point m_dragDelta;
    int m_dragGraphic = -1;
    int m_dragCorner = -1;
    QPointF m_pressAt;
    // What the press took of several chosen: chosen alone when the mouse
    // is let go without a drag.
    std::optional<FootprintChoice> m_pressAlone;
    // A frame from 'm_frameFrom' to 'm_frameTo'.
    bool m_framing = false;
    bool m_frameAdding = false;
    QPointF m_frameFrom;
    QPointF m_frameTo;
};

// The properties of the pads chosen in a footprint editor: number, shape,
// width, height, hole, sides and place. With several chosen, a field shows
// what they all have, or stays empty saying they differ, and what is set
// there goes to them all as one step; the number is set one pad at a time.
// With none chosen, the fields are those of the pad the Pad mode places
// next. Enter or leaving a field sets what is typed, Esc takes it back.
class PadPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PadPanel(FootprintEditor *editor, QWidget *parent = nullptr);

    // The panel's title.
    static QString padTitle();

    // Shows the chosen pads again. A field being typed in is left alone
    // while the same pads stay chosen, unless 'force'.
    void refresh(bool force = false);

protected:
    // Esc in a field takes back what was typed there.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Sets a field on the pads shown: 'apply' changes one pad, false when
    // what is typed cannot be taken.
    template <typename Apply>
    void commit(Apply apply);
    void commitNumber();
    void commitShape(int index);
    void commitLayers(int index);
    // A size field typed: 'field' (width, height, hole, X or Y) set from it.
    void commitMm(QLineEdit *edit);

    FootprintEditor *m_editor;
    QSet<int> m_shown;
    QLabel *m_heading = nullptr;
    QLineEdit *m_number = nullptr;
    QComboBox *m_shape = nullptr;
    QLineEdit *m_width = nullptr;
    QLineEdit *m_height = nullptr;
    QLineEdit *m_hole = nullptr;
    QComboBox *m_layers = nullptr;
    QLineEdit *m_x = nullptr;
    QLineEdit *m_y = nullptr;
};

// The properties of the drawings chosen in a footprint editor: the line
// width and dashes, the diameter of a circle or an arc, the fill of a
// polygon, and of a text its words, height and alignment; fields that none of them has are off. Several chosen are
// set as one, as the pad panel does; with none chosen, the line of the
// drawings drawn next.
class GraphicPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GraphicPanel(FootprintEditor *editor, QWidget *parent = nullptr);

    static QString drawingTitle();
    void refresh(bool force = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    template <typename Apply>
    void commit(Apply apply);

    FootprintEditor *m_editor;
    QSet<int> m_shown;
    QLabel *m_heading = nullptr;
    QLineEdit *m_width = nullptr;
    QCheckBox *m_dashed = nullptr;
    QLineEdit *m_diameter = nullptr;
    QCheckBox *m_filled = nullptr;
    QLineEdit *m_text = nullptr;
    QLineEdit *m_height = nullptr;
    QComboBox *m_align = nullptr;
};

// The body of the footprint, under the other panel: its shape or none, a
// ready-made one, its size (empty for the drawing of the silkscreen), its
// height, how far above the board it stands, and its colour from a table;
// under them the footprint in three dimensions. Each field set is a step
// of its own.
class BodyPanel : public QWidget
{
    Q_OBJECT

public:
    explicit BodyPanel(FootprintEditor *editor, QWidget *parent = nullptr);

    static QString bodyTitle();
    // The colours a body is offered in: 128, greys and the colour wheel
    // in seven shades.
    static QList<QColor> bodyColors();
    void refresh(bool force = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The footprint with 'body', as one step; the fields again when it is
    // the same.
    void commit(const std::optional<Body> &body);
    // A size field typed into the body there is.
    void commitMm();

    FootprintEditor *m_editor;
    QComboBox *m_shape = nullptr;
    QLineEdit *m_sizeX = nullptr;
    QLineEdit *m_sizeY = nullptr;
    QLineEdit *m_height = nullptr;
    QLineEdit *m_raised = nullptr;
    QToolButton *m_color = nullptr;
    QToolButton *m_presets = nullptr;
    // The footprint on a piece of board in three dimensions.
    QWidget *m_preview = nullptr;
};

// Esc in one of the line fields of a panel: the panel shows what is chosen
// again and the field lets go of the keyboard, the window keeping its Esc.
// True when 'event' was that.
bool panelFieldEscape(QObject *watched, QEvent *event, const std::function<void()> &refresh);

} // namespace ppcb
