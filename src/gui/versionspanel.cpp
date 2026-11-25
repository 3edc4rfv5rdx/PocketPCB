#include "versionspanel.h"

#include "document.h"
#include "paint.h"
#include "projectversions.h"

#include <QDir>
#include <QHBoxLayout>
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
    m_openAction = new QAction(openCopyTitle(), this);
    m_tree->addActions({m_restoreAction, m_openAction});
    m_restoreButton = new QPushButton(restoreTitle() + QStringLiteral("…"), this);
    m_restoreButton->setObjectName(QStringLiteral("restoreVersionButton"));
    m_openButton = new QPushButton(openCopyTitle(), this);
    m_openButton->setObjectName(QStringLiteral("openVersionCopyButton"));

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(m_restoreButton);
    buttons->addWidget(m_openButton);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addLayout(buttons);
    layout->addWidget(m_tree);

    const auto restore = [this] {
        int number = 0;
        QString path;
        if (chosenVersion(number, path))
            emit restoreRequested(number, path);
    };
    const auto open = [this] {
        int number = 0;
        QString path;
        if (chosenVersion(number, path))
            emit openRequested(number, path);
    };
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &VersionsPanel::updateRestore);
    connect(m_restoreAction, &QAction::triggered, this, restore);
    connect(m_restoreButton, &QPushButton::clicked, this, restore);
    connect(m_openAction, &QAction::triggered, this, open);
    connect(m_openButton, &QPushButton::clicked, this, open);
    // A double click opens the copy.
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, open);
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

QString VersionsPanel::openCopyTitle()
{
    return tr("Open a copy");
}

bool VersionsPanel::chosenVersion(int &number, QString &path) const
{
    const QList<QTreeWidgetItem *> chosen = m_tree->selectedItems();
    if (chosen.isEmpty())
        return false;
    path = chosen.first()->data(NumberColumn, PATH_ROLE).toString();
    number = chosen.first()->text(NumberColumn).toInt();
    return !path.isEmpty();
}

void VersionsPanel::updateRestore()
{
    int number = 0;
    QString path;
    const bool readable = chosenVersion(number, path);
    for (QAction *action : {m_restoreAction, m_openAction})
        action->setEnabled(readable);
    for (QPushButton *button : {m_restoreButton, m_openButton})
        button->setEnabled(readable);
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
