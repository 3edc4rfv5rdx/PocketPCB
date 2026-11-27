#include "symboleditor.h"

#include "format.h"
#include "inplaceedit.h"
#include "itempanels.h"
#include "paint.h"
#include "sheeteditor.h"

#include "format.h"
#include "graphicedit.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

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

QString SymbolEditor::newSymbolFromTitle()
{
    return tr("New symbol from");
}

QString SymbolEditor::pieceTitle(SymbolPiece piece)
{
    switch (piece) {
    case SymbolPiece::Resistor:
        return tr("Resistor");
    case SymbolPiece::Capacitor:
        return tr("Capacitor");
    case SymbolPiece::ElectrolyticCapacitor:
        return tr("Electrolytic capacitor");
    case SymbolPiece::Diode:
        return tr("Diode");
    case SymbolPiece::NpnTransistor:
        return tr("NPN transistor");
    case SymbolPiece::PnpTransistor:
        return tr("PNP transistor");
    case SymbolPiece::Inductor:
        return tr("Inductor");
    case SymbolPiece::ZenerDiode:
        return tr("Zener diode");
    case SymbolPiece::SchottkyDiode:
        return tr("Schottky diode");
    case SymbolPiece::Led:
        return tr("LED");
    case SymbolPiece::Photodiode:
        return tr("Photodiode");
    case SymbolPiece::Potentiometer:
        return tr("Potentiometer");
    }
    return QString();
}

void SymbolEditor::addPieceActions(QMenu *menu,
                                   const std::function<void(const Symbol &)> &chosen,
                                   const std::function<void()> &chip)
{
    for (const SymbolPiece piece : symbolPieces()) {
        if (piece == SymbolPiece::Inductor) {
            QMenu *turns = menu->addMenu(pieceTitle(piece));
            for (const int count : inductorTurnChoices()) {
                QAction *action = turns->addAction(tr("%1 turns").arg(count));
                QObject::connect(action, &QAction::triggered, menu,
                                 [chosen, count] { chosen(inductorSymbol(count)); });
            }
            continue;
        }
        QAction *action = menu->addAction(pieceTitle(piece));
        QObject::connect(action, &QAction::triggered, menu,
                         [chosen, piece] { chosen(pieceSymbol(piece)); });
    }
    menu->addSeparator();
    QAction *chipAction = menu->addAction(tr("Chip") + QStringLiteral("…"));
    QObject::connect(chipAction, &QAction::triggered, menu, chip);
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

Point SymbolEditor::placingAt() const
{
    return terminalSnapped(m_hoverAt);
}

QList<Pin> SymbolEditor::floatingPins() const
{
    if (!m_cursorKnown)
        return {};
    if (m_mode == Mode::Row)
        return withPiece(Symbol(), m_placing, placingAt()).pins;
    if (m_mode != Mode::Place)
        return {};
    Pin pin = m_nextPin;
    pin.at = terminalSnapped(m_hoverAt);
    pin.number = nextPinNumber(m_item);
    return {pin};
}

void SymbolEditor::addPlacingTools(QToolBar *bar)
{
    QAction *pin = addTool(bar, PinPanel::pinTitle(), "actionPinTool", QKeySequence(Qt::Key_P),
                           std::nullopt, [this] { setMode(Mode::Place); });
    pin->setIcon(toolIcon(ToolIcon::Pin, bar->palette().color(QPalette::WindowText)));
    m_modeActions.insert(int(Mode::Place), pin);
    // The arrow offers the usual lengths, the next pin's marked: the one
    // chosen is placed.
    QMenu *lengths = new QMenu(this);
    for (const Coord length : pinLengthChoices()) {
        QAction *choice = lengths->addAction(formatMmShortUnit(length));
        choice->setCheckable(true);
        connect(choice, &QAction::triggered, this, [this, length] {
            Pin next = m_nextPin;
            next.length = length;
            setNextPin(next);
            setMode(Mode::Place);
        });
        connect(lengths, &QMenu::aboutToShow, choice,
                [this, choice, length] { choice->setChecked(m_nextPin.length == length); });
    }
    addMenuButton(bar, pin, lengths, "pinButton");
    // Ready-made pieces drawn to the standards, and a chip of any size.
    // Its menu chooses what: the action itself only shows the mode.
    QAction *readyMade = addTool(bar, tr("Ready-made"), "actionReadyMade", QKeySequence(),
                                 ToolIcon::ReadyMade, [] {});
    m_modeActions.insert(int(Mode::Row), readyMade);
    QMenu *pieces = new QMenu(this);
    addPieceActions(
        pieces, [this](const Symbol &piece) { placePiece(piece); },
        [this] { askChip(); });
    // The button opens the menu: a click alone has nothing to place yet.
    bar->removeAction(readyMade);
    QToolButton *button = new QToolButton(bar);
    button->setObjectName(QStringLiteral("readyMadeButton"));
    button->setDefaultAction(readyMade);
    button->setMenu(pieces);
    button->setPopupMode(QToolButton::InstantPopup);
    bar->addWidget(button);
}

void SymbolEditor::placePiece(const Symbol &piece)
{
    m_placing = piece;
    m_placingPasted = false;
    setMode(Mode::Row);
}

void SymbolEditor::clipTerminals(ItemClip &clip, const QSet<int> &terminals,
                                 const Point &delta) const
{
    for (const int i : inOrder(terminals)) {
        Pin pin = m_item.pins.at(i);
        pin.at = pin.at + delta;
        clip.pins.append(pin);
    }
}

bool SymbolEditor::startPasting(const ItemClip &clip)
{
    if (clip.pins.isEmpty() && clip.graphics.isEmpty())
        return false;
    Symbol piece;
    piece.pins = clip.pins;
    piece.graphics = clip.graphics;
    placePiece(piece);
    m_placingPasted = true;
    return true;
}

void SymbolEditor::askChip()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Chip"));
    QFormLayout *form = new QFormLayout;
    QSpinBox *left = new QSpinBox(&dialog);
    left->setObjectName(QStringLiteral("chipLeft"));
    left->setRange(0, 100);
    left->setValue(m_chipLeft);
    form->addRow(tr("Pins on the left") + QLatin1Char(':'), left);
    QSpinBox *right = new QSpinBox(&dialog);
    right->setObjectName(QStringLiteral("chipRight"));
    right->setRange(0, 100);
    right->setValue(m_chipRight);
    form->addRow(tr("Pins on the right") + QLatin1Char(':'), right);
    QDoubleSpinBox *width = mmSizeBox(m_chipWidth, nmToMm(CHIP_MIN_WIDTH), 200, 2.54, &dialog);
    form->addRow(tr("Width") + QStringLiteral(", ") + mmUnit() + QLatin1Char(':'), width);
    QLineEdit *function = new QLineEdit(m_chipFunction, &dialog);
    function->setPlaceholderText(QStringLiteral("&, 1, =1, DD"));
    form->addRow(tr("Function") + QLatin1Char(':'), function);
    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted || left->value() + right->value() == 0) {
        updateModeButtons();
        return;
    }
    m_chipLeft = left->value();
    m_chipRight = right->value();
    m_chipWidth = mmSizeOf(width);
    m_chipFunction = function->text().trimmed();
    placePiece(chipSymbol(m_chipLeft, m_chipRight, m_chipWidth, m_chipFunction));
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
    if (m_mode == Mode::Row) {
        Symbol changed = withPiece(m_item, m_placing, terminalSnapped(at));
        if (m_placingPasted) {
            changed.refAt = m_item.refAt;
            changed.valueAt = m_item.valueAt;
            changed.showPinNumbers = m_item.showPinNumbers;
            changed.showPinNames = m_item.showPinNames;
        }
        ItemChoice placed;
        for (int i = int(m_item.pins.size()); i < changed.pins.size(); ++i)
            placed.terminals.insert(i);
        for (int i = int(m_item.graphics.size()); i < changed.graphics.size(); ++i)
            placed.graphics.insert(i);
        m_mode = Mode::Select;
        updateModeButtons();
        change(m_placingPasted ? SheetEditor::actionName(SheetEditor::Action::Paste)
                               : tr("Place ready-made"),
               changed, placed);
        return;
    }
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
    if (m_mode == Mode::Row) {
        // The whole piece about its origin, a point of the pin grid.
        QSet<int> all;
        for (int i = 0; i < m_placing.pins.size(); ++i)
            all.insert(i);
        QSet<int> drawings;
        for (int i = 0; i < m_placing.graphics.size(); ++i)
            drawings.insert(i);
        m_placing = withPinsRotated(m_placing, all, Point{});
        m_placing.graphics = withGraphicsRotated(m_placing.graphics, drawings, Point{});
        m_placing.refAt = turnedQuarter(m_placing.refAt, Point{});
        m_placing.valueAt = turnedQuarter(m_placing.valueAt, Point{});
        emit changed();
        return;
    }
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
    if (m_mode == Mode::Row && m_cursorKnown)
        paintSymbolGraphics(painter, withPiece(Symbol(), m_placing, placingAt()).graphics);
}

} // namespace ppcb
