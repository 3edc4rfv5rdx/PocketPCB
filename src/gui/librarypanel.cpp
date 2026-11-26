#include "librarypanel.h"

#include "language.h"
#include "paint.h"
#include "sheetedit.h"

#include <QApplication>
#include <QComboBox>
#include <QFont>
#include <QHash>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>

namespace ppcb {

namespace {

// Item data: the name of the part or of the power symbol, on their items
// only; the source, on those items and on section items; and on the items of
// power symbols, that they are.
const int PART_ROLE = Qt::UserRole;
const int SOURCE_ROLE = Qt::UserRole + 1;
const int POWER_ROLE = Qt::UserRole + 2;

bool isPowerItem(const QTreeWidgetItem *item)
{
    return item != nullptr && item->data(0, POWER_ROLE).toBool();
}

// True when 'library' has a power symbol.
bool hasPowerSymbols(const Library &library)
{
    return std::any_of(library.symbols.cbegin(), library.symbols.cend(),
                       [](const Symbol &symbol) { return symbol.isPower(); });
}

// The list of parts, from which a part can be dragged onto a sheet.
class PartTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;
    PartDragGesture drag;

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        drag.press(event);
        QTreeWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!drag.move(this, event))
            QTreeWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!drag.release(this, event))
            QTreeWidget::mouseReleaseEvent(event);
    }
};

// Sections and groups: chosen like the parts, so that the chosen row is
// marked the same bright way wherever it is, but never edited or dragged.
const Qt::ItemFlags HEADER_FLAGS = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

// The chosen item: its background and its text.
const char CHOSEN_ITEM_STYLE[] =
    "QTreeView::item:selected, QTreeView::item:selected:!active { background: %1; color: %2; }";

// The titles of the sections are bold and this much larger than the rest.
const double SECTION_FONT_SCALE = 1.15;

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

// The pixmaps of symbolIcon(): as many pixels per pixel of the icon.
const int ICON_SCALES[] = {1, 2};
// symbolIcon() draws lines two pixels wide on icons this large and larger,
// one pixel wide on smaller ones.
const int WIDE_LINE_ICON_SIZE = 20;
// Qt's angles are in sixteenths of a degree.
const int QT_ANGLE_UNITS = 16;

} // namespace

QIcon symbolIcon(const Symbol &symbol, int size, const QColor &color)
{
    // The lines and the pins alone, in millimetres: texts would be specks.
    double left = std::numeric_limits<double>::max();
    double top = left;
    double right = std::numeric_limits<double>::lowest();
    double bottom = right;
    const auto take = [&](const QPointF &point) {
        left = std::min(left, point.x());
        top = std::min(top, point.y());
        right = std::max(right, point.x());
        bottom = std::max(bottom, point.y());
    };
    for (const Pin &pin : symbol.pins) {
        take(toMm(pin.at));
        take(toMm(pinBodyEnd(pin)));
    }
    for (const Graphic &graphic : symbol.graphics) {
        const QPointF a = toMm(graphic.a);
        const double radius = nmToMm(graphic.radius);
        switch (graphic.kind) {
        case GraphicKind::Line:
        case GraphicKind::Rect:
        case GraphicKind::Oval:
            take(a);
            take(toMm(graphic.b));
            break;
        case GraphicKind::Circle:
        case GraphicKind::Arc:
            take(a - QPointF(radius, radius));
            take(a + QPointF(radius, radius));
            break;
        case GraphicKind::Polygon:
            for (const Point &point : graphic.points)
                take(toMm(point));
            break;
        case GraphicKind::Text:
            break;
        }
    }
    QIcon icon;
    if (left > right)
        return icon;

    const QPointF middle((left + right) / 2, (top + bottom) / 2);
    const double extent = std::max(right - left, bottom - top);
    for (const int scale : ICON_SCALES) {
        const int pixels = size * scale;
        const int width = (size >= WIDE_LINE_ICON_SIZE ? 2 : 1) * scale;
        // Room for half a line and a pixel on each side.
        const double room = pixels - 2.0 * (width + scale);
        const double perMm = extent > 0 ? room / extent : 0.0;
        // Every point lands on a whole pixel, or on the middle of one under
        // a line of an odd width, so that straight lines cover whole rows of
        // pixels and stay sharp.
        const double shift = width % 2 != 0 ? 0.5 : 0.0;
        const auto map = [&](const QPointF &mm) {
            const QPointF at = (mm - middle) * perMm + QPointF(pixels / 2.0, pixels / 2.0);
            return QPointF(std::round(at.x() - shift) + shift, std::round(at.y() - shift) + shift);
        };

        QPixmap pixmap(pixels, pixels);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(color, width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
        painter.setBrush(Qt::NoBrush);
        for (const Pin &pin : symbol.pins)
            painter.drawLine(map(toMm(pin.at)), map(toMm(pinBodyEnd(pin))));
        for (const Graphic &graphic : symbol.graphics) {
            const QPointF a = map(toMm(graphic.a));
            const double radius = std::round(nmToMm(graphic.radius) * perMm);
            const QRectF circle(a - QPointF(radius, radius), QSizeF(2 * radius, 2 * radius));
            switch (graphic.kind) {
            case GraphicKind::Line:
                painter.drawLine(a, map(toMm(graphic.b)));
                break;
            case GraphicKind::Rect:
                painter.drawRect(QRectF(a, map(toMm(graphic.b))).normalized());
                break;
            case GraphicKind::Oval: {
                const QRectF box = QRectF(a, map(toMm(graphic.b))).normalized();
                const double round = std::min(box.width(), box.height()) / 2;
                painter.drawRoundedRect(box, round, round);
                break;
            }
            case GraphicKind::Circle:
                painter.drawEllipse(circle);
                break;
            case GraphicKind::Arc:
                // As on the sheet: the model's angles grow clockwise on screen.
                painter.drawArc(circle, -graphic.startAngle * QT_ANGLE_UNITS,
                                -graphic.spanAngle * QT_ANGLE_UNITS);
                break;
            case GraphicKind::Polygon: {
                QPolygonF polygon;
                for (const Point &point : graphic.points)
                    polygon.append(map(toMm(point)));
                painter.setBrush(graphic.filled ? QBrush(color) : QBrush(Qt::NoBrush));
                painter.drawPolygon(polygon);
                painter.setBrush(Qt::NoBrush);
                break;
            }
            case GraphicKind::Text:
                break;
            }
        }
        painter.end();
        pixmap.setDevicePixelRatio(scale);
        icon.addPixmap(pixmap);
    }
    return icon;
}

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

void PartPreview::setFootprintAlone(bool alone)
{
    m_footprintAlone = alone;
    update();
}

void PartDragGesture::press(const QMouseEvent *event)
{
    m_pressed = event->button() == Qt::LeftButton;
    m_dragging = false;
    m_pressPos = event->position().toPoint();
}

bool PartDragGesture::move(QWidget *widget, const QMouseEvent *event)
{
    if (!m_pressed || !(event->buttons() & Qt::LeftButton))
        return false;
    if (!m_dragging) {
        if ((event->position().toPoint() - m_pressPos).manhattanLength()
            < QApplication::startDragDistance()) {
            return false;
        }
        m_pressed = start && start(m_pressPos);
        if (!m_pressed)
            return false;
        m_dragging = true;
        widget->setCursor(Qt::ClosedHandCursor);
    }
    if (moved)
        moved(event->globalPosition().toPoint());
    return true;
}

bool PartDragGesture::release(QWidget *widget, const QMouseEvent *event)
{
    const bool wasDragging = m_dragging && event->button() == Qt::LeftButton;
    m_pressed = false;
    m_dragging = false;
    if (!wasDragging)
        return false;
    widget->unsetCursor();
    if (ended)
        ended(event->globalPosition().toPoint());
    return true;
}

void PartPreview::mousePressEvent(QMouseEvent *event)
{
    drag.press(event);
    QWidget::mousePressEvent(event);
}

void PartPreview::mouseMoveEvent(QMouseEvent *event)
{
    if (!drag.move(this, event))
        QWidget::mouseMoveEvent(event);
}

void PartPreview::mouseReleaseEvent(QMouseEvent *event)
{
    if (!drag.release(this, event))
        QWidget::mouseReleaseEvent(event);
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
        paintFitted(painter, m_footprintAlone && m_symbol == nullptr ? whole : bottom,
                    footprintBounds(footprint),
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
    expandButton->setToolTip(tr("Expand the chosen section"));
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
    tree->drag.start = [this, tree](const QPoint &pressPos) {
        return requestPlacing(tree->itemAt(pressPos), true);
    };
    tree->drag.moved = [this](const QPoint &at) { emit dragMoved(at); };
    tree->drag.ended = [this](const QPoint &at) { emit dragEnded(at); };
    m_tree = tree;
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(1);
    // The chosen item in the colour of the selection on the sheet, just as
    // bright when the panel is not the one in use. A style sheet, since some
    // styles draw the selection their own way, whatever the palette says.
    m_tree->setStyleSheet(QString::fromLatin1(CHOSEN_ITEM_STYLE)
                              .arg(paintcolor::selection().name(),
                                   paintcolor::background().name()));

    m_footprintBox = new QComboBox(this);
    m_footprintBox->setObjectName(QStringLiteral("libraryFootprint"));
    m_preview = new PartPreview(this);
    m_preview->drag.start = [this](const QPoint &) {
        return requestPlacing(m_tree->currentItem(), true);
    };
    m_preview->drag.moved = [this](const QPoint &at) { emit dragMoved(at); };
    m_preview->drag.ended = [this](const QPoint &at) { emit dragEnded(at); };

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
    connect(expandButton, &QToolButton::clicked, this, &LibraryPanel::expandChosenSection);
    connect(collapseButton, &QToolButton::clicked, this,
            &LibraryPanel::collapseAll);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &LibraryPanel::showCurrentPart);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item) { requestPlacing(item, false); });
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

void LibraryPanel::setLibraries(const MergedLibrary &library, const Library &project)
{
    if (m_hasLibraries && library.items == m_library.items
        && library.replacedParts == m_library.replacedParts && project == m_project) {
        return;
    }
    m_library = library;
    m_project = project;
    m_hasLibraries = true;
    rebuild();
}

void LibraryPanel::setFilter(const QString &text)
{
    m_search->setText(text);
}

void LibraryPanel::expandChosenSection()
{
    // The section of the chosen item; every section when none is chosen.
    const QTreeWidgetItem *chosen = m_tree->currentItem();
    while (chosen != nullptr && chosen->parent() != nullptr)
        chosen = chosen->parent();
    const bool searching = !m_search->text().trimmed().isEmpty();
    const QSignalBlocker blocker(m_tree);
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem *section = m_tree->topLevelItem(s);
        if (chosen != nullptr && section != chosen)
            continue;
        section->setExpanded(true);
        for (int g = 0; g < section->childCount(); ++g) {
            QTreeWidgetItem *group = section->child(g);
            if (group->isHidden())
                continue;
            group->setExpanded(true);
            if (!searching)
                m_expandedGroups.insert(groupKey(group));
        }
        if (!searching)
            m_collapsedSections.remove(sectionKey(section));
    }
}

void LibraryPanel::collapseAll()
{
    const bool searching = !m_search->text().trimmed().isEmpty();
    const QSignalBlocker blocker(m_tree);
    for (int s = 0; s < m_tree->topLevelItemCount(); ++s) {
        QTreeWidgetItem *section = m_tree->topLevelItem(s);
        section->setExpanded(false);
        for (int g = 0; g < section->childCount(); ++g) {
            QTreeWidgetItem *group = section->child(g);
            group->setExpanded(false);
            if (!searching)
                m_expandedGroups.remove(groupKey(group));
        }
        if (!searching)
            m_collapsedSections.insert(sectionKey(section));
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

bool LibraryPanel::currentIsPower() const
{
    return isPowerItem(m_tree->currentItem());
}

QString LibraryPanel::currentFootprint() const
{
    if (currentPart().isEmpty() || currentIsPower())
        return QString();
    return m_footprintBox->currentText();
}

bool LibraryPanel::requestPlacing(const QTreeWidgetItem *item, bool drag)
{
    const QString name = item == nullptr ? QString() : item->data(0, PART_ROLE).toString();
    if (name.isEmpty())
        return false;
    const Source source = Source(item->data(0, SOURCE_ROLE).toInt());
    if (isPowerItem(item)) {
        if (drag)
            emit powerDragStarted(source, name);
        else
            emit placePowerRequested(source, name);
    } else if (drag) {
        emit dragStarted(source, name, footprintFor(item));
    } else {
        emit placeRequested(source, name, footprintFor(item));
    }
    return true;
}

bool LibraryPanel::showProjectPart(const QString &part, const QString &footprint)
{
    QTreeWidgetItem *item = findPartItem(Source::Project, part);
    if (item == nullptr)
        return false;
    // Fills the footprint box with the part's footprints.
    m_tree->setCurrentItem(item);
    const int index = m_footprintBox->findText(footprint);
    if (index >= 0)
        m_footprintBox->setCurrentIndex(index);
    // A part hidden by the search stays hidden; its drawing is shown all the same.
    if (!item->isHidden()) {
        item->parent()->setExpanded(true);
        m_tree->scrollToItem(item);
    }
    return true;
}

QTreeWidgetItem *LibraryPanel::findPartItem(Source source, const QString &part) const
{
    if (part.isEmpty())
        return nullptr;
    for (QTreeWidgetItemIterator it(m_tree); *it != nullptr; ++it) {
        if ((*it)->data(0, PART_ROLE).toString() == part && !isPowerItem(*it)
            && Source((*it)->data(0, SOURCE_ROLE).toInt()) == source) {
            return *it;
        }
    }
    return nullptr;
}

void LibraryPanel::rebuild()
{
    // The chosen part is kept across the rebuild when it is still there.
    const QString part = currentPart();
    const Source source = currentSource();
    const bool power = currentIsPower();
    const int scroll = m_tree->verticalScrollBar()->value();

    m_tree->clear();
    addSection(libraryTitle(), m_library.items, Source::Library, m_library.replacedParts);
    if (!m_project.parts.isEmpty() || hasPowerSymbols(m_project))
        addSection(tr("Project"), m_project, Source::Project, QSet<QString>());

    for (QTreeWidgetItemIterator it(m_tree); *it != nullptr; ++it) {
        if ((*it)->data(0, PART_ROLE).toString() == part && isPowerItem(*it) == power
            && Source((*it)->data(0, SOURCE_ROLE).toInt()) == source && !part.isEmpty()) {
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
    return sectionKey(group->parent()) + QLatin1Char('/') + group->text(0);
}

QString LibraryPanel::sectionKey(const QTreeWidgetItem *section)
{
    return QString::number(section->data(0, SOURCE_ROLE).toInt());
}

void LibraryPanel::noteExpanded(QTreeWidgetItem *item, bool expanded)
{
    // Only sections and groups, and only the user's own choice: a search
    // opens them by itself.
    if (!item->data(0, PART_ROLE).toString().isEmpty()
        || !m_search->text().trimmed().isEmpty()) {
        return;
    }
    if (item->parent() == nullptr) {
        if (expanded)
            m_collapsedSections.remove(sectionKey(item));
        else
            m_collapsedSections.insert(sectionKey(item));
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
    section->setFlags(HEADER_FLAGS);
    QFont font = section->font(0);
    font.setBold(true);
    if (font.pointSizeF() > 0)
        font.setPointSizeF(font.pointSizeF() * SECTION_FONT_SCALE);
    else
        font.setPixelSize(qRound(font.pixelSize() * SECTION_FONT_SCALE));
    section->setFont(0, font);
    section->setData(0, SOURCE_ROLE, int(source));
    const QStringList languages = nameLanguages();

    addPowerGroup(section, library, source);

    // The groups of the parts in the program's order (see partGroupRank()),
    // those it does not know by name; parts by their shown name inside each.
    QHash<QString, QList<QPair<QString, QString>>> groups;
    for (auto it = library.parts.constBegin(); it != library.parts.constEnd(); ++it) {
        groups[it.value().group].append({it.value().displayName(languages, it.key()), it.key()});
    }
    QStringList keys = groups.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
        const int rankA = partGroupRank(a);
        const int rankB = partGroupRank(b);
        if (rankA != rankB)
            return rankA < rankB;
        return QString::localeAwareCompare(partGroupName(a), partGroupName(b)) < 0;
    });
    for (const QString &key : std::as_const(keys)) {
        QList<QPair<QString, QString>> &group = groups[key];
        QTreeWidgetItem *groupItem = new QTreeWidgetItem(section, {partGroupName(key)});
        groupItem->setFlags(HEADER_FLAGS);
        std::sort(group.begin(), group.end(), [](const auto &a, const auto &b) {
            return QString::localeAwareCompare(a.first, b.first) < 0;
        });
        for (const auto &entry : std::as_const(group)) {
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
    section->setExpanded(!m_collapsedSections.contains(sectionKey(section)));
}

void LibraryPanel::addPowerGroup(QTreeWidgetItem *section, const Library &library, Source source)
{
    if (!hasPowerSymbols(library))
        return;
    const QStringList languages = nameLanguages();
    QList<QPair<QString, QString>> entries;
    for (auto it = library.symbols.constBegin(); it != library.symbols.constEnd(); ++it) {
        if (it.value().isPower())
            entries.append({it.value().displayName(languages, it.key()), it.key()});
    }
    std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
        return QString::localeAwareCompare(a.first, b.first) < 0;
    });
    QTreeWidgetItem *group = new QTreeWidgetItem(section, {tr("Power")});
    group->setFlags(HEADER_FLAGS);
    for (const auto &entry : entries) {
        QTreeWidgetItem *item = new QTreeWidgetItem(group, {entry.first});
        item->setData(0, PART_ROLE, entry.second);
        item->setData(0, SOURCE_ROLE, int(source));
        item->setData(0, POWER_ROLE, true);
        // The net name it is placed with, and its file.
        item->setToolTip(0, library.symbols.value(entry.second).power + QStringLiteral(" — ")
                                + entry.second);
    }
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
                bool shown = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive)
                             || name.contains(text, Qt::CaseInsensitive);
                if (isPowerItem(item)) {
                    // Also by the net name it is placed with.
                    shown = shown
                            || library.symbols.value(name).power.contains(text, Qt::CaseInsensitive);
                } else {
                    const Part part = library.parts.value(name);
                    shown = shown || part.prefix.compare(text, Qt::CaseInsensitive) == 0
                            || part.prefixGost.compare(text, Qt::CaseInsensitive) == 0;
                }
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
        // The same for the section.
        const QSignalBlocker blocker(m_tree);
        section->setExpanded(text.isEmpty() ? !m_collapsedSections.contains(sectionKey(section))
                                            : sectionShown > 0);
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

const Library &LibraryPanel::currentLibrary() const
{
    return currentSource() == Source::Project ? m_project : m_library.items;
}

void LibraryPanel::showCurrentPart()
{
    // A power symbol has no footprints.
    const Part *part = currentIsPower() ? nullptr : currentLibrary().findPart(currentPart());
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
    const Symbol *symbol = nullptr;
    if (!part.isEmpty())
        symbol = currentIsPower() ? library.findSymbol(part) : library.findSymbolOfPart(part);
    m_preview->setContent(symbol, library.findFootprint(m_footprintBox->currentText()));
}

} // namespace ppcb
