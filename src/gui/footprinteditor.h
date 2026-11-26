#pragma once

#include "footprintedit.h"

#include <QHash>
#include <QWidget>
#include <QSet>
#include <QUndoStack>

#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;

namespace ppcb {

class CanvasView;
class InPlaceEdit;
class PadPanel;

// A tab of the library editor window editing one footprint of the library,
// with its own strip of tools and its own Undo: pads placed one by one or in
// a row, chosen, dragged on the grid, turned, deleted and given their
// properties in the panel beside, and the origin moved. It saves into the
// user's library, where a footprint of the built-in one's name replaces it
// (see mergeLibraries()). Its keys work while the keyboard is in it.
class FootprintEditor : public QWidget
{
    Q_OBJECT

public:
    enum class Mode {
        // Clicks choose pads, drags move them or draw a frame.
        Select,
        // Each click places a pad like the last one, numbered on.
        Pad,
        // A click places the row set up in the Row dialog, then Select.
        Row,
        // A click puts the origin there, then Select.
        Origin,
    };

    // 'name' is the footprint's name in the library; empty for a new one,
    // which is named when it is first saved. 'inUserLibrary' tells whether
    // the user's library has it already, or only the built-in one or the
    // project does.
    FootprintEditor(const QString &name, const Footprint &footprint, bool inUserLibrary,
                    QWidget *parent = nullptr);

    // "Footprint editor".
    static QString editorTitle();
    // The step that sets the properties of pads in the panel.
    static QString padPropertiesStep();
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

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);
    // The pads chosen, by index.
    const QSet<int> &chosen() const { return m_chosen; }
    void setChosen(const QSet<int> &pads);
    // The pad the Pad mode places next, but for its place and number.
    const Pad &nextPad() const { return m_nextPad; }
    void setNextPad(const Pad &pad);
    // The row the Row mode places, pad 1 at the origin.
    const QList<Pad> &rowPads() const { return m_rowPads; }

    // One step of Undo named 'text': the footprint becomes 'changed', with
    // 'chosen' chosen. Nothing is done when it is the same.
    void change(const QString &text, const Footprint &changed, const QSet<int> &chosen);

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

    // The left mouse button and the mouse, in scene millimetres; with
    // 'adding' (Ctrl) a pad clicked is added to the chosen or taken away,
    // and a frame adds what it holds.
    void press(const QPointF &at, bool adding);
    void move(const QPointF &at);
    void release(const QPointF &at);
    void doubleClick(const QPointF &at);
    void hover(const QPointF &at);

    // What the drawing shows: the footprint with the chosen pads where a
    // drag puts them.
    Footprint shownFootprint() const;
    // The pads that follow the cursor in the Pad and Row modes, where they
    // would be placed; none in the others.
    QList<Pad> floatingPads() const;
    // The origin where a click of the Origin mode would put it.
    std::optional<Point> floatingOrigin() const;
    // The frame being drawn; null when none is.
    QRectF frame() const;

    // R: the pads chosen turn a quarter about their middle, or the pad or
    // the row about to be placed turns.
    void rotate();
    void deleteChosen();
    void chooseAll();
    // Esc: a drag or a frame is called off, else the mode ends, else
    // nothing stays chosen.
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
    void editNumber(int pad);
    Point snapped(const QPointF &at) const;

    QString m_name;
    bool m_inUserLibrary = false;
    Footprint m_footprint;
    QUndoStack m_undoStack;
    CanvasView *m_canvas = nullptr;
    PadPanel *m_panel = nullptr;
    InPlaceEdit *m_numberEdit = nullptr;
    QComboBox *m_gridBox = nullptr;
    QString m_status;
    QHash<int, QAction *> m_modeActions;

    Mode m_mode = Mode::Select;
    QSet<int> m_chosen;
    Pad m_nextPad;
    QList<Pad> m_rowPads;
    PadRow m_row;
    // Where the cursor is, on the grid.
    Point m_cursor;
    bool m_cursorKnown = false;
    // A drag of the chosen pads from 'm_dragAnchor', the pad taken, which
    // lands on the grid; by 'm_dragDelta' so far.
    bool m_dragging = false;
    Point m_dragAnchor;
    Point m_dragDelta;
    QPointF m_pressAt;
    // The pad the press took: a click on one of several chosen chooses it
    // alone when the mouse is let go without a drag.
    int m_pressPad = -1;
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
    bool isTyping(const QWidget *field, bool force) const;

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

} // namespace ppcb
