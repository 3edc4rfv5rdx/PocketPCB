#include "librarypanel.h"

#include "language.h"
#include "paint.h"
#include "sheetedit.h"

#include <QApplication>
#include <QComboBox>
#include <QDrag>
#include <QFont>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace ppcb {

namespace {

// Item data: the part's name, on part items only, and the source, on part
// and section items.
const int PART_ROLE = Qt::UserRole;
const int SOURCE_ROLE = Qt::UserRole + 1;

const char PART_MIME_TYPE[] = "application/x-pocketpcb-part";

// The list of parts, from which a part can be dragged onto a sheet.
class PartTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;
    std::function<QMimeData *(const QTreeWidgetItem *)> makeMimeData;

protected:
    // Without the picture of the row a list drags by default: it would hide
    // the part, which follows the cursor on the sheet itself.
    void startDrag(Qt::DropActions actions) override
    {
        Q_UNUSED(actions);
        const QTreeWidgetItem *item = currentItem();
        QMimeData *data = item == nullptr || !makeMimeData ? nullptr : makeMimeData(item);
        if (data == nullptr)
            return;
        QDrag *drag = new QDrag(this);
        drag->setMimeData(data);
        drag->exec(Qt::CopyAction);
    }
};

// Room around the drawings, as a share of the preview's size.
const double PREVIEW_MARGIN_SHARE = 0.1;

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

void PartPreview::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        m_pressPos = event->position().toPoint();
    QWidget::mousePressEvent(event);
}

void PartPreview::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton) || !makeMimeData
        || (event->position().toPoint() - m_pressPos).manhattanLength()
               < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    QMimeData *data = makeMimeData();
    if (data == nullptr)
        return;
    QDrag *drag = new QDrag(this);
    drag->setMimeData(data);
    drag->exec(Qt::CopyAction);
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

    QToolButton *expandButton = new QToolButton(this);
    expandButton->setObjectName(QStringLiteral("libraryExpandAll"));
    expandButton->setText(QStringLiteral("⊞"));
    expandButton->setToolTip(tr("Expand all"));
    QToolButton *collapseButton = new QToolButton(this);
    collapseButton->setObjectName(QStringLiteral("libraryCollapseAll"));
    collapseButton->setText(QStringLiteral("⊟"));
    collapseButton->setToolTip(tr("Collapse all"));

    QHBoxLayout *searchRow = new QHBoxLayout;
    searchRow->addWidget(m_search, 1);
    searchRow->addWidget(expandButton);
    searchRow->addWidget(collapseButton);
    searchRow->addWidget(reloadButton);

    PartTree *tree = new PartTree(this);
    tree->makeMimeData = [this](const QTreeWidgetItem *item) -> QMimeData * {
        const QString part = item->data(0, PART_ROLE).toString();
        if (part.isEmpty())
            return nullptr;
        return partMimeData(Source(item->data(0, SOURCE_ROLE).toInt()), part, footprintFor(item));
    };
    tree->setDragEnabled(true);
    tree->setDragDropMode(QAbstractItemView::DragOnly);
    m_tree = tree;
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);

    m_footprintBox = new QComboBox(this);
    m_footprintBox->setObjectName(QStringLiteral("libraryFootprint"));
    m_preview = new PartPreview(this);
    m_preview->makeMimeData = [this]() -> QMimeData * {
        const QString part = currentPart();
        if (part.isEmpty())
            return nullptr;
        return partMimeData(currentSource(), part, m_footprintBox->currentText());
    };

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
    connect(expandButton, &QToolButton::clicked, this, [this] { setAllGroupsExpanded(true); });
    connect(collapseButton, &QToolButton::clicked, this,
            [this] { setAllGroupsExpanded(false); });
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &LibraryPanel::showCurrentPart);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const QString part = item->data(0, PART_ROLE).toString();
        if (!part.isEmpty())
            emit placeRequested(Source(item->data(0, SOURCE_ROLE).toInt()), part, footprintFor(item));
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { noteExpanded(item, true); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) { noteExpanded(item, false); });
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

void LibraryPanel::setAllGroupsExpanded(bool expanded)
{
    const bool searching = !m_search->text().trimmed().isEmpty();
    const QSignalBlocker blocker(m_tree);
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem *section = m_tree->topLevelItem(s);
        for (int g = 0; g < section->childCount(); ++g) {
            QTreeWidgetItem *group = section->child(g);
            if (group->isHidden())
                continue;
            group->setExpanded(expanded);
            if (searching)
                continue;
            if (expanded)
                m_expandedGroups.insert(groupKey(group));
            else
                m_expandedGroups.remove(groupKey(group));
        }
    }
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
    const int scroll = m_tree->verticalScrollBar()->value();

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
    // Laid out now, so that the scroll bar has its range back.
    m_tree->doItemsLayout();
    m_tree->verticalScrollBar()->setValue(scroll);
}

QString LibraryPanel::groupKey(const QTreeWidgetItem *group)
{
    return QString::number(group->parent()->data(0, SOURCE_ROLE).toInt()) + QLatin1Char('/')
           + group->text(0);
}

void LibraryPanel::noteExpanded(QTreeWidgetItem *item, bool expanded)
{
    // Only groups, and only the user's own choice: a search opens groups by
    // itself.
    if (item->parent() == nullptr || !item->data(0, PART_ROLE).toString().isEmpty()
        || !m_search->text().trimmed().isEmpty()) {
        return;
    }
    if (expanded)
        m_expandedGroups.insert(groupKey(item));
    else
        m_expandedGroups.remove(groupKey(item));
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
        groups[designatorPrefix(it.value(), m_style)].append(
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
            // While searching, every group that has something is open;
            // without a search, those the user keeps open.
            const QSignalBlocker blocker(m_tree);
            group->setExpanded(text.isEmpty() ? m_expandedGroups.contains(groupKey(group))
                                              : groupShown > 0);
            sectionShown += groupShown;
        }
        section->setHidden(sectionShown == 0);
    }
}

QString LibraryPanel::footprintFor(const QTreeWidgetItem *item) const
{
    if (item == m_tree->currentItem())
        return m_footprintBox->currentText();
    const Source source = Source(item->data(0, SOURCE_ROLE).toInt());
    const Library &library = source == Source::Project ? m_project : m_library.items;
    const Part *part = library.findPart(item->data(0, PART_ROLE).toString());
    return part == nullptr ? QString() : part->footprints.value(0);
}

QString LibraryPanel::partMimeType()
{
    return QLatin1String(PART_MIME_TYPE);
}

QMimeData *LibraryPanel::partMimeData(Source source, const QString &part, const QString &footprint)
{
    QMimeData *data = new QMimeData;
    const QStringList fields = {QString::number(int(source)), part, footprint};
    data->setData(partMimeType(), fields.join(QLatin1Char('\n')).toUtf8());
    return data;
}

bool LibraryPanel::readPartMimeData(const QMimeData *data, Source &source, QString &part,
                                    QString &footprint)
{
    if (data == nullptr || !data->hasFormat(partMimeType()))
        return false;
    const QStringList fields = QString::fromUtf8(data->data(partMimeType())).split(QLatin1Char('\n'));
    if (fields.size() != 3 || fields.at(1).isEmpty())
        return false;
    source = fields.at(0) == QString::number(int(Source::Project)) ? Source::Project
                                                                    : Source::Library;
    part = fields.at(1);
    footprint = fields.at(2);
    return true;
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
