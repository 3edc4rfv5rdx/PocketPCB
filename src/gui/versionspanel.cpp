#include "versionspanel.h"

#include "document.h"
#include "paint.h"
#include "projectversions.h"

#include <QDir>
#include <QHeaderView>
#include <QLocale>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ppcb {

namespace {

enum Column { NumberColumn, MadeColumn, RevisionColumn, DescriptionColumn, ColumnCount };

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

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_tree);

    connect(m_document, &Document::designReset, this, &VersionsPanel::refresh);
    // Saving may have put the project in another folder.
    connect(m_document, &Document::modifiedChanged, this, &VersionsPanel::refresh);
}

QString VersionsPanel::versionsTitle()
{
    return tr("Versions");
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
        }
        item->setToolTip(DescriptionColumn, QDir::toNativeSeparators(version.path));
    }
    for (int column = 0; column + 1 < ColumnCount; ++column)
        m_tree->resizeColumnToContents(column);
}

} // namespace ppcb
