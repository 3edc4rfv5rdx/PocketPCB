#include "versionspanel.h"

#include "document.h"
#include "paint.h"
#include "projectversions.h"

#include <QDir>
#include <QHeaderView>
#include <QAction>
#include <QLocale>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ppcb {

namespace {

enum Column { NumberColumn, MadeColumn, RevisionColumn, DescriptionColumn, ColumnCount };
// Where an item keeps its archive; empty for one that cannot be read.
const int PATH_ROLE = Qt::UserRole;

} // namespace

VersionsPanel::VersionsPanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
{
    m_tree = new QTreeWidget(this);
    m_tree->setObjectName(QStringLiteral("versionsTree"));
    m_tree->setColumnCount(ColumnCount);
    m_tree->setHeaderLabels({tr("Number"), tr("Date"), revisionWord(), tr("Description")});
    m_tree->setRootIsDecorated(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setContextMenuPolicy(Qt::ActionsContextMenu);
    m_restoreAction = new QAction(restoreTitle() + QStringLiteral("…"), this);
    m_tree->addAction(m_restoreAction);
    m_restoreButton = new QPushButton(restoreTitle() + QStringLiteral("…"), this);
    m_restoreButton->setObjectName(QStringLiteral("restoreVersionButton"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_restoreButton);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &VersionsPanel::updateRestore);
    connect(m_restoreAction, &QAction::triggered, this, &VersionsPanel::restoreChosen);
    connect(m_restoreButton, &QPushButton::clicked, this, &VersionsPanel::restoreChosen);
    updateRestore();

    connect(m_document, &Document::designReset, this, &VersionsPanel::refresh);
    // Saving may have put the project in another folder.
    connect(m_document, &Document::modifiedChanged, this, &VersionsPanel::refresh);
}

QString VersionsPanel::versionsTitle()
{
    return tr("Versions");
}

QString VersionsPanel::restoreTitle()
{
    return tr("Restore this version");
}

void VersionsPanel::updateRestore()
{
    const QList<QTreeWidgetItem *> chosen = m_tree->selectedItems();
    const bool readable =
        !chosen.isEmpty() && !chosen.first()->data(NumberColumn, PATH_ROLE).toString().isEmpty();
    m_restoreAction->setEnabled(readable);
    m_restoreButton->setEnabled(readable);
}

void VersionsPanel::restoreChosen()
{
    const QList<QTreeWidgetItem *> chosen = m_tree->selectedItems();
    if (chosen.isEmpty())
        return;
    const QString path = chosen.first()->data(NumberColumn, PATH_ROLE).toString();
    if (!path.isEmpty())
        emit restoreRequested(chosen.first()->text(NumberColumn).toInt(), path);
}

void VersionsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

void VersionsPanel::refresh()
{
    if (!isVisible())
        return;
    m_tree->clear();
    if (m_document->projectPath().isEmpty())
        return;
    const QLocale locale;
    for (const ProjectVersion &version : projectVersions(m_document->projectPath())) {
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(NumberColumn, QString::number(version.number));
        item->setTextAlignment(NumberColumn, Qt::AlignRight | Qt::AlignVCenter);
        if (!version.readable) {
            item->setText(DescriptionColumn, tr("Cannot be read"));
            item->setForeground(DescriptionColumn, Qt::red);
        } else {
            item->setText(MadeColumn, locale.toString(version.made, QLocale::ShortFormat));
            item->setText(RevisionColumn, version.revision);
            item->setText(DescriptionColumn, version.description);
            item->setData(NumberColumn, PATH_ROLE, version.path);
        }
        item->setToolTip(DescriptionColumn, QDir::toNativeSeparators(version.path));
    }
    for (int column = 0; column + 1 < ColumnCount; ++column)
        m_tree->resizeColumnToContents(column);
    updateRestore();
}

} // namespace ppcb
