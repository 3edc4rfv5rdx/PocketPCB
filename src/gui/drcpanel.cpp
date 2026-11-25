#include "drcpanel.h"

#include "boardeditor.h"
#include "format.h"
#include "rulescheck.h"

#include <QHeaderView>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ppcb {

namespace {

// Where an item keeps the index of its problem; a group has none.
const int PROBLEM_ROLE = Qt::UserRole;

} // namespace

DrcPanel::DrcPanel(Document *document, BoardEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
{
    m_check = new RulesCheck(
        document, [this] { return isVisible() && m_editor->isActive(); },
        [this] { m_editor->findProblems(); }, this);
    QPushButton *checkButton = new QPushButton(checkButtonText(), this);
    checkButton->setObjectName(QStringLiteral("drcCheckButton"));
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("drcTree"));
    m_tree->setColumnCount(1);
    m_tree->header()->hide();
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(checkButton);
    layout->addWidget(m_tree);

    connect(checkButton, &QPushButton::clicked, this, [this] {
        m_check->check();
        emit checkRequested();
    });
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &DrcPanel::itemChosen);
    // A click on the problem chosen shows it again.
    connect(m_tree, &QTreeWidget::itemClicked, this, &DrcPanel::itemChosen);
    connect(m_editor, &BoardEditor::problemsChanged, this, &DrcPanel::refresh);
    connect(m_check, &RulesCheck::waitingChanged, this, &DrcPanel::refresh);
    // The board shown, or another page.
    connect(m_editor, &BoardEditor::changed, m_check, &RulesCheck::poke);
    refresh();
}

void DrcPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_check->poke();
}

QString DrcPanel::drcTitle()
{
    return tr("DRC");
}

QString DrcPanel::itemText(const DrcItem &item)
{
    switch (item.kind) {
    case DrcItem::Kind::Pad:
        return item.name;
    case DrcItem::Kind::Track:
        return tr("track of %1").arg(item.net);
    case DrcItem::Kind::Via:
        return tr("via of %1").arg(item.net);
    case DrcItem::Kind::LinkPad:
        return item.name.isEmpty() ? tr("wire link") : item.name;
    case DrcItem::Kind::HolePad:
        return item.net.isEmpty() ? tr("mounting hole") : tr("mounting hole of %1").arg(item.net);
    case DrcItem::Kind::Hole:
        return tr("mounting hole");
    }
    return QString();
}

QString DrcPanel::problemText(const DrcProblem &problem)
{
    const QString first = problem.items.isEmpty() ? QString() : itemText(problem.items.first());
    const QString second = problem.items.size() < 2 ? QString() : itemText(problem.items.at(1));
    // What there is against what is asked for.
    const QString measured = QStringLiteral(": ") + formatMmShortUnit(problem.actual)
                             + QStringLiteral(" < ") + formatMmShortUnit(problem.required);
    switch (problem.kind) {
    case DrcKind::Short:
        return tr("Short between %1 and %2").arg(first, second);
    case DrcKind::Clearance:
        return tr("Clearance between %1 and %2").arg(first, second) + measured;
    case DrcKind::EdgeClearance:
        return capitalized(tr("%1 too near the board edge").arg(first)) + measured;
    case DrcKind::AnnularRing:
        return tr("Annular ring of %1 too narrow").arg(first) + measured;
    case DrcKind::ClassWidth:
        return capitalized(tr("%1 narrower than its net class").arg(first)) + measured;
    case DrcKind::MinimumWidth:
        return capitalized(tr("%1 narrower than the rules allow").arg(first)) + measured;
    case DrcKind::DrillNotInSet:
        return tr("Drill %1 not in the drill set").arg(formatMmShortUnit(problem.actual))
               + QStringLiteral(": ") + tr("holes") + QLatin1Char(' ')
               + QString::number(problem.count);
    case DrcKind::OffBoard:
        return tr("%1 is off the board").arg(problem.name);
    case DrcKind::Unrouted:
        return tr("Unrouted connection of %1").arg(problem.name);
    }
    return QString();
}

QSet<ObjectId> DrcPanel::problemObjects(const DrcProblem &problem)
{
    QSet<ObjectId> objects;
    for (const DrcItem &item : problem.items)
        objects.insert(item.id);
    return objects;
}

void DrcPanel::refresh()
{
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    m_problems = m_editor->problems();
    if (m_check->waiting() || m_problems.isEmpty()) {
        QTreeWidgetItem *item = new QTreeWidgetItem(
            m_tree, {m_check->waiting() ? notCheckedMessage() : noProblemsMessage()});
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    // The errors come first.
    QTreeWidgetItem *group = nullptr;
    bool groupErrors = false;
    for (int i = 0; i < m_problems.size(); ++i) {
        const DrcProblem &problem = m_problems.at(i);
        if (group == nullptr || problem.error != groupErrors) {
            groupErrors = problem.error;
            group = new QTreeWidgetItem(m_tree, {problem.error ? tr("Errors") : tr("Warnings")});
            group->setFlags(Qt::ItemIsEnabled);
            group->setExpanded(true);
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(group, {problemText(problem)});
        item->setData(0, PROBLEM_ROLE, i);
    }
}

void DrcPanel::itemChosen()
{
    const QList<QTreeWidgetItem *> chosen = m_tree->selectedItems();
    if (chosen.isEmpty())
        return;
    const QVariant index = chosen.first()->data(0, PROBLEM_ROLE);
    if (index.isValid())
        emit problemChosen(m_problems.at(index.toInt()));
}

} // namespace ppcb
