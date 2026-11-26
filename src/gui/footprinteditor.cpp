#include "footprinteditor.h"

#include "canvasview.h"
#include "format.h"
#include "inplaceedit.h"
#include "itempanels.h"
#include "paint.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineF>
#include <QMenu>
#include <QPainter>
#include <QRadioButton>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

// The grid a footprint is drawn on at first.
const Coord FOOTPRINT_GRID_STEP = 1270000;
// The pad placed first: round, 1.8 mm across, a hole of 1 mm, through.
const Coord FIRST_PAD_SIZE = 1800000;
const Coord FIRST_PAD_HOLE = 1000000;
// The pads offered by the arrow of the Pad button: size and hole.
const QList<QPair<Coord, Coord>> &padChoices()
{
    static const QList<QPair<Coord, Coord>> choices = {
        {1600000, 800000}, {1800000, 1000000}, {2000000, 1200000},
        {2500000, 1500000}, {3000000, 1800000},
    };
    return choices;
}
// The most pads a row may have.
const int MAX_ROW_PADS = 200;
// The rings round the body and round pads too near, in screen pixels.
const double MARK_PEN_PIXELS = 2.0;

Pad firstPad()
{
    Pad pad;
    pad.sizeX = FIRST_PAD_SIZE;
    pad.sizeY = FIRST_PAD_SIZE;
    pad.hole = FIRST_PAD_HOLE;
    return pad;
}

QList<Pad> padsMovedBy(QList<Pad> pads, const Point &delta)
{
    for (Pad &pad : pads)
        pad.at = pad.at + delta;
    return pads;
}

QPen markPen(const QColor &color, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, MARK_PEN_PIXELS, style);
    pen.setCosmetic(true);
    return pen;
}

} // namespace

FootprintEditor::FootprintEditor(const QString &name, const Footprint &footprint,
                                 bool inUserLibrary, QWidget *parent)
    : ItemEditorOf<Footprint>(name, footprint, inUserLibrary, parent)
    , m_nextPad(firstPad())
{
    // The pads go on as the footprint's last one, if it has one.
    if (!m_item.pads.isEmpty())
        setNextPad(m_item.pads.last());
    build(new PadPanel(this, this), new BodyPanel(this, this), FOOTPRINT_GRID_STEP);
}

QString FootprintEditor::editorTitle() const
{
    return tr("Footprint editor");
}

QString FootprintEditor::padPropertiesStep()
{
    return tr("Pad properties");
}

QString FootprintEditor::newFootprintTitle()
{
    return tr("New footprint");
}

bool FootprintEditor::isInUserLibrary(const QString &name)
{
    return userLibraryHas(LIBRARY_FOOTPRINTS_FOLDER, name);
}

void FootprintEditor::addPlacingTools(QToolBar *bar)
{
    const QColor color = bar->palette().color(QPalette::WindowText);
    QAction *pad = addTool(bar, PadPanel::padTitle(), "actionPadTool", QKeySequence(Qt::Key_P),
                           std::nullopt, [this] { setMode(Mode::Place); });
    pad->setIcon(toolIcon(ToolIcon::Pad, color));
    m_modeActions.insert(int(Mode::Place), pad);
    // The arrow offers pads of the usual sizes: the one chosen is placed.
    QMenu *padMenu = new QMenu(this);
    for (const auto &[size, hole] : padChoices()) {
        QAction *choice = padMenu->addAction(formatMmShort(size) + QStringLiteral(" / ")
                                             + formatMmShortUnit(hole));
        connect(choice, &QAction::triggered, this, [this, size = size, hole = hole] {
            Pad next = m_nextPad;
            next.sizeX = size;
            next.sizeY = size;
            next.hole = hole;
            next.layers = PadLayers::Both;
            setNextPad(next);
            setMode(Mode::Place);
        });
    }
    addMenuButton(bar, pad, padMenu, "padButton");
    QAction *row = addTool(bar, tr("Row of pads"), "actionPadRow", QKeySequence(),
                           ToolIcon::PadRow, [this] { askRow(); });
    m_modeActions.insert(int(Mode::Row), row);
}

QRectF FootprintEditor::shownBounds() const
{
    return footprintBounds(shownItem());
}

void FootprintEditor::setClearance(Coord clearance)
{
    if (clearance == m_clearance)
        return;
    m_clearance = clearance;
    checkGaps();
    emit changed();
}

void FootprintEditor::checkGaps()
{
    const QList<PadGap> gaps =
        m_clearance > 0 ? padsTooNear(m_item, m_clearance) : QList<PadGap>();
    const bool news = gaps != m_gaps;
    m_gaps = gaps;
    if (!news || gaps.isEmpty())
        return;
    const PadGap &first = gaps.first();
    const QString number = m_item.pads.at(first.first).number;
    const QString other = m_item.pads.at(first.second).number;
    showStatus(tr("Pads %1 and %2 are %3 apart, less than the clearance %4")
                   .arg(number, other, formatMmShortUnit(first.gap),
                        formatMmShortUnit(m_clearance)));
}

void FootprintEditor::setNextPad(const Pad &pad)
{
    m_nextPad = pad;
    m_nextPad.number.clear();
    m_nextPad.at = Point{};
    emit changed();
}

QList<Pad> FootprintEditor::floatingPads() const
{
    if (!m_cursorKnown)
        return {};
    if (m_mode == Mode::Place) {
        Pad pad = m_nextPad;
        pad.at = m_cursor;
        pad.number = nextPadNumber(m_item);
        return {pad};
    }
    if (m_mode == Mode::Row)
        return padsMovedBy(m_rowPads, m_cursor);
    return {};
}

void FootprintEditor::describeChoice()
{
    if (m_chosen.terminals.size() != 2 || !m_chosen.graphics.isEmpty())
        return;
    const QList<int> two = inOrder(m_chosen.terminals);
    const Pad &a = m_item.pads.at(two.first());
    const Pad &b = m_item.pads.at(two.last());
    const Coord centres = mmToNm(QLineF(toMm(a.at), toMm(b.at)).length());
    showStatus(tr("Pads %1 and %2: %3 between the copper, %4 between the centres")
                   .arg(a.number, b.number, formatMmShortUnit(padsApart(a, b)),
                        formatMmShortUnit(centres)));
}

int FootprintEditor::terminalAt(const QPointF &at, double reach) const
{
    return padAtPoint(m_item, at, reach);
}

QSet<int> FootprintEditor::terminalsInside(const QRectF &rect) const
{
    return padsInside(m_item, rect);
}

QRectF FootprintEditor::terminalsBounds(const Footprint &item, const QSet<int> &terminals) const
{
    return padsBounds(item, terminals);
}

Point FootprintEditor::terminalPoint(int terminal) const
{
    return m_item.pads.at(terminal).at;
}

Footprint FootprintEditor::withTerminalsMoved(const Footprint &item, const QSet<int> &terminals,
                                              const Point &delta) const
{
    return withPadsMoved(item, terminals, delta);
}

Footprint FootprintEditor::withTerminalsRotated(const Footprint &item, const QSet<int> &terminals,
                                                const Point &center) const
{
    return withPadsRotated(item, terminals, center);
}

Footprint FootprintEditor::withoutTerminals(const Footprint &item, const QSet<int> &terminals) const
{
    return withoutPads(item, terminals);
}

void FootprintEditor::placeAt(const QPointF &at)
{
    Q_UNUSED(at);
    if (m_mode == Mode::Place) {
        Pad pad = m_nextPad;
        pad.at = m_cursor;
        pad.number = nextPadNumber(m_item);
        ItemChoice placed;
        placed.terminals = {int(m_item.pads.size())};
        change(tr("Place pad"), withPadsAdded(m_item, {pad}), placed);
        return;
    }
    const int first = int(m_item.pads.size());
    ItemChoice placed;
    for (int k = 0; k < m_rowPads.size(); ++k)
        placed.terminals.insert(first + k);
    const Footprint changed = withPadsAdded(m_item, padsMovedBy(m_rowPads, m_cursor));
    m_mode = Mode::Select;
    updateModeButtons();
    change(tr("Place row of pads"), changed, placed);
}

void FootprintEditor::rotatePlacing()
{
    if (m_mode == Mode::Place) {
        setNextPad(padsRotated({m_nextPad}, Point{}).first());
        return;
    }
    m_rowPads = padsRotated(m_rowPads, Point{});
    emit changed();
}

void FootprintEditor::editTerminal(int pad)
{
    // The edit is tied to this pad as the footprint stands now: a step made
    // meanwhile closes it first (press() does), so the index holds.
    disconnect(m_textEdit, &InPlaceEdit::accepted, this, nullptr);
    connect(m_textEdit, &InPlaceEdit::accepted, this, [this, pad](const QString &text) {
        if (pad >= m_item.pads.size())
            return;
        Pad changed = m_item.pads.at(pad);
        changed.number = text.trimmed();
        ItemChoice chosen;
        chosen.terminals = {pad};
        change(padPropertiesStep(), withPadsReplaced(m_item, {pad}, {changed}), chosen);
    });
    m_textEdit->open(m_item.pads.at(pad).number, padRect(m_item.pads.at(pad)));
}

void FootprintEditor::paintShown(QPainter &painter, const Footprint &shown) const
{
    paintFootprint(painter, shown);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    // The body seen from above, dashed in its colour.
    const QRectF plan = bodyPlan(shown);
    if (!plan.isNull()) {
        painter.setPen(markPen(QColor(shown.body->color), Qt::DashLine));
        if (shown.body->shape == BodyShape::Upright)
            painter.drawEllipse(plan);
        else
            painter.drawRect(plan);
    }
    // Pads too near each other, in red.
    painter.setPen(markPen(paintcolor::ercMark()));
    for (const PadGap &gap : m_gaps) {
        for (const int i : {gap.first, gap.second}) {
            if (i < shown.pads.size())
                painter.drawRect(padRect(shown.pads.at(i)));
        }
    }
    painter.restore();
}

void FootprintEditor::paintChosenTerminals(QPainter &painter, const Footprint &shown) const
{
    for (const int i : m_chosen.terminals) {
        if (i >= 0 && i < shown.pads.size())
            painter.drawRect(padRect(shown.pads.at(i)));
    }
}

void FootprintEditor::paintFloating(QPainter &painter, const QList<Graphic> &graphics) const
{
    paintFootprintPads(painter, floatingPads());
    paintFootprintGraphics(painter, graphics);
}

void FootprintEditor::askRow()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Row of pads"));
    QFormLayout *form = new QFormLayout;
    QRadioButton *single = new QRadioButton(tr("One column"), &dialog);
    QRadioButton *twoColumns = new QRadioButton(tr("Two columns, numbered across"), &dialog);
    QRadioButton *dip = new QRadioButton(tr("DIP, numbered around"), &dialog);
    QButtonGroup kinds(&dialog);
    kinds.addButton(single, int(PadRowKind::Single));
    kinds.addButton(twoColumns, int(PadRowKind::Double));
    kinds.addButton(dip, int(PadRowKind::Dip));
    kinds.button(int(m_row.kind))->setChecked(true);
    QVBoxLayout *kindLayout = new QVBoxLayout;
    kindLayout->addWidget(single);
    kindLayout->addWidget(twoColumns);
    kindLayout->addWidget(dip);
    form->addRow(tr("Layout") + QLatin1Char(':'), kindLayout);
    QSpinBox *count = new QSpinBox(&dialog);
    count->setRange(1, MAX_ROW_PADS);
    count->setValue(m_row.count);
    form->addRow(tr("Pads") + QLatin1Char(':'), count);
    QDoubleSpinBox *pitch = mmSizeBox(m_row.pitch, 0.1, 100, 0.01, &dialog);
    form->addRow(tr("Pitch") + QLatin1Char(':'), pitch);
    QDoubleSpinBox *spacing = mmSizeBox(m_row.spacing, 0.1, 200, 0.01, &dialog);
    form->addRow(tr("Between columns") + QLatin1Char(':'), spacing);
    QSpinBox *first = new QSpinBox(&dialog);
    first->setRange(1, 9999);
    first->setValue(m_row.firstNumber);
    form->addRow(tr("First number") + QLatin1Char(':'), first);
    QCheckBox *square = new QCheckBox(tr("First pad square"), &dialog);
    square->setChecked(m_row.firstSquare);
    form->addRow(QString(), square);
    QLabel *problem = new QLabel(&dialog);
    problem->setStyleSheet(QStringLiteral("color: #c00000;"));
    form->addRow(problem);
    const auto update = [&] {
        const bool two = kinds.checkedId() != int(PadRowKind::Single);
        spacing->setEnabled(two);
        const bool odd = two && count->value() % 2 != 0;
        problem->setText(odd ? tr("Two columns need an even number of pads") : QString());
    };
    connect(&kinds, &QButtonGroup::idClicked, &dialog, update);
    connect(count, &QSpinBox::valueChanged, &dialog, update);
    update();

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (problem->text().isEmpty())
            dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        updateModeButtons();
        return;
    }
    m_row.kind = PadRowKind(kinds.checkedId());
    m_row.count = count->value();
    m_row.pitch = mmSizeOf(pitch);
    m_row.spacing = mmSizeOf(spacing);
    m_row.firstNumber = first->value();
    m_row.firstSquare = square->isChecked();
    m_rowPads = padRow(m_row, m_nextPad);
    if (m_rowPads.isEmpty()) {
        updateModeButtons();
        return;
    }
    setMode(Mode::Row);
}

const char *FootprintEditor::libraryFolder() const
{
    return LIBRARY_FOOTPRINTS_FOLDER;
}

LibraryItem FootprintEditor::libraryItem() const
{
    LibraryItem item;
    item.kind = LibraryKind::Footprint;
    item.footprint = m_item;
    return item;
}

QString FootprintEditor::saveProblem() const
{
    for (const Pad &pad : m_item.pads) {
        const QString problem = padProblem(pad);
        if (!problem.isEmpty())
            return problem;
    }
    return QString();
}

QStringList FootprintEditor::partsUsing(const Library &library, const QString &name) const
{
    return partsUsingFootprint(library, name);
}

Part FootprintEditor::partRenamed(const Part &part, const QString &from, const QString &to) const
{
    return withFootprintRenamed(part, from, to);
}

QString FootprintEditor::nameLabel() const
{
    return tr("Footprint name");
}

QString FootprintEditor::takenQuestion(const QString &name) const
{
    return tr("The library has a footprint %1 already. Replace it").arg(name);
}

QString FootprintEditor::strandedQuestion(const QString &name, const QStringList &parts) const
{
    return tr("Built-in parts list the footprint %1 and keep that name: %2. Rename it anyway")
        .arg(name, parts.join(QStringLiteral(", ")));
}

QString FootprintEditor::unsavedQuestion() const
{
    return tr("The footprint has changes that are not saved. Save them");
}

QString FootprintEditor::renamedMessage(const QString &from, const QString &to) const
{
    return tr("Footprint %1 renamed to %2").arg(from, to);
}

} // namespace ppcb
