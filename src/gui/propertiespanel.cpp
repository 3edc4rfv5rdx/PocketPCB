#include "propertiespanel.h"

#include "document.h"
#include "format.h"
#include "sheetedit.h"
#include "sheeteditor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

// How many designators the heading lists before it stops.
const int NAMED_COMPONENTS = 5;
// A designator another component has.
const char TAKEN_STYLE[] = "QLineEdit { color: #c00000; }";

} // namespace

PropertiesPanel::PropertiesPanel(Document *document, SheetEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_editor(editor)
{
    m_heading = new QLabel(this);
    m_heading->setWordWrap(true);
    m_designator = new QLineEdit(this);
    m_designator->setObjectName(QStringLiteral("propertiesDesignator"));
    m_value = new QLineEdit(this);
    m_value->setObjectName(QStringLiteral("propertiesValue"));
    m_footprint = new QComboBox(this);
    m_footprint->setObjectName(QStringLiteral("propertiesFootprint"));
    m_offBoard = new QCheckBox(this);
    m_offBoard->setObjectName(QStringLiteral("propertiesOffBoard"));
    m_locked = new QCheckBox(this);
    m_locked->setObjectName(QStringLiteral("propertiesLocked"));
    m_power = new QComboBox(this);
    m_power->setObjectName(QStringLiteral("propertiesPower"));
    m_comment = new QLineEdit(this);
    m_comment->setObjectName(QStringLiteral("propertiesComment"));
    m_unit = new QComboBox(this);
    m_unit->setObjectName(QStringLiteral("propertiesUnit"));

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("Designator"), m_designator);
    form->addRow(tr("Unit"), m_unit);
    form->addRow(valueTitle(), m_value);
    form->addRow(footprintTitle(), m_footprint);
    form->addRow(tr("Off board"), m_offBoard);
    form->addRow(tr("Locked"), m_locked);
    form->addRow(powerTitle(), m_power);
    form->addRow(commentTitle(), m_comment);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addLayout(form);
    layout->addStretch();

    m_designator->installEventFilter(this);
    m_value->installEventFilter(this);
    m_comment->installEventFilter(this);
    // Only what the user does: filling the fields in refresh() sends none of these.
    connect(m_designator, &QLineEdit::editingFinished, this, &PropertiesPanel::commitDesignator);
    connect(m_designator, &QLineEdit::textEdited, this, &PropertiesPanel::markDesignator);
    connect(m_value, &QLineEdit::editingFinished, this, &PropertiesPanel::commitValue);
    connect(m_footprint, &QComboBox::activated, this, &PropertiesPanel::commitFootprint);
    connect(m_offBoard, &QCheckBox::clicked, this, &PropertiesPanel::commitOffBoard);
    connect(m_locked, &QCheckBox::clicked, this, &PropertiesPanel::commitLocked);
    connect(m_power, &QComboBox::activated, this, &PropertiesPanel::commitPower);
    connect(m_unit, &QComboBox::activated, this, &PropertiesPanel::commitUnit);
    connect(m_comment, &QLineEdit::editingFinished, this, &PropertiesPanel::commitComment);
    connect(m_editor, &SheetEditor::changed, this, [this] { refresh(); });
    connect(m_document, &Document::designChanged, this, [this] { refresh(); });
    refresh(true);
}

void PropertiesPanel::focusFirstField()
{
    QLineEdit *field = m_designator->isEnabled() ? m_designator : m_value;
    if (!field->isEnabled())
        return;
    field->setFocus(Qt::OtherFocusReason);
    field->selectAll();
}

QString PropertiesPanel::propertiesTitle()
{
    return tr("Properties");
}

QString PropertiesPanel::footprintTitle()
{
    return tr("Footprint");
}

QString PropertiesPanel::valueTitle()
{
    return tr("Value");
}

QString PropertiesPanel::commentTitle()
{
    return tr("Comment");
}

QString PropertiesPanel::powerTitle()
{
    return tr("Power");
}

QList<QPair<QString, int>> PropertiesPanel::powerChoices()
{
    QList<QPair<QString, int>> choices = {qMakePair(tr("none"), 0)};
    for (const int powerMw : standardPowersMw())
        choices.append(qMakePair(formatPower(powerMw), powerMw));
    return choices;
}

bool PropertiesPanel::havePower(const QList<Component> &components, const Library &library)
{
    return !components.isEmpty()
           && std::all_of(components.cbegin(), components.cend(), [&library](const Component &c) {
                  const Part *part = library.findPart(c.part);
                  return part != nullptr && part->powerMw > 0;
              });
}

QString PropertiesPanel::differentMark()
{
    return QLatin1Char('(') + tr("different") + QLatin1Char(')');
}

bool PropertiesPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_designator && watched != m_value && watched != m_comment)
        return QWidget::eventFilter(watched, event);
    const bool escape = (event->type() == QEvent::ShortcutOverride
                         || event->type() == QEvent::KeyPress)
                        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape;
    if (!escape)
        return QWidget::eventFilter(watched, event);
    // Esc is the field's, not the window's, which would drop the selection.
    event->accept();
    if (event->type() == QEvent::KeyPress) {
        // The fields show the components again: leaving them sets nothing.
        refresh(true);
        static_cast<QWidget *>(watched)->clearFocus();
    }
    return true;
}

bool PropertiesPanel::isTyping(const QWidget *field, bool force) const
{
    return !force && field->hasFocus();
}

void PropertiesPanel::refresh(bool force)
{
    const QList<Component> chosen =
        m_editor->sheet() >= 0 ? m_editor->chosenComponents() : QList<Component>();
    QSet<ObjectId> ids;
    for (const Component &component : chosen)
        ids.insert(component.id);
    // Other components: what is typed belongs to the ones before.
    force = force || ids != m_ids;
    m_ids = ids;
    const QString different = differentMark();

    QStringList names;
    for (const Component &component : chosen)
        names.append(component.designator);
    std::sort(names.begin(), names.end());
    if (names.size() > NAMED_COMPONENTS) {
        names = names.mid(0, NAMED_COMPONENTS);
        names.append(QStringLiteral("…"));
    }
    m_heading->setText(chosen.isEmpty() ? tr("No component chosen")
                                        : names.join(QStringLiteral(", ")));

    // The designator: of one component only.
    m_designator->setEnabled(chosen.size() == 1);
    if (!isTyping(m_designator, force)) {
        m_designator->setText(chosen.size() == 1 ? chosen.first().designator : QString());
        markDesignator();
    }

    // The unit: of one component of a part of several.
    const Design &design = m_document->design();
    const Part *unitPart =
        chosen.size() == 1 ? design.library.findPart(chosen.first().part) : nullptr;
    m_unit->clear();
    m_unit->setEnabled(unitPart != nullptr && unitPart->unitCount() > 1);
    if (m_unit->isEnabled()) {
        for (int unit = 0; unit < unitPart->unitCount(); ++unit)
            m_unit->addItem(chosen.first().designator
                                + unitSuffix(unit, design.project.designatorStyle),
                            unit);
        m_unit->setCurrentIndex(m_unit->findData(chosen.first().unit));
    }

    // The value: the one they all have, or none.
    m_valuesDiffer = false;
    for (const Component &component : chosen)
        m_valuesDiffer = m_valuesDiffer || component.value != chosen.first().value;
    m_value->setEnabled(!chosen.isEmpty());
    m_value->setPlaceholderText(m_valuesDiffer ? different : QString());
    if (!isTyping(m_value, force))
        m_value->setText(chosen.isEmpty() || m_valuesDiffer ? QString() : chosen.first().value);

    // The footprint: one of those the part allows, when all are of one part.
    const Part *part =
        chosen.isEmpty() ? nullptr : m_document->design().library.findPart(chosen.first().part);
    bool footprintsDiffer = false;
    for (const Component &component : chosen) {
        if (component.part != chosen.first().part)
            part = nullptr;
        footprintsDiffer = footprintsDiffer || component.footprint != chosen.first().footprint;
    }
    m_footprint->clear();
    m_footprint->setEnabled(part != nullptr && !part->footprints.isEmpty());
    if (m_footprint->isEnabled()) {
        m_footprint->addItems(part->footprints);
        const QString current = footprintsDiffer ? QString() : chosen.first().footprint;
        // One the part no longer allows is still told, though not offered.
        m_footprint->setPlaceholderText(footprintsDiffer ? different : current);
        m_footprint->setCurrentIndex(m_footprint->findText(current));
    } else {
        m_footprint->setPlaceholderText(QString());
    }

    // Off the board: checked, not, or partly when they differ.
    int offBoard = 0;
    for (const Component &component : chosen)
        offBoard += component.offBoard ? 1 : 0;
    m_offBoard->setEnabled(!chosen.isEmpty());
    const bool mixed = offBoard > 0 && offBoard < chosen.size();
    // Partly only to show: a click turns it on or off for all.
    m_offBoard->setTristate(mixed);
    m_offBoard->setCheckState(mixed                       ? Qt::PartiallyChecked
                              : offBoard > 0              ? Qt::Checked
                                                          : Qt::Unchecked);

    // Locked: the same way.
    int locked = 0;
    for (const Component &component : chosen)
        locked += component.locked ? 1 : 0;
    m_locked->setEnabled(!chosen.isEmpty());
    const bool lockMixed = locked > 0 && locked < chosen.size();
    m_locked->setTristate(lockMixed);
    m_locked->setCheckState(lockMixed ? Qt::PartiallyChecked
                            : locked > 0 ? Qt::Checked
                                         : Qt::Unchecked);

    // The rated power: of resistors only; the one they all have, or none.
    m_power->clear();
    m_power->setEnabled(havePower(chosen, m_document->design().library));
    if (m_power->isEnabled()) {
        for (const auto &[text, powerMw] : powerChoices())
            m_power->addItem(text, powerMw);
        bool powersDiffer = false;
        for (const Component &component : chosen)
            powersDiffer = powersDiffer || component.powerMw != chosen.first().powerMw;
        const int current = chosen.first().powerMw;
        // One not offered is still told.
        m_power->setPlaceholderText(powersDiffer ? different : formatPower(current));
        m_power->setCurrentIndex(powersDiffer ? -1 : m_power->findData(current));
    } else {
        m_power->setPlaceholderText(QString());
    }

    // The comment: the one they all have, or none.
    m_commentsDiffer = false;
    for (const Component &component : chosen)
        m_commentsDiffer = m_commentsDiffer || component.comment != chosen.first().comment;
    m_comment->setEnabled(!chosen.isEmpty());
    m_comment->setPlaceholderText(m_commentsDiffer ? different : QString());
    if (!isTyping(m_comment, force))
        m_comment->setText(chosen.isEmpty() || m_commentsDiffer ? QString()
                                                                : chosen.first().comment);
}

void PropertiesPanel::commitDesignator()
{
    if (m_ids.size() != 1)
        return;
    m_editor->setComponentDesignator(*m_ids.constBegin(), m_designator->text());
    refresh(true);
}

void PropertiesPanel::commitValue()
{
    // Left empty over values that differ: they stay as they are.
    if (m_ids.isEmpty() || (m_valuesDiffer && m_value->text().isEmpty()))
        return;
    m_editor->setComponentValue(m_ids, m_value->text());
    refresh(true);
}

void PropertiesPanel::commitFootprint(int index)
{
    if (m_ids.isEmpty() || index < 0)
        return;
    m_editor->setComponentFootprint(m_ids, m_footprint->itemText(index));
    refresh(true);
}

void PropertiesPanel::commitOffBoard()
{
    if (m_ids.isEmpty())
        return;
    m_editor->setComponentOffBoard(m_ids, m_offBoard->checkState() == Qt::Checked);
    refresh(true);
}

void PropertiesPanel::commitLocked()
{
    if (m_ids.isEmpty())
        return;
    m_editor->setComponentsLocked(m_ids, m_locked->checkState() == Qt::Checked);
    refresh(true);
}

void PropertiesPanel::commitComment()
{
    // Left empty over comments that differ: they stay as they are.
    if (m_ids.isEmpty() || (m_commentsDiffer && m_comment->text().isEmpty()))
        return;
    m_editor->setComponentComment(m_ids, m_comment->text());
    refresh(true);
}

void PropertiesPanel::commitPower(int index)
{
    if (m_ids.isEmpty() || index < 0)
        return;
    m_editor->setComponentPower(m_ids, m_power->itemData(index).toInt());
    refresh(true);
}

void PropertiesPanel::commitUnit(int index)
{
    if (m_ids.size() != 1 || index < 0)
        return;
    m_editor->setComponentUnit(*m_ids.constBegin(), m_unit->itemData(index).toInt());
    refresh(true);
}

void PropertiesPanel::markDesignator()
{
    const bool taken =
        m_ids.size() == 1
        && isDesignatorTaken(m_document->design().schematic, m_document->design().library,
                             m_designator->text().trimmed(), *m_ids.constBegin());
    m_designator->setStyleSheet(taken ? QLatin1String(TAKEN_STYLE) : QString());
    m_designator->setToolTip(taken ? tr("Another component has this designator") : QString());
}

} // namespace ppcb
