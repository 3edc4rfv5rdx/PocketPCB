#include "librarypanel.h"

#include "language.h"
#include "paint.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

// Item data: the part's name, on part items only, and the source, on part
// and section items.
const int PART_ROLE = Qt::UserRole;
const int SOURCE_ROLE = Qt::UserRole + 1;

// Room around the drawings, as a share of the preview's size.
const double PREVIEW_MARGIN_SHARE = 0.1;

// The prefix the parts are grouped by in the given style.
QString prefixOf(const Part &part, DesignatorStyle style)
{
    return style == DesignatorStyle::Gost ? part.prefixGost : part.prefix;
}

// Draws 'paint' scaled to fit 'bounds' (millimetres) into 'area' (pixels).
template <typename Paint>
void paintFitted(QPainter &painter, const QRectF &area, const QRectF &bounds, Paint paint)
{
    if (bounds.isEmpty() || area.isEmpty())
        return;
    const QRectF room = area.adjusted(area.width() * PREVIEW_MARGIN_SHARE,
                                      area.height() * PREVIEW_MARGIN_SHARE,
                                      -area.width() * PREVIEW_MARGIN_SHARE,
                                      -area.height() * PREVIEW_MARGIN_SHARE);
    const double scale = std::min(room.width() / bounds.width(), room.height() / bounds.height());
    painter.save();
    painter.translate(room.center());
    painter.scale(scale, scale);
    painter.translate(-bounds.center());
    paint(painter);
    painter.restore();
}

} // namespace

PartPreview::PartPreview(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(160);
}

void PartPreview::setContent(const Symbol *symbol, const Footprint *footprint)
{
    m_symbol = symbol;
    m_footprint = footprint;
    update();
}

void PartPreview::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.fillRect(rect(), paintcolor::background());
    const QRectF whole = rect();
    const QRectF top(whole.left(), whole.top(), whole.width(), whole.height() / 2);
    const QRectF bottom(whole.left(), top.bottom(), whole.width(), whole.height() / 2);
    if (m_symbol != nullptr) {
        const Symbol &symbol = *m_symbol;
        paintFitted(painter, top, symbolBounds(symbol),
                    [&symbol](QPainter &p) { paintSymbol(p, symbol); });
    }
    if (m_footprint != nullptr) {
        const Footprint &footprint = *m_footprint;
        paintFitted(painter, bottom, footprintBounds(footprint),
                    [&footprint](QPainter &p) { paintFootprint(p, footprint); });
    }
}

LibraryPanel::LibraryPanel(QWidget *parent)
    : QWidget(parent)
{
    m_search = new QLineEdit(this);
    m_search->setObjectName(QStringLiteral("librarySearch"));
    m_search->setPlaceholderText(tr("Search"));
    m_search->setClearButtonEnabled(true);
    QToolButton *reloadButton = new QToolButton(this);
    reloadButton->setObjectName(QStringLiteral("libraryReload"));
    reloadButton->setText(QStringLiteral("⟳"));
    reloadButton->setToolTip(tr("Read the library again"));

    QHBoxLayout *searchRow = new QHBoxLayout;
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(reloadButton);

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);

    m_footprintBox = new QComboBox(this);
    m_footprintBox->setObjectName(QStringLiteral("libraryFootprint"));
    m_preview = new PartPreview(this);

    QWidget *previewBox = new QWidget(this);
    QVBoxLayout *previewLayout = new QVBoxLayout(previewBox);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(m_footprintBox);
    previewLayout->addWidget(m_preview, 1);

    QSplitter *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_tree);
    splitter->addWidget(previewBox);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(searchRow);
    layout->addWidget(splitter, 1);

    connect(m_search, &QLineEdit::textChanged, this, &LibraryPanel::applyFilter);
    connect(reloadButton, &QToolButton::clicked, this, &LibraryPanel::reloadRequested);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &LibraryPanel::showCurrentPart);
    connect(m_footprintBox, &QComboBox::currentIndexChanged, this,
            &LibraryPanel::showCurrentFootprint);
    showCurrentPart();
}

QString LibraryPanel::libraryTitle()
{
    return tr("Library");
}

void LibraryPanel::setLibraries(const MergedLibrary &library, const Library &project,
                                DesignatorStyle style)
{
    if (m_hasLibraries && library.items == m_library.items
        && library.replacedParts == m_library.replacedParts && project == m_project
        && style == m_style) {
        return;
    }
    m_library = library;
    m_project = project;
    m_style = style;
    m_hasLibraries = true;
    rebuild();
}

void LibraryPanel::setFilter(const QString &text)
{
    m_search->setText(text);
}

QString LibraryPanel::currentPart() const
{
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item == nullptr ? QString() : item->data(0, PART_ROLE).toString();
}

LibraryPanel::Source LibraryPanel::currentSource() const
{
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item == nullptr ? Source::Library : Source(item->data(0, SOURCE_ROLE).toInt());
}

void LibraryPanel::rebuild()
{
    // The chosen part is kept across the rebuild when it is still there.
    const QString part = currentPart();
    const Source source = currentSource();

    m_tree->clear();
    addSection(libraryTitle(), m_library.items, Source::Library, m_library.replacedParts);
    if (!m_project.parts.isEmpty())
        addSection(tr("Project"), m_project, Source::Project, QSet<QString>());

    for (QTreeWidgetItemIterator it(m_tree); *it != nullptr; ++it) {
        if ((*it)->data(0, PART_ROLE).toString() == part && !part.isEmpty()
            && Source((*it)->data(0, SOURCE_ROLE).toInt()) == source) {
            m_tree->setCurrentItem(*it);
            break;
        }
    }
    applyFilter();
    showCurrentPart();
}

void LibraryPanel::addSection(const QString &title, const Library &library, Source source,
                              const QSet<QString> &replaced)
{
    QTreeWidgetItem *section = new QTreeWidgetItem(m_tree, {title});
    section->setFlags(Qt::ItemIsEnabled);
    section->setData(0, SOURCE_ROLE, int(source));
    const QStringList languages = nameLanguages();

    // Prefixes in order, parts by their shown name inside each.
    QMap<QString, QList<QPair<QString, QString>>> groups;
    for (auto it = library.parts.constBegin(); it != library.parts.constEnd(); ++it) {
        groups[prefixOf(it.value(), m_style)].append(
            {it.value().displayName(languages, it.key()), it.key()});
    }
    for (auto group = groups.begin(); group != groups.end(); ++group) {
        QTreeWidgetItem *groupItem = new QTreeWidgetItem(section, {group.key()});
        groupItem->setFlags(Qt::ItemIsEnabled);
        std::sort(group->begin(), group->end(), [](const auto &a, const auto &b) {
            return QString::localeAwareCompare(a.first, b.first) < 0;
        });
        for (const auto &entry : *group) {
            QTreeWidgetItem *partItem = new QTreeWidgetItem(groupItem, {entry.first});
            partItem->setData(0, PART_ROLE, entry.second);
            partItem->setData(0, SOURCE_ROLE, int(source));
            QString tip = entry.second;
            if (replaced.contains(entry.second)) {
                QFont font = partItem->font(0);
                font.setItalic(true);
                partItem->setFont(0, font);
                tip += QStringLiteral(" — ") + tr("replaced by the user's library");
            }
            partItem->setToolTip(0, tip);
        }
    }
    section->setExpanded(true);
}

void LibraryPanel::applyFilter()
{
    const QString text = m_search->text().trimmed();
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem *section = m_tree->topLevelItem(s);
        const Library &library = Source(section->data(0, SOURCE_ROLE).toInt()) == Source::Project
                                     ? m_project
                                     : m_library.items;
        int sectionShown = 0;
        for (int g = 0; g < section->childCount(); ++g) {
            QTreeWidgetItem *group = section->child(g);
            int groupShown = 0;
            for (int p = 0; p < group->childCount(); ++p) {
                QTreeWidgetItem *item = group->child(p);
                const QString name = item->data(0, PART_ROLE).toString();
                const Part part = library.parts.value(name);
                const bool shown = text.isEmpty()
                                   || item->text(0).contains(text, Qt::CaseInsensitive)
                                   || name.contains(text, Qt::CaseInsensitive)
                                   || part.prefix.compare(text, Qt::CaseInsensitive) == 0
                                   || part.prefixGost.compare(text, Qt::CaseInsensitive) == 0;
                item->setHidden(!shown);
                groupShown += shown ? 1 : 0;
            }
            group->setHidden(groupShown == 0);
            // While searching, every group that has something is open.
            group->setExpanded(!text.isEmpty() && groupShown > 0);
            sectionShown += groupShown;
        }
        section->setHidden(sectionShown == 0);
    }
}

const Library &LibraryPanel::currentLibrary() const
{
    return currentSource() == Source::Project ? m_project : m_library.items;
}

void LibraryPanel::showCurrentPart()
{
    const Part *part = currentLibrary().findPart(currentPart());
    {
        const QSignalBlocker blocker(m_footprintBox);
        m_footprintBox->clear();
        if (part != nullptr)
            m_footprintBox->addItems(part->footprints);
    }
    m_footprintBox->setEnabled(m_footprintBox->count() > 1);
    showCurrentFootprint();
}

void LibraryPanel::showCurrentFootprint()
{
    const Library &library = currentLibrary();
    const QString part = currentPart();
    m_preview->setContent(part.isEmpty() ? nullptr : library.findSymbolOfPart(part),
                          library.findFootprint(m_footprintBox->currentText()));
}

} // namespace ppcb
