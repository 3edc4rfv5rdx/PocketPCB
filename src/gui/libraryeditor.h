#pragma once

#include "library.h"

#include <QMainWindow>

#include <functional>

class QLabel;
class QMenu;
class QStackedWidget;
class QTabWidget;

namespace ppcb {

class FootprintEditor;
class ItemEditor;
class SymbolEditor;

// The one window of the library editors: each item of the library open in
// it — a footprint or a symbol — is a tab of its own, with its own strip of
// tools and its own Undo. Over the tabs, on the right: + for a new
// footprint or symbol, Open… for one of the library, Copy for a copy of the
// one shown. With no tab, which hides those, the window shows the first two
// in its middle. It closes with its last tab.
class LibraryEditor : public QMainWindow
{
    Q_OBJECT

public:
    explicit LibraryEditor(QWidget *parent = nullptr);

    // "Library editor".
    static QString libraryEditorTitle();

    // Opens the footprint 'name' (see ItemEditor) in a new tab, or shows
    // the tab it is open in already; a new footprint for an empty name,
    // always in a new tab.
    FootprintEditor *openFootprint(const QString &name, const Footprint &footprint,
                                   bool inUserLibrary);
    // The same for a symbol.
    SymbolEditor *openSymbol(const QString &name, const Symbol &symbol, bool inUserLibrary);
    QTabWidget *tabs() const { return m_tabs; }
    // The editor of the tab shown; null when there is none.
    ItemEditor *currentEditor() const;
    // The same when it edits a footprint.
    FootprintEditor *currentFootprintEditor() const;
    // The library Open… offers; it must outlive the window, or be set
    // again before it goes.
    void setLibrary(const Library *library) { m_library = library; }
    // The least copper between pads, of the open project's rules, for every
    // footprint tab (FootprintEditor::setClearance()).
    void setClearance(Coord clearance);
    // A new footprint, or symbol, in a new tab.
    FootprintEditor *openNewFootprint();
    SymbolEditor *openNewSymbol();
    // A new symbol in a new tab, begun as 'symbol' (a ready-made one), not
    // saved yet.
    SymbolEditor *openSymbolFrom(const Symbol &symbol);
    // The items of a New symbol from menu in 'menu': the ready-made pieces
    // and a chip asked for, each in a new tab of the window 'window' gives
    // when one is chosen.
    static void addNewSymbolFromActions(QMenu *menu,
                                        const std::function<LibraryEditor *()> &window);
    // A copy of the item shown in a new tab, with no name yet; null when no
    // tab is shown.
    ItemEditor *openCopy();
    // Asks which footprint or symbol of the library to open, from a list
    // with a search field and a switch between the two, and opens it; null
    // when none is chosen.
    ItemEditor *askOpen();
    // Closes tab 'index' once its changes are saved or dropped; false when
    // the user stays.
    bool closeTab(int index);

signals:
    // An item was written into the user's library.
    void saved();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    // 'editor' in a new tab, shown, its signals heard.
    void addEditorTab(ItemEditor *editor);
    void tabShown(int index);
    // The tabs, or the buttons of an empty window when there are none.
    void showTabsOrStart();
    void updateTab(QWidget *tab);

    const Library *m_library = nullptr;
    Coord m_clearance = 0;
    // What Open… lists: symbols rather than footprints.
    bool m_openSymbols = false;
    QStackedWidget *m_pages = nullptr;
    QTabWidget *m_tabs = nullptr;
    QWidget *m_start = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_position = nullptr;
};

} // namespace ppcb
