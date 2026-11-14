#pragma once

#include "library.h"
#include "project.h"

#include <QWidget>

class QComboBox;
class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace ppcb {

// The drawing of one part: its symbol above, one of its footprints below.
class PartPreview : public QWidget
{
    Q_OBJECT

public:
    explicit PartPreview(QWidget *parent = nullptr);

    // Null for what is not there; the pointers must stay valid while shown.
    void setContent(const Symbol *symbol, const Footprint *footprint);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    const Symbol *m_symbol = nullptr;
    const Footprint *m_footprint = nullptr;
};

// The parts of the library to choose from: a search field, the parts of the
// built-in and the user's library and those of the open project, each
// grouped by designator prefix, and the drawing of the chosen one.
class LibraryPanel : public QWidget
{
    Q_OBJECT

public:
    // Where a part in the list comes from.
    enum class Source { Library, Project };

    explicit LibraryPanel(QWidget *parent = nullptr);

    // "Library": the panel's title and that of its first section.
    static QString libraryTitle();

    // Shows these libraries. 'style' picks the prefix the parts are grouped
    // by. Nothing is rebuilt when nothing changed, so the choice stays.
    void setLibraries(const MergedLibrary &library, const Library &project,
                      DesignatorStyle style);

    // Hides the parts whose name, file name or prefixes do not hold 'text'.
    void setFilter(const QString &text);

    // Opens or closes every group that is shown. Without a search on, the
    // groups stay so when the list is built again.
    void setAllGroupsExpanded(bool expanded);

    // The chosen part and where it is from; an empty name when none is.
    QString currentPart() const;
    Source currentSource() const;

    QTreeWidget *tree() const { return m_tree; }
    PartPreview *preview() const { return m_preview; }
    QComboBox *footprintBox() const { return m_footprintBox; }

signals:
    // The reload button was pressed: the libraries should be read again.
    void reloadRequested();

private:
    void rebuild();
    // Adds the parts of 'library' under a new top item 'title'.
    void addSection(const QString &title, const Library &library, Source source,
                    const QSet<QString> &replaced);
    void applyFilter();
    // Fills the footprint box for the chosen part and shows it.
    void showCurrentPart();
    void showCurrentFootprint();
    // The library the chosen part comes from.
    const Library &currentLibrary() const;
    // Notes a group the user opened or closed, while no search is on.
    void noteExpanded(QTreeWidgetItem *item, bool expanded);
    // The key a group is remembered by: its section and its prefix.
    static QString groupKey(const QTreeWidgetItem *group);

    MergedLibrary m_library;
    Library m_project;
    DesignatorStyle m_style = DesignatorStyle::International;
    bool m_hasLibraries = false;
    // The groups the user keeps open, by groupKey(); they stay open when
    // the list is built again and when a search ends.
    QSet<QString> m_expandedGroups;

    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QComboBox *m_footprintBox = nullptr;
    PartPreview *m_preview = nullptr;
};

} // namespace ppcb
