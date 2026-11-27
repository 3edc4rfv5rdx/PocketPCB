#include "parteditor.h"

#include "format.h"
#include "itemchooser.h"
#include "librarypanel.h"
#include "namefield.h"
#include "partedit.h"
#include "toolicons.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QToolBar>
#include <QUndoCommand>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

// One step of the part editor: the part before and after it.
class PartStep : public QUndoCommand
{
public:
    PartStep(PartEditor *editor, const QString &text, const Part &before, const Part &after)
        : QUndoCommand(text)
        , m_editor(editor)
        , m_before(before)
        , m_after(after)
    {
    }

    void undo() override { m_editor->applyStep(m_before); }
    void redo() override { m_editor->applyStep(m_after); }

private:
    PartEditor *m_editor;
    Part m_before;
    Part m_after;
};

QPushButton *formButton(QWidget *parent, const QString &text, const char *name)
{
    QPushButton *button = new QPushButton(text, parent);
    button->setObjectName(QLatin1String(name));
    return button;
}

QLineEdit *formLineEdit(QWidget *parent, const char *name)
{
    QLineEdit *edit = new QLineEdit(parent);
    edit->setObjectName(QLatin1String(name));
    return edit;
}

// The item of 'box' holding 'data', added at its end with 'text' when
// there is none.
int itemFor(QComboBox *box, const QVariant &data, const QString &text)
{
    const int found = box->findData(data);
    if (found >= 0)
        return found;
    box->addItem(text, data);
    return box->count() - 1;
}

} // namespace

PartEditor::PartEditor(const QString &name, const Part &part, bool inUserLibrary,
                       const Library *library, QWidget *parent)
    : LibraryTab(name, inUserLibrary, parent)
    , m_part(part)
    , m_library(library)
{
    QToolBar *bar = new QToolBar(this);
    bar->setObjectName(QStringLiteral("partBar"));
    bar->setMovable(false);
    styleToolBar(bar);
    addFileTools(bar);

    QWidget *form = new QWidget(this);
    m_symbol = formButton(form, QString(), "partSymbol");
    connect(m_symbol, &QPushButton::clicked, this, &PartEditor::askSymbol);
    m_footprints = new QListWidget(form);
    m_footprints->setObjectName(QStringLiteral("partFootprints"));
    connect(m_footprints, &QListWidget::currentRowChanged, this, [this] { showDrawings(); });
    QPushButton *add = formButton(form, tr("Add") + QStringLiteral("…"), "partAddFootprint");
    connect(add, &QPushButton::clicked, this, &PartEditor::askFootprint);
    m_remove = formButton(form, tr("Remove"), "partRemoveFootprint");
    connect(m_remove, &QPushButton::clicked, this, &PartEditor::removeChosenFootprint);
    m_up = formButton(form, QStringLiteral("↑"), "partFootprintUp");
    m_up->setToolTip(tr("Up"));
    connect(m_up, &QPushButton::clicked, this, [this] { moveChosenFootprint(-1); });
    m_down = formButton(form, QStringLiteral("↓"), "partFootprintDown");
    m_down->setToolTip(tr("Down"));
    connect(m_down, &QPushButton::clicked, this, [this] { moveChosenFootprint(1); });
    QHBoxLayout *listButtons = new QHBoxLayout;
    listButtons->addWidget(add);
    listButtons->addWidget(m_remove);
    listButtons->addWidget(m_up);
    listButtons->addWidget(m_down);
    listButtons->addStretch();
    QLabel *defaultNote = new QLabel(tr("The first is the default"), form);

    m_prefix = formLineEdit(form, "partPrefix");
    m_prefixGost = formLineEdit(form, "partPrefixGost");
    m_value = formLineEdit(form, "partValue");
    m_group = new QComboBox(form);
    m_group->setObjectName(QStringLiteral("partGroup"));
    for (const QString &key : partGroupKeys())
        m_group->addItem(partGroupName(key), key);
    m_group->addItem(partGroupName(QString()), QString());
    m_power = new QComboBox(form);
    m_power->setObjectName(QStringLiteral("partPower"));
    m_power->addItem(tr("None"), 0);
    for (const int power : standardPowersMw())
        m_power->addItem(formatPower(power), power);
    m_connector = new QCheckBox(tr("Connector"), form);
    m_connector->setObjectName(QStringLiteral("partConnector"));
    m_connector->setToolTip(tr("It carries nets off the board"));
    m_noValue = new QCheckBox(tr("Needs no value"), form);
    m_noValue->setObjectName(QStringLiteral("partNoValue"));

    QFormLayout *fields = new QFormLayout(form);
    fields->addRow(tr("Symbol") + QLatin1Char(':'), m_symbol);
    fields->addRow(ItemChooser::kindTitle(LibraryKind::Footprint) + QLatin1Char(':'), m_footprints);
    fields->addRow(QString(), listButtons);
    fields->addRow(QString(), defaultNote);
    fields->addRow(tr("Designator prefix") + QLatin1Char(':'), m_prefix);
    fields->addRow(tr("Designator prefix, GOST") + QLatin1Char(':'), m_prefixGost);
    fields->addRow(tr("Value") + QLatin1Char(':'), m_value);
    fields->addRow(tr("Group") + QLatin1Char(':'), m_group);
    fields->addRow(tr("Rated power") + QLatin1Char(':'), m_power);
    fields->addRow(QString(), m_connector);
    fields->addRow(QString(), m_noValue);
    m_name = new NameField("partName", form);
    fields->addRow(NameField::title() + QLatin1Char(':'), m_name);
    for (QLineEdit *edit : {m_prefix, m_prefixGost, m_value})
        connect(edit, &QLineEdit::editingFinished, this, &PartEditor::takeFields);
    connect(m_name, &NameField::edited, this, &PartEditor::takeFields);
    connect(m_group, &QComboBox::activated, this, [this](int index) {
        Part changed = m_part;
        changed.group = m_group->itemData(index).toString();
        change(changed);
    });
    connect(m_power, &QComboBox::activated, this, [this](int index) {
        Part changed = m_part;
        changed.powerMw = m_power->itemData(index).toInt();
        change(changed);
    });
    connect(m_connector, &QCheckBox::toggled, this, [this](bool on) {
        Part changed = m_part;
        changed.connector = on;
        change(changed);
    });
    connect(m_noValue, &QCheckBox::toggled, this, [this](bool on) {
        Part changed = m_part;
        changed.noValue = on;
        change(changed);
    });

    m_preview = new PartPreview(this);
    m_preview->setObjectName(QStringLiteral("partPreview"));
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(form);
    splitter->addWidget(m_preview);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setChildrenCollapsible(false);
    m_check = new QLabel(this);
    m_check->setObjectName(QStringLiteral("partCheck"));
    m_check->setWordWrap(true);
    m_check->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(bar);
    layout->addWidget(splitter, 1);
    layout->addWidget(m_check);
    refresh();
    m_footprints->setCurrentRow(m_part.footprints.isEmpty() ? -1 : 0);
}

QString PartEditor::newPartTitle()
{
    return tr("New part");
}

bool PartEditor::isInUserLibrary(const QString &name)
{
    return userLibraryHas(LIBRARY_PARTS_FOLDER, name);
}

QString PartEditor::editorTitle() const
{
    return tr("Part editor");
}

void PartEditor::takeFocus()
{
    m_symbol->setFocus();
}

void PartEditor::change(const Part &changed)
{
    if (changed == m_part) {
        // A field typed back to what it was, or a box set to the same:
        // shown as the part has it.
        refresh();
        return;
    }
    m_undoStack.push(new PartStep(this, tr("Part properties"), m_part, changed));
}

void PartEditor::applyStep(const Part &part)
{
    m_part = part;
    refresh();
}

void PartEditor::setLibrary(const Library *library)
{
    m_library = library;
    refresh();
}

int PartEditor::chosenFootprint() const
{
    return m_footprints->currentRow();
}

void PartEditor::chooseFootprint(int index)
{
    m_footprints->setCurrentRow(index);
}

void PartEditor::askSymbol()
{
    if (m_library == nullptr)
        return;
    LibraryKind kind = LibraryKind::Symbol;
    const QString name =
        ItemChooser::choose(this, tr("Symbol"), *m_library, {LibraryKind::Symbol}, kind,
                            QDialogButtonBox::Ok, m_part.symbol);
    if (name.isEmpty())
        return;
    Part changed = m_part;
    changed.symbol = name;
    change(changed);
}

void PartEditor::askFootprint()
{
    if (m_library == nullptr)
        return;
    LibraryKind kind = LibraryKind::Footprint;
    const QString current = chosenFootprint() >= 0 ? m_part.footprints.value(chosenFootprint())
                                                   : QString();
    const QString name =
        ItemChooser::choose(this, tr("Add footprint"), *m_library, {LibraryKind::Footprint}, kind,
                            QDialogButtonBox::Ok, current);
    if (name.isEmpty())
        return;
    // One listed already is only chosen.
    if (!m_part.footprints.contains(name)) {
        Part changed = m_part;
        changed.footprints.append(name);
        change(changed);
    }
    chooseFootprint(int(m_part.footprints.indexOf(name)));
}

void PartEditor::removeChosenFootprint()
{
    const int index = chosenFootprint();
    if (index < 0 || index >= m_part.footprints.size())
        return;
    Part changed = m_part;
    changed.footprints.removeAt(index);
    change(changed);
    chooseFootprint(std::min(index, int(m_part.footprints.size()) - 1));
}

void PartEditor::moveChosenFootprint(int by)
{
    const int index = chosenFootprint();
    const Part changed = withFootprintMoved(m_part, index, index + by);
    if (changed == m_part)
        return;
    change(changed);
    chooseFootprint(index + by);
}

QString PartEditor::checkText() const
{
    // What keeps it from being saved, then what does not match.
    QStringList problems;
    const QString problem = partProblem(m_part);
    if (!problem.isEmpty())
        problems.append(problem);
    if (m_library != nullptr)
        problems.append(partMismatches(m_part, *m_library));
    if (!problems.isEmpty())
        return capitalized(problems.join(QStringLiteral("; ")));
    if (m_library == nullptr)
        return QString();
    if (m_part.footprints.isEmpty())
        return tr("The part has no footprints and is never on the board");
    return tr("Every pin has a pad in every footprint");
}

void PartEditor::refresh()
{
    const QSignalBlocker blockList(m_footprints);
    const QSignalBlocker blockConnector(m_connector);
    const QSignalBlocker blockNoValue(m_noValue);
    m_symbol->setText(m_part.symbol.isEmpty() ? tr("Choose") + QStringLiteral("…")
                                              : m_part.symbol);
    const int row = m_footprints->currentRow();
    m_footprints->clear();
    m_footprints->addItems(m_part.footprints);
    if (m_footprints->count() > 0) {
        QListWidgetItem *first = m_footprints->item(0);
        QFont bold = first->font();
        bold.setBold(true);
        first->setFont(bold);
        first->setToolTip(tr("The default footprint"));
    }
    m_footprints->setCurrentRow(std::min(row, m_footprints->count() - 1));
    m_prefix->setText(m_part.prefix);
    m_prefixGost->setText(m_part.prefixGost);
    m_value->setText(m_part.value);
    m_group->setCurrentIndex(itemFor(m_group, m_part.group, m_part.group));
    m_power->setCurrentIndex(itemFor(m_power, m_part.powerMw, formatPower(m_part.powerMw)));
    m_connector->setChecked(m_part.connector);
    m_noValue->setChecked(m_part.noValue);
    m_name->setNames(m_part.name);
    m_check->setText(checkText());
    showDrawings();
}

void PartEditor::showDrawings()
{
    const int index = chosenFootprint();
    m_remove->setEnabled(index >= 0);
    m_up->setEnabled(index > 0);
    m_down->setEnabled(index >= 0 && index + 1 < m_footprints->count());
    const Symbol *symbol = m_library != nullptr ? m_library->findSymbol(m_part.symbol) : nullptr;
    const Footprint *footprint = m_library != nullptr && index >= 0
                                     ? m_library->findFootprint(m_part.footprints.value(index))
                                     : nullptr;
    m_symbolShown = symbol != nullptr;
    m_shownSymbol = m_symbolShown ? *symbol : Symbol();
    m_footprintShown = footprint != nullptr;
    m_shownFootprint = m_footprintShown ? *footprint : Footprint();
    m_preview->setContent(m_symbolShown ? &m_shownSymbol : nullptr,
                          m_footprintShown ? &m_shownFootprint : nullptr);
}

void PartEditor::takeFields()
{
    Part changed = m_part;
    changed.prefix = m_prefix->text().trimmed();
    changed.prefixGost = m_prefixGost->text().trimmed();
    changed.value = m_value->text().trimmed();
    changed.name = m_name->taken(changed.name);
    change(changed);
}

const char *PartEditor::libraryFolder() const
{
    return LIBRARY_PARTS_FOLDER;
}

LibraryItem PartEditor::libraryItem() const
{
    LibraryItem item;
    item.kind = LibraryKind::Part;
    item.part = withBothPrefixes(m_part);
    return item;
}

QString PartEditor::saveProblem() const
{
    return partProblem(m_part);
}

QString PartEditor::saveWarning() const
{
    return m_library != nullptr ? partMismatches(m_part, *m_library).join(QStringLiteral("; "))
                                : QString();
}

QStringList PartEditor::partsUsing(const Library &library, const QString &name) const
{
    Q_UNUSED(library);
    Q_UNUSED(name);
    return {};
}

Part PartEditor::partRenamed(const Part &part, const QString &from, const QString &to) const
{
    Q_UNUSED(from);
    Q_UNUSED(to);
    return part;
}

QString PartEditor::nameLabel() const
{
    return tr("Part name");
}

QString PartEditor::takenQuestion(const QString &name) const
{
    return tr("The library has a part %1 already. Replace it").arg(name);
}

QString PartEditor::strandedQuestion(const QString &name, const QStringList &parts) const
{
    // No part lists a part: never asked.
    Q_UNUSED(name);
    Q_UNUSED(parts);
    return QString();
}

QString PartEditor::unsavedQuestion() const
{
    return tr("The part has changes that are not saved. Save them");
}

QString PartEditor::renamedMessage(const QString &from, const QString &to) const
{
    return tr("Part %1 renamed to %2").arg(from, to);
}

} // namespace ppcb
