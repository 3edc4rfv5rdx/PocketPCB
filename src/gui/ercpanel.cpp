#include "ercpanel.h"

#include "document.h"
#include "sheeteditor.h"

#include <QHeaderView>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ppcb {

namespace {

// Where an item keeps the index of its problem; a group has none.
const int PROBLEM_ROLE = Qt::UserRole;

} // namespace

ErcPanel::ErcPanel(Document *document, SheetEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_editor(editor)
{
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("ercTree"));
    m_tree->setColumnCount(1);
    m_tree->header()->hide();
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &ErcPanel::itemChosen);
    // A click on the problem chosen shows it again.
    connect(m_tree, &QTreeWidget::itemClicked, this, &ErcPanel::itemChosen);
    connect(m_editor, &SheetEditor::netsChanged, this, &ErcPanel::refresh);
    refresh();
}

QString ErcPanel::ercTitle()
{
    return tr("ERC");
}

QString ErcPanel::problemText(const ErcProblem &problem)
{
    switch (problem.kind) {
    case ErcKind::EmptyValue:
        return tr("%1 has no value").arg(problem.subject);
    case ErcKind::DuplicateDesignator:
        return tr("Designator %1 repeats").arg(problem.subject);
    case ErcKind::UnconnectedPin:
        return tr("Pin %1 is not connected").arg(problem.subject);
    case ErcKind::MisplacedNoConnect:
        return problem.subject.isEmpty()
                   ? tr("No-connect mark on no pin")
                   : tr("No-connect mark on connected pin %1").arg(problem.subject);
    case ErcKind::LooseLabel:
        return tr("Label %1 joins nothing").arg(problem.subject);
    case ErcKind::LooseWireEnd:
        return tr("Wire end joins nothing");
    case ErcKind::OverlappingWires:
        return tr("Wires lie on top of each other");
    case ErcKind::WireLoop:
        return tr("Wires make a loop");
    case ErcKind::ShortedComponent:
        return tr("%1 is shorted").arg(problem.subject);
    case ErcKind::TwoNames:
        return tr("Net has two names") + QStringLiteral(": ") + problem.subject;
    case ErcKind::LostTableName:
        return tr("Net name %1 has no pins left").arg(problem.subject);
    case ErcKind::NameOfAnotherNet:
        return tr("Name %1 belongs to another net").arg(problem.subject);
    }
    return QString();
}

void ErcPanel::refresh()
{
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    m_problems = m_editor->problems();
    if (m_problems.isEmpty()) {
        QTreeWidgetItem *item = new QTreeWidgetItem(m_tree, {tr("No problems found")});
        item->setFlags(Qt::NoItemFlags);
        return;
    }
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    // The problems come sheet by sheet, the project's own last.
    QTreeWidgetItem *group = nullptr;
    int groupSheet = -2;
    for (int i = 0; i < m_problems.size(); ++i) {
        const ErcProblem &problem = m_problems.at(i);
        if (group == nullptr || problem.sheet != groupSheet) {
            groupSheet = problem.sheet;
            const QString title = problem.sheet >= 0 && problem.sheet < sheets.size()
                                      ? SheetEditor::sheetName(sheets.at(problem.sheet),
                                                               problem.sheet)
                                      : tr("Whole project");
            group = new QTreeWidgetItem(m_tree, {title});
            group->setFlags(Qt::ItemIsEnabled);
            group->setExpanded(true);
        }
        QTreeWidgetItem *item = new QTreeWidgetItem(group, {problemText(problem)});
        item->setData(0, PROBLEM_ROLE, i);
    }
}

void ErcPanel::itemChosen()
{
    const QList<QTreeWidgetItem *> chosen = m_tree->selectedItems();
    if (chosen.isEmpty())
        return;
    const QVariant index = chosen.first()->data(0, PROBLEM_ROLE);
    if (!index.isValid())
        return;
    const ErcProblem &problem = m_problems.at(index.toInt());
    if (problem.sheet >= 0)
        emit problemChosen(problem);
}

} // namespace ppcb
