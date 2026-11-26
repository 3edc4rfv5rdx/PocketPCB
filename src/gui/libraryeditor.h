#pragma once

#include "library.h"

#include <QMainWindow>

class QLabel;
class QStackedWidget;
class QTabWidget;

namespace ppcb {

class FootprintEditor;

// The one window of the library editors: each item of the library open in
// it is a tab of its own, with its own strip of tools and its own Undo. Over
// the tabs, on the right: + for a new footprint, Open… for one of the
// library, Copy for a copy of the one shown. With no tab, which hides
// those, the window shows the first two in its middle. It closes with its
// last tab.
class LibraryEditor : public QMainWindow
{
    Q_OBJECT

public:
    explicit LibraryEditor(QWidget *parent = nullptr);

    // "Library editor".
    static QString libraryEditorTitle();

    // Opens the footprint 'name' (see FootprintEditor) in a new tab, or
    // shows the tab it is open in already; a new footprint for an empty
    // name, always in a new tab.
    FootprintEditor *openFootprint(const QString &name, const Footprint &footprint,
                                   bool inUserLibrary);
    QTabWidget *tabs() const { return m_tabs; }
    // The footprint editor of the tab shown; null when there is none.
    FootprintEditor *currentFootprintEditor() const;
    // The library Open… offers; it must outlive the window, or be set
    // again before it goes.
    void setLibrary(const Library *library) { m_library = library; }
    // The least copper between pads, of the open project's rules, for every
    // tab (FootprintEditor::setClearance()).
    void setClearance(Coord clearance);
    // A new footprint in a new tab.
    FootprintEditor *openNewFootprint();
    // A copy of the footprint shown in a new tab, with no name yet; null
    // when no footprint is shown.
    FootprintEditor *openCopy();
    // Asks which footprint of the library to open, from a list with a
    // search field, and opens it; null when none is chosen.
    FootprintEditor *askOpen();
    // Closes tab 'index' once its changes are saved or dropped; false when
    // the user stays.
    bool closeTab(int index);

signals:
    // An item was written into the user's library.
    void saved();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void tabShown(int index);
    // The tabs, or the buttons of an empty window when there are none.
    void showTabsOrStart();
    void updateTab(QWidget *tab);

    const Library *m_library = nullptr;
    Coord m_clearance = 0;
    QStackedWidget *m_pages = nullptr;
    QTabWidget *m_tabs = nullptr;
    QWidget *m_start = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_position = nullptr;
};

} // namespace ppcb
