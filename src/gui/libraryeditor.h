#pragma once

#include "library.h"
#include "itemchooser.h"
#include "libraryfile.h"

#include <QMainWindow>

#include <functional>

class QLabel;
class QMenu;
class QStackedWidget;
class QTabWidget;

namespace ppcb {

class FootprintEditor;
class ItemEditor;
class LibraryTab;
class PartEditor;
class SymbolEditor;

// The one window of the library editors: each item of the library open in
// it — a footprint, a symbol or a part — is a tab of its own, with its own
// strip of tools and its own Undo. Over the tabs, on the right: + for a new
// footprint, symbol or part, Open… for one of the library, Copy for a copy
// of the one shown. With no tab, which hides those, the window shows the
// first two in its middle. It closes with its last tab.
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
    // The same for a symbol, and for a part.
    SymbolEditor *openSymbol(const QString &name, const Symbol &symbol, bool inUserLibrary);
    PartEditor *openPart(const QString &name, const Part &part, bool inUserLibrary);
    QTabWidget *tabs() const { return m_tabs; }
    // The editor of the tab shown; null when there is none.
    ItemEditor *currentEditor() const;
    // The same when it edits a footprint.
    FootprintEditor *currentFootprintEditor() const;
    // The library Open… offers and the part tabs read; it must outlive the
    // window, or be set again before it goes. Set again when it changed.
    void setLibrary(const Library *library);
    // The least copper between pads, of the open project's rules, for every
    // footprint tab (FootprintEditor::setClearance()).
    void setClearance(Coord clearance);
    // A new footprint, symbol or part in a new tab.
    FootprintEditor *openNewFootprint();
    SymbolEditor *openNewSymbol();
    PartEditor *openNewPart();
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
    LibraryTab *openCopy();
    // Asks which footprint, symbol or part of the library to open
    // (ItemChooser), of all, of the built-in library or of the user's, and
    // opens it; null when none is chosen. Items of the user's can be
    // deleted there (deleteFromLibrary()).
    LibraryTab *askOpen();
    // Deletes the item 'name' of 'kind' from the user's library once the
    // user says so, told which parts list it and whether a built-in one of
    // that name is seen again; its tab closes first, asking about unsaved
    // changes. False when it is not deleted.
    bool deleteFromLibrary(LibraryKind kind, const QString &name);
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
    void addEditorTab(LibraryTab *editor);
    void tabShown(int index);
    // The tabs, or the buttons of an empty window when there are none.
    void showTabsOrStart();
    void updateTab(QWidget *tab);
    // The kind of item 'tab' edits.
    static LibraryKind tabKind(const QWidget *tab);

    const Library *m_library = nullptr;
    Coord m_clearance = 0;
    // What Open… lists.
    LibraryKind m_openKind = LibraryKind::Symbol;
    ItemChooser::Source m_openSource = ItemChooser::Source::All;
    QStackedWidget *m_pages = nullptr;
    QTabWidget *m_tabs = nullptr;
    QWidget *m_start = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_position = nullptr;
};

} // namespace ppcb
