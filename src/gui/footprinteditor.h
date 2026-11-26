#pragma once

#include "footprintedit.h"
#include "itemeditor.h"

#include <QColor>
#include <QSet>

#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QToolButton;

namespace ppcb {

// A tab of the library editor window editing one footprint of the library
// (see ItemEditor): its pads placed one by one or in a row, its silkscreen
// drawn, its body set. Pads nearer than the clearance of the project are
// ringed in red.
class FootprintEditor : public ItemEditorOf<Footprint>
{
    Q_OBJECT

public:
    FootprintEditor(const QString &name, const Footprint &footprint, bool inUserLibrary,
                    QWidget *parent = nullptr);

    // The step that sets the properties of pads in the panel.
    static QString padPropertiesStep();
    // "New footprint".
    static QString newFootprintTitle();
    // True when the user's library has a footprint 'name' of its own.
    static bool isInUserLibrary(const QString &name);
    QString editorTitle() const override;
    QString newItemTitle() const override { return newFootprintTitle(); }

    const Footprint &footprint() const { return m_item; }

    // The least copper between two pads, of the open project's rules; 0
    // checks nothing.
    void setClearance(Coord clearance);
    Coord clearance() const { return m_clearance; }
    // The pads nearer than the clearance, as the footprint stands.
    const QList<PadGap> &padGaps() const { return m_gaps; }

    // The pad the Place mode places next, but for its place and number.
    const Pad &nextPad() const { return m_nextPad; }
    void setNextPad(const Pad &pad);
    // The row the Row mode places, pad 1 at the origin.
    const QList<Pad> &rowPads() const { return m_rowPads; }
    // The pads that follow the cursor in the Place and Row modes, where
    // they would be placed; none in the others.
    QList<Pad> floatingPads() const;
    // Asks for a row of pads and starts placing it.
    void askRow();

protected:
    void addPlacingTools(QToolBar *bar) override;
    int terminalCount() const override { return int(m_item.pads.size()); }
    QRectF shownBounds() const override;
    // With two pads chosen, the copper and the centres between them in the
    // status bar.
    void describeChoice() override;
    const char *libraryFolder() const override;
    LibraryItem libraryItem() const override;
    QString saveProblem() const override;
    QStringList partsUsing(const Library &library, const QString &name) const override;
    Part partRenamed(const Part &part, const QString &from, const QString &to) const override;
    QString nameLabel() const override;
    QString takenQuestion(const QString &name) const override;
    QString strandedQuestion(const QString &name, const QStringList &parts) const override;
    QString unsavedQuestion() const override;
    QString renamedMessage(const QString &from, const QString &to) const override;

    int terminalAt(const QPointF &at, double reach) const override;
    QSet<int> terminalsInside(const QRectF &rect) const override;
    QRectF terminalsBounds(const Footprint &item, const QSet<int> &terminals) const override;
    Point terminalPoint(int terminal) const override;
    Footprint withTerminalsMoved(const Footprint &item, const QSet<int> &terminals,
                                 const Point &delta) const override;
    Footprint withTerminalsRotated(const Footprint &item, const QSet<int> &terminals,
                                   const Point &center) const override;
    Footprint withoutTerminals(const Footprint &item, const QSet<int> &terminals) const override;
    void placeAt(const QPointF &at) override;
    void rotatePlacing() override;
    void editTerminal(int terminal) override;
    void itemReplaced() override { checkGaps(); }
    Point itemMiddle() const override { return footprintMiddle(m_item); }
    void paintShown(QPainter &painter, const Footprint &shown) const override;
    void paintChosenTerminals(QPainter &painter, const Footprint &shown) const override;
    void paintFloating(QPainter &painter, const QList<Graphic> &graphics) const override;

private:
    // The gaps found again, and the first told in the status bar.
    void checkGaps();

    Coord m_clearance = 0;
    QList<PadGap> m_gaps;
    Pad m_nextPad;
    QList<Pad> m_rowPads;
    PadRow m_row;
};

// The properties of the pads chosen in a footprint editor: number, shape,
// width, height, hole, sides and place. With several chosen, a field shows
// what they all have, or stays empty saying they differ, and what is set
// there goes to them all as one step; the number is set one pad at a time.
// With none chosen, the fields are those of the pad the Pad mode places
// next. Enter or leaving a field sets what is typed, Esc takes it back.
class PadPanel : public ItemPanel
{
    Q_OBJECT

public:
    explicit PadPanel(FootprintEditor *editor, QWidget *parent = nullptr);

    // The panel's title.
    static QString padTitle();

    // Shows the chosen pads again. A field being typed in is left alone
    // while the same pads stay chosen, unless 'force'.
    void refresh(bool force = false) override;

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

// The body of the footprint, under the other panel: its shape or none, a
// ready-made one, its size (empty for the drawing of the silkscreen), its
// height, how far above the board it stands, and its colour from a table;
// under them the footprint in three dimensions. Each field set is a step
// of its own.
class BodyPanel : public ItemPanel
{
    Q_OBJECT

public:
    explicit BodyPanel(FootprintEditor *editor, QWidget *parent = nullptr);

    static QString bodyTitle();
    // The colours a body is offered in: 160, greys and the colour wheel
    // in nine shades, dull ones among them.
    static QList<QColor> bodyColors();
    void refresh(bool force = false) override;

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

} // namespace ppcb
