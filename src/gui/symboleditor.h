#pragma once

#include "itemeditor.h"
#include "symboledit.h"

#include <QSet>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;

namespace ppcb {

// A tab of the library editor window editing one symbol of the library
// (see ItemEditor): its pins placed, their point always on the pin grid
// (2.54 mm) whatever the grid of the drawings, its body drawn, and whether
// it shows pin numbers and names or is a power symbol.
class SymbolEditor : public ItemEditorOf<Symbol>
{
    Q_OBJECT

public:
    SymbolEditor(const QString &name, const Symbol &symbol, bool inUserLibrary,
                 QWidget *parent = nullptr);

    // The step that sets the properties of pins, or of the symbol, in a
    // panel.
    static QString pinPropertiesStep();
    static QString symbolPropertiesStep();
    // "New symbol".
    static QString newSymbolTitle();
    // True when the user's library has a symbol 'name' of its own.
    static bool isInUserLibrary(const QString &name);
    QString editorTitle() const override;
    QString newItemTitle() const override { return newSymbolTitle(); }

    const Symbol &symbol() const { return m_item; }
    // The pin the Place mode places next, but for its place and number.
    const Pin &nextPin() const { return m_nextPin; }
    void setNextPin(const Pin &pin);
    // The pin that follows the cursor in the Place mode, where it would be
    // placed; none in the others.
    QList<Pin> floatingPins() const;

protected:
    void addPlacingTools(QToolBar *bar) override;
    int terminalCount() const override { return int(m_item.pins.size()); }
    Coord terminalGrid() const override { return PIN_GRID; }
    QRectF shownBounds() const override;
    const char *libraryFolder() const override;
    LibraryItem libraryItem() const override;
    QString saveProblem() const override { return symbolProblem(m_item); }
    QStringList partsUsing(const Library &library, const QString &name) const override;
    Part partRenamed(const Part &part, const QString &from, const QString &to) const override;
    QString nameLabel() const override;
    QString takenQuestion(const QString &name) const override;
    QString strandedQuestion(const QString &name, const QStringList &parts) const override;
    QString unsavedQuestion() const override;
    QString renamedMessage(const QString &from, const QString &to) const override;

    int terminalAt(const QPointF &at, double reach) const override;
    QSet<int> terminalsInside(const QRectF &rect) const override;
    QRectF terminalsBounds(const Symbol &item, const QSet<int> &terminals) const override;
    Point terminalPoint(int terminal) const override;
    Symbol withTerminalsMoved(const Symbol &item, const QSet<int> &terminals,
                              const Point &delta) const override;
    Symbol withTerminalsRotated(const Symbol &item, const QSet<int> &terminals,
                                const Point &center) const override;
    Symbol withoutTerminals(const Symbol &item, const QSet<int> &terminals) const override;
    // The end at the body: dragged, it sets the length.
    std::optional<Point> terminalEnd(int terminal) const override;
    Symbol withTerminalEnd(const Symbol &item, int terminal, const Point &to) const override;
    void placeAt(const QPointF &at) override;
    void rotatePlacing() override;
    void editTerminal(int terminal) override;
    Point itemMiddle() const override { return symbolMiddle(m_item); }
    void paintShown(QPainter &painter, const Symbol &shown) const override;
    void paintChosenTerminals(QPainter &painter, const Symbol &shown) const override;
    void paintFloating(QPainter &painter, const QList<Graphic> &graphics) const override;

private:
    Pin m_nextPin;
};

// The properties of the pins chosen in a symbol editor: number, name, the
// side the wire comes from, length and place. With several chosen, a field
// shows what they all have, or stays empty saying they differ, and what is
// set there goes to them all as one step; the number is set one pin at a
// time. With none chosen, the fields are those of the pin placed next.
// Enter or leaving a field sets what is typed, Esc takes it back.
class PinPanel : public ItemPanel
{
    Q_OBJECT

public:
    explicit PinPanel(SymbolEditor *editor, QWidget *parent = nullptr);

    static QString pinTitle();
    void refresh(bool force = false) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Sets a field on the pins shown: 'apply' changes one pin.
    template <typename Apply>
    void commit(Apply apply);
    // A length field typed: 'edit' (length, X or Y) set from it.
    void commitMm(QLineEdit *edit);

    SymbolEditor *m_editor;
    QSet<int> m_shown;
    QLabel *m_heading = nullptr;
    QLineEdit *m_number = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_side = nullptr;
    QLineEdit *m_length = nullptr;
    QLineEdit *m_x = nullptr;
    QLineEdit *m_y = nullptr;
};

// The symbol as a whole, under the other panel: whether it shows the
// numbers and the names of its pins, and the net of a power symbol, whose
// name it may hide. Each field set is a step of its own.
class SymbolPanel : public ItemPanel
{
    Q_OBJECT

public:
    explicit SymbolPanel(SymbolEditor *editor, QWidget *parent = nullptr);

    static QString symbolTitle();
    void refresh(bool force = false) override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void commit(const Symbol &changed);

    SymbolEditor *m_editor;
    QCheckBox *m_numbers = nullptr;
    QCheckBox *m_names = nullptr;
    QLineEdit *m_power = nullptr;
    QCheckBox *m_hidePower = nullptr;
};

} // namespace ppcb
