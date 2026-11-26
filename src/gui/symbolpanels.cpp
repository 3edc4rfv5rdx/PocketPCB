#include "symboleditor.h"

#include "format.h"
#include "itempanels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

namespace ppcb {

// PinPanel

PinPanel::PinPanel(SymbolEditor *editor, QWidget *parent)
    : ItemPanel(parent)
    , m_editor(editor)
{
    m_heading = panelHeading(this);
    m_number = panelLineEdit(this, "pinNumber");
    m_name = panelLineEdit(this, "pinName");
    m_side = new QComboBox(this);
    m_side->setObjectName(QStringLiteral("pinSide"));
    // In the order of PinSide: the side of the symbol the wire comes from.
    m_side->addItems({tr("Left"), tr("Right"), tr("Top"), tr("Bottom")});
    m_length = panelLineEdit(this, "pinLength");
    m_x = panelLineEdit(this, "pinX");
    m_y = panelLineEdit(this, "pinY");

    QFormLayout *form = new QFormLayout;
    const QString unit = QStringLiteral(", ") + mmUnit() + QLatin1Char(':');
    form->addRow(tr("Number") + QLatin1Char(':'), m_number);
    form->addRow(tr("Name") + QLatin1Char(':'), m_name);
    form->addRow(tr("Side") + QLatin1Char(':'), m_side);
    form->addRow(tr("Length") + unit, m_length);
    form->addRow(QStringLiteral("X") + unit, m_x);
    form->addRow(QStringLiteral("Y") + unit, m_y);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addLayout(form);
    layout->addStretch();

    connect(m_number, &QLineEdit::editingFinished, this, [this] {
        if (m_shown.size() != 1)
            return;
        const QString number = m_number->text().trimmed();
        commit([&number](Pin &pin) { pin.number = number; });
    });
    connect(m_name, &QLineEdit::editingFinished, this, [this] {
        // An empty field of pins that differ sets nothing; one pin may be
        // given no name.
        const QString name = m_name->text().trimmed();
        if (name.isEmpty() && !m_name->placeholderText().isEmpty()) {
            refresh(true);
            return;
        }
        commit([&name](Pin &pin) { pin.name = name; });
    });
    connect(m_side, &QComboBox::activated, this, [this](int index) {
        if (index < 0)
            return;
        commit([index](Pin &pin) { pin.side = PinSide(index); });
    });
    for (QLineEdit *edit : {m_length, m_x, m_y})
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] { commitMm(edit); });
    refresh(true);
}

QString PinPanel::pinTitle()
{
    return tr("Pin");
}

bool PinPanel::eventFilter(QObject *watched, QEvent *event)
{
    return panelFieldEscape(watched, event, [this] { refresh(true); })
           || QWidget::eventFilter(watched, event);
}

void PinPanel::refresh(bool force)
{
    const QSet<int> &chosen = m_editor->chosen().terminals;
    if (chosen != m_shown)
        force = true;
    m_shown = chosen;
    QList<Pin> pins;
    for (const int i : inOrder(chosen))
        pins.append(m_editor->symbol().pins.at(i));
    const bool none = pins.isEmpty();
    if (none)
        pins.append(m_editor->nextPin());

    if (none)
        m_heading->setText(tr("New pins"));
    else if (pins.size() == 1)
        m_heading->setText(pinTitle() + QLatin1Char(' ') + pins.first().number);
    else
        m_heading->setText(tr("Pins chosen") + QStringLiteral(": ") + QString::number(pins.size()));

    const auto text = [](const QString &value) { return value; };
    m_number->setEnabled(pins.size() == 1 && !none);
    showSame(m_number, pins, [](const Pin &pin) { return pin.number; }, text, force);
    // Each pin placed is given a name of its own, typed later.
    m_name->setEnabled(!none);
    showSame(m_name, pins, [](const Pin &pin) { return pin.name; }, text, force);
    showSameChoice(m_side, pins, [](const Pin &pin) { return pin.side; });
    showSame(m_length, pins, [](const Pin &pin) { return pin.length; }, mmText, force);
    m_x->setEnabled(!none);
    m_y->setEnabled(!none);
    showSame(m_x, pins, [](const Pin &pin) { return pin.at.x; }, mmText, force);
    showSame(m_y, pins, [](const Pin &pin) { return pin.at.y; }, mmText, force);
    for (QLineEdit *edit : {m_number, m_name, m_x, m_y}) {
        if (none && !isTyping(edit, force))
            edit->clear();
    }
}

template <typename Apply>
void PinPanel::commit(Apply apply)
{
    const QList<int> indices = inOrder(m_shown);
    const Symbol &symbol = m_editor->symbol();
    QList<Pin> changed;
    if (indices.isEmpty())
        changed.append(m_editor->nextPin());
    for (const int i : indices)
        changed.append(symbol.pins.at(i));
    for (Pin &pin : changed)
        apply(pin);
    if (indices.isEmpty()) {
        // The pins placed next.
        m_editor->setNextPin(changed.first());
        refresh(true);
        return;
    }
    const Symbol result = withPinsReplaced(symbol, indices, changed);
    const QString problem = pinsProblem(result);
    if (!problem.isEmpty()) {
        m_editor->showStatus(capitalized(problem));
        refresh(true);
        return;
    }
    m_editor->setNextPin(changed.first());
    m_editor->change(SymbolEditor::pinPropertiesStep(), result, m_editor->chosen());
    refresh(true);
}

void PinPanel::commitMm(QLineEdit *edit)
{
    // An empty field of pins that differ sets nothing.
    Coord nm = 0;
    if (edit->text().trimmed().isEmpty() || !parseMm(edit->text(), nm)) {
        refresh(true);
        return;
    }
    commit([this, edit, nm](Pin &pin) {
        if (edit == m_length)
            pin.length = nm;
        else if (edit == m_x)
            pin.at.x = nm;
        else if (edit == m_y)
            pin.at.y = nm;
    });
}

// SymbolPanel

SymbolPanel::SymbolPanel(SymbolEditor *editor, QWidget *parent)
    : ItemPanel(parent)
    , m_editor(editor)
{
    QLabel *heading = panelHeading(this);
    heading->setText(symbolTitle());
    m_numbers = new QCheckBox(tr("Show pin numbers"), this);
    m_numbers->setObjectName(QStringLiteral("symbolPinNumbers"));
    m_names = new QCheckBox(tr("Show pin names"), this);
    m_names->setObjectName(QStringLiteral("symbolPinNames"));
    m_power = panelLineEdit(this, "symbolPower");
    m_power->setToolTip(tr("A power symbol joins every one of the same net; empty for a part"));
    m_hidePower = new QCheckBox(tr("Hide the net name"), this);
    m_hidePower->setObjectName(QStringLiteral("symbolHidePower"));

    QFormLayout *form = new QFormLayout;
    form->addRow(QString(), m_numbers);
    form->addRow(QString(), m_names);
    form->addRow(tr("Power net") + QLatin1Char(':'), m_power);
    form->addRow(QString(), m_hidePower);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addLayout(form);

    connect(m_numbers, &QCheckBox::clicked, this, [this](bool on) {
        Symbol changed = m_editor->symbol();
        changed.showPinNumbers = on;
        commit(changed);
    });
    connect(m_names, &QCheckBox::clicked, this, [this](bool on) {
        Symbol changed = m_editor->symbol();
        changed.showPinNames = on;
        commit(changed);
    });
    connect(m_power, &QLineEdit::editingFinished, this, [this] {
        Symbol changed = m_editor->symbol();
        changed.power = m_power->text().trimmed();
        // Only a power symbol has these.
        if (!changed.isPower()) {
            changed.hidePowerName = false;
            changed.name.clear();
        }
        commit(changed);
    });
    connect(m_hidePower, &QCheckBox::clicked, this, [this](bool on) {
        Symbol changed = m_editor->symbol();
        changed.hidePowerName = on;
        commit(changed);
    });
    refresh(true);
}

QString SymbolPanel::symbolTitle()
{
    return tr("Symbol");
}

bool SymbolPanel::eventFilter(QObject *watched, QEvent *event)
{
    return panelFieldEscape(watched, event, [this] { refresh(true); })
           || QWidget::eventFilter(watched, event);
}

void SymbolPanel::refresh(bool force)
{
    const Symbol &symbol = m_editor->symbol();
    m_numbers->setChecked(symbol.showPinNumbers);
    m_names->setChecked(symbol.showPinNames);
    if (!isTyping(m_power, force))
        m_power->setText(symbol.power);
    m_hidePower->setEnabled(symbol.isPower());
    m_hidePower->setChecked(symbol.hidePowerName);
}

void SymbolPanel::commit(const Symbol &changed)
{
    m_editor->change(SymbolEditor::symbolPropertiesStep(), changed, m_editor->chosen());
    refresh(true);
}

} // namespace ppcb
