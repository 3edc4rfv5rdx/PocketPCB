#include "symboleditor.h"

#include "format.h"
#include "inplaceedit.h"
#include "paint.h"

#include <QAction>
#include <QPainter>
#include <QToolBar>

namespace ppcb {

namespace {

// How far the frame of a chosen pin, and the line its number is typed in,
// stand off its line, in millimetres.
const double PIN_FRAME_MM = 0.4;
const double PIN_EDIT_MM = 1.0;

// The line of 'pin' in millimetres, grown by 'margin'.
QRectF pinFrame(const Pin &pin, double margin)
{
    return QRectF(toMm(pin.at), toMm(pinBodyEnd(pin)))
        .normalized()
        .adjusted(-margin, -margin, margin, margin);
}

} // namespace

SymbolEditor::SymbolEditor(const QString &name, const Symbol &symbol, bool inUserLibrary,
                           QWidget *parent)
    : ItemEditorOf<Symbol>(name, symbol, inUserLibrary, parent)
{
    // The pins go on as the symbol's last one, if it has one.
    if (!m_item.pins.isEmpty())
        setNextPin(m_item.pins.last());
    // The grid of the pins at first; a finer one for the drawings.
    build(new PinPanel(this, this), new SymbolPanel(this, this), PIN_GRID);
}

QString SymbolEditor::editorTitle() const
{
    return tr("Symbol editor");
}

QString SymbolEditor::pinPropertiesStep()
{
    return tr("Pin properties");
}

QString SymbolEditor::symbolPropertiesStep()
{
    return tr("Symbol properties");
}

QString SymbolEditor::newSymbolTitle()
{
    return tr("New symbol");
}

bool SymbolEditor::isInUserLibrary(const QString &name)
{
    return userLibraryHas(LIBRARY_SYMBOLS_FOLDER, name);
}

void SymbolEditor::setNextPin(const Pin &pin)
{
    m_nextPin = pin;
    // Its own number and name each pin is given.
    m_nextPin.number.clear();
    m_nextPin.name.clear();
    m_nextPin.at = Point{};
    emit changed();
}

QList<Pin> SymbolEditor::floatingPins() const
{
    if (!m_cursorKnown || m_mode != Mode::Place)
        return {};
    Pin pin = m_nextPin;
    pin.at = terminalSnapped(m_hoverAt);
    pin.number = nextPinNumber(m_item);
    return {pin};
}

void SymbolEditor::addPlacingTools(QToolBar *bar)
{
    QAction *pin = addTool(bar, PinPanel::pinTitle(), "actionPinTool", QKeySequence(Qt::Key_P),
                           ToolIcon::Pin, [this] { setMode(Mode::Place); });
    m_modeActions.insert(int(Mode::Place), pin);
}

QRectF SymbolEditor::shownBounds() const
{
    return symbolBounds(shownItem());
}

const char *SymbolEditor::libraryFolder() const
{
    return LIBRARY_SYMBOLS_FOLDER;
}

LibraryItem SymbolEditor::libraryItem() const
{
    LibraryItem item;
    item.kind = LibraryKind::Symbol;
    item.symbol = m_item;
    return item;
}

QStringList SymbolEditor::partsUsing(const Library &library, const QString &name) const
{
    return partsUsingSymbol(library, name);
}

Part SymbolEditor::partRenamed(const Part &part, const QString &from, const QString &to) const
{
    Part result = part;
    if (result.symbol == from)
        result.symbol = to;
    return result;
}

QString SymbolEditor::nameLabel() const
{
    return tr("Symbol name");
}

QString SymbolEditor::takenQuestion(const QString &name) const
{
    return tr("The library has a symbol %1 already. Replace it").arg(name);
}

QString SymbolEditor::strandedQuestion(const QString &name, const QStringList &parts) const
{
    return tr("Built-in parts are drawn with the symbol %1 and keep that name: %2. Rename it anyway")
        .arg(name, parts.join(QStringLiteral(", ")));
}

QString SymbolEditor::unsavedQuestion() const
{
    return tr("The symbol has changes that are not saved. Save them");
}

QString SymbolEditor::renamedMessage(const QString &from, const QString &to) const
{
    return tr("Symbol %1 renamed to %2").arg(from, to);
}

int SymbolEditor::terminalAt(const QPointF &at, double reach) const
{
    return pinAtPoint(m_item, at, reach);
}

QSet<int> SymbolEditor::terminalsInside(const QRectF &rect) const
{
    return pinsInside(m_item, rect);
}

QRectF SymbolEditor::terminalsBounds(const Symbol &item, const QSet<int> &terminals) const
{
    return pinsBounds(item, terminals);
}

Point SymbolEditor::terminalPoint(int terminal) const
{
    return m_item.pins.at(terminal).at;
}

Symbol SymbolEditor::withTerminalsMoved(const Symbol &item, const QSet<int> &terminals,
                                        const Point &delta) const
{
    return withPinsMoved(item, terminals, delta);
}

Symbol SymbolEditor::withTerminalsRotated(const Symbol &item, const QSet<int> &terminals,
                                          const Point &center) const
{
    return withPinsRotated(item, terminals, center);
}

Symbol SymbolEditor::withoutTerminals(const Symbol &item, const QSet<int> &terminals) const
{
    return withoutPins(item, terminals);
}

std::optional<Point> SymbolEditor::terminalEnd(int terminal) const
{
    return pinBodyEnd(m_item.pins.at(terminal));
}

Symbol SymbolEditor::withTerminalEnd(const Symbol &item, int terminal, const Point &to) const
{
    Symbol result = item;
    result.pins[terminal] = withBodyEndAt(item.pins.at(terminal), to);
    return result;
}

void SymbolEditor::placeAt(const QPointF &at)
{
    if (m_mode != Mode::Place)
        return;
    Pin pin = m_nextPin;
    pin.at = terminalSnapped(at);
    pin.number = nextPinNumber(m_item);
    ItemChoice placed;
    placed.terminals = {int(m_item.pins.size())};
    change(tr("Place pin"), withPinsAdded(m_item, {pin}), placed);
}

void SymbolEditor::rotatePlacing()
{
    m_nextPin.side = sideTurned(m_nextPin.side);
    emit changed();
}

void SymbolEditor::editTerminal(int pin)
{
    // The edit is tied to this pin as the symbol stands now: a step made
    // meanwhile closes it first (press() does), so the index holds.
    disconnect(m_textEdit, &InPlaceEdit::accepted, this, nullptr);
    connect(m_textEdit, &InPlaceEdit::accepted, this, [this, pin](const QString &text) {
        if (pin >= m_item.pins.size())
            return;
        Pin changed = m_item.pins.at(pin);
        changed.number = text.trimmed();
        const Symbol result = withPinsReplaced(m_item, {pin}, {changed});
        const QString problem = pinsProblem(result);
        if (!problem.isEmpty()) {
            showStatus(capitalized(problem));
            return;
        }
        ItemChoice chosen;
        chosen.terminals = {pin};
        change(pinPropertiesStep(), result, chosen);
    });
    m_textEdit->open(m_item.pins.at(pin).number, pinFrame(m_item.pins.at(pin), PIN_EDIT_MM));
}

void SymbolEditor::paintShown(QPainter &painter, const Symbol &shown) const
{
    paintSymbol(painter, shown);
}

void SymbolEditor::paintChosenTerminals(QPainter &painter, const Symbol &shown) const
{
    for (const int i : m_chosen.terminals) {
        if (i < 0 || i >= shown.pins.size())
            continue;
        painter.drawRect(pinFrame(shown.pins.at(i), PIN_FRAME_MM));
        // The end that sets the length.
        paintHandle(painter, pinBodyEnd(shown.pins.at(i)));
    }
}

void SymbolEditor::paintFloating(QPainter &painter, const QList<Graphic> &graphics) const
{
    paintSymbolPins(painter, floatingPins(), m_item.showPinNumbers, m_item.showPinNames);
    paintSymbolGraphics(painter, graphics);
}

} // namespace ppcb
