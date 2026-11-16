#pragma once

#include "library.h"
#include "project.h"

#include <QWidget>

#include <functional>

class QComboBox;
class QMouseEvent;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace ppcb {

// A press of the left button and a move far enough, turned into the drag of
// a part out of the library panel. The drag is not Qt's drag and drop: that
// takes the keyboard while it lasts, and R and X must turn the part on the
// way. The widget keeps the mouse while the button is down, so the moves
// come here wherever the mouse goes.
struct PartDragGesture
{
    // Once the move is long enough: 'pressPos' is where the button went
    // down. False when there is nothing to drag there.
    std::function<bool(const QPoint &pressPos)> start;
    // Where the mouse is, and where it was let go, on the screen.
    std::function<void(const QPoint &globalPos)> moved;
    std::function<void(const QPoint &globalPos)> ended;

    void press(const QMouseEvent *event);
    // Both return true when the event belongs to a drag and is used up.
    bool move(QWidget *widget, const QMouseEvent *event);
    bool release(QWidget *widget, const QMouseEvent *event);

private:
    QPoint m_pressPos;
    bool m_pressed = false;
    bool m_dragging = false;
};

// The drawing of one part: its symbol above, one of its footprints below.
class PartPreview : public QWidget
{
    Q_OBJECT

public:
    explicit PartPreview(QWidget *parent = nullptr);

    // Null for what is not there; the pointers must stay valid while shown.
    void setContent(const Symbol *symbol, const Footprint *footprint);

    // Dragging the drawing drags the part shown.
    PartDragGesture drag;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    const Symbol *m_symbol = nullptr;
    const Footprint *m_footprint = nullptr;
};

// The parts of the library to choose from: a search field, the parts of the
// built-in and the user's library and those of the open project, each
// grouped by designator prefix after a group of power symbols, and the
// drawing of the chosen one.
class LibraryPanel : public QWidget
{
    Q_OBJECT

public:
    // Where a part in the list comes from.
    enum class Source { Library, Project };
    Q_ENUM(Source)

    explicit LibraryPanel(QWidget *parent = nullptr);

    // "Library": the panel's title and that of its first section.
    static QString libraryTitle();

    // Shows these libraries. 'style' picks the prefix the parts are grouped
    // by. Nothing is rebuilt when nothing changed, so the choice stays.
    void setLibraries(const MergedLibrary &library, const Library &project,
                      DesignatorStyle style);

    // Hides the parts whose name, file name or prefixes do not hold 'text'.
    void setFilter(const QString &text);

    // Opens the section of the chosen item, every section when none is
    // chosen, with all the groups in it that are shown.
    void expandChosenSection();
    // Closes every section and every group.
    // Without a search on, both stay as they are left when the list is
    // built again.
    void collapseAll();

    // The chosen part, or power symbol, and where it is from; an empty name
    // when none is.
    QString currentPart() const;
    Source currentSource() const;
    bool currentIsPower() const;
    // Chooses the project's part 'part' and, below the list, 'footprint'
    // when the part allows it. False when the project has no such part.
    bool showProjectPart(const QString &part, const QString &footprint);

    QTreeWidget *tree() const { return m_tree; }
    PartPreview *preview() const { return m_preview; }
    QComboBox *footprintBox() const { return m_footprintBox; }

signals:
    // The reload button was pressed: the libraries should be read again.
    void reloadRequested();
    // A part was double-clicked: it is to be placed on the sheet.
    void placeRequested(ppcb::LibraryPanel::Source source, const QString &part,
                        const QString &footprint);
    // The same for a power symbol, by the name of its symbol.
    void placePowerRequested(ppcb::LibraryPanel::Source source, const QString &symbol);
    // A part is dragged out of the list or the drawing: it is to be placed,
    // following the mouse, and put down where the button is let go.
    void dragStarted(ppcb::LibraryPanel::Source source, const QString &part,
                     const QString &footprint);
    void powerDragStarted(ppcb::LibraryPanel::Source source, const QString &symbol);
    void dragMoved(const QPoint &globalPos);
    void dragEnded(const QPoint &globalPos);

private:
    void rebuild();
    // The item of the part 'part' of 'source'; null when there is none.
    QTreeWidgetItem *findPartItem(Source source, const QString &part) const;
    // Asks for the part or the power symbol of 'item' to be placed, or
    // dragged when 'drag'; false when 'item' is neither.
    bool requestPlacing(const QTreeWidgetItem *item, bool drag);
    // Adds the power symbols of 'library' as the first group of 'section'.
    void addPowerGroup(QTreeWidgetItem *section, const Library &library, Source source);
    // Adds the parts of 'library' under a new top item 'title'.
    void addSection(const QString &title, const Library &library, Source source,
                    const QSet<QString> &replaced);
    void applyFilter();
    // Fills the footprint box for the chosen part and shows it.
    void showCurrentPart();
    void showCurrentFootprint();
    // The library the chosen part comes from.
    const Library &currentLibrary() const;
    // The footprint a part item is placed with: the one chosen below the
    // list for the current part, else the part's first.
    QString footprintFor(const QTreeWidgetItem *item) const;
    // Notes a section or a group the user opened or closed, while no search
    // is on.
    void noteExpanded(QTreeWidgetItem *item, bool expanded);
    // The key a group is remembered by: its section and its prefix.
    static QString groupKey(const QTreeWidgetItem *group);
    // The key a section is remembered by: its source.
    static QString sectionKey(const QTreeWidgetItem *section);

    MergedLibrary m_library;
    Library m_project;
    DesignatorStyle m_style = DesignatorStyle::International;
    bool m_hasLibraries = false;
    // The groups the user keeps open, by groupKey(); they stay open when
    // the list is built again and when a search ends.
    QSet<QString> m_expandedGroups;
    // The sections the user keeps closed, by sectionKey().
    QSet<QString> m_collapsedSections;

    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QComboBox *m_footprintBox = nullptr;
    PartPreview *m_preview = nullptr;
};

} // namespace ppcb
