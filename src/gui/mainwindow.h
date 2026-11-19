#pragma once

#include "appsettings.h"
#include "library.h"
#include "librarypanel.h"
#include "projectview.h"
#include "units.h"

#include <QMainWindow>

class QActionGroup;
class QFileSystemWatcher;
class QTimer;
class QComboBox;
class QDockWidget;
class QLabel;
class QPushButton;
class QMenu;
class QStackedWidget;

namespace ppcb {

class BomPanel;
class CanvasView;
class DesignViewItem;
class ErcPanel;
class Document;
struct Design;
class LibraryPanel;
class NetsPanel;
class PropertiesPanel;
class SheetEditor;
class SheetTabs;
class LibraryViewItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    CanvasView *canvas() const { return m_canvas; }

    // The open project. The window starts with a new one; its pages (the
    // sheets, then the board) are the tabs over the canvas.
    Document *document() const { return m_document; }
    DesignViewItem *designItem() const { return m_designItem; }
    void newProject();
    // Both return false, with the reason in the status bar, on failure; what
    // was open before stays.
    bool openProject(const QString &path);
    // Saves the project into the new folder 'name' inside 'parentFolder'
    // (see Document::saveAsNewFolder()).
    bool saveProjectAs(const QString &parentFolder, const QString &name);
    // Asks what to do with unsaved changes before the project is closed,
    // replaced by a new one or by another one opened: save them, drop them
    // or stay. True when the project may go; always true without changes.
    bool confirmLeavingProject();
    SheetEditor *sheetEditor() const { return m_sheetEditor; }
    // Starts placing a part from the panel on the sheet shown. False, with
    // the reason in the status bar, when no sheet is shown.
    bool placePart(LibraryPanel::Source source, const QString &part, const QString &footprint);
    // The same for a power symbol; it becomes the one P places.
    bool placePower(LibraryPanel::Source source, const QString &symbol);
    // Places again the power symbol placed last, the ground at first.
    bool placeLastPower();
    // Starts placing labels (see SheetEditor::startPlacingLabel()).
    bool placeLabel();
    // Starts placing no-connect marks (see SheetEditor::startPlacingNoConnect()).
    bool placeNoConnect();
    // Shows page 'page' of the project and, when 'fit', the view it was last
    // seen with, or fits the view to it when it has none yet.
    void showPage(int page, bool fit = true);
    // The menu of the sheet shown, for a short right click at 'globalPos':
    // what is chosen can be turned, mirrored, deleted, copied, cut or
    // duplicated, and the text of what is under the cursor edited in place;
    // with nothing chosen, things are pasted, drawn and placed.
    void showSheetMenu(const QPoint &globalPos);

    // Shows a symbol or footprint file of the library on the canvas in the
    // place of the project and keeps watching it: the drawing follows every
    // save of the file. The project comes back with newProject(),
    // openProject() or a click on a tab.
    // Returns false, with the reason in the status bar, when the file cannot
    // be read; what was shown before stays.
    bool openLibraryFile(const QString &path);
    // Reads the shown file again. An unreadable file keeps the last drawing
    // on screen and puts the reason into the status bar.
    bool reloadLibraryFile();
    // Path of the shown library file; empty when there is none.
    QString libraryFilePath() const { return m_libraryPath; }
    LibraryViewItem *libraryItem() const { return m_libraryItem; }

    LibraryPanel *libraryPanel() const { return m_libraryPanel; }

    // The message last put into the status bar, beside the saved mark.
    QString statusText() const;

    // The built-in library with the user's over it, as last loaded.
    const MergedLibrary &library() const { return m_library; }
    // Reads both libraries again. What cannot be read is told in the status
    // bar and left out.
    void loadLibrary();

protected:
    void closeEvent(QCloseEvent *event) override;
    // The swatches of the paper's colours: the mouse over one previews it.
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void createMenus();
    void createStatusBar();
    // Adds a checkable, mutually exclusive item to a menu.
    QAction *addChoice(QMenu *menu, QActionGroup *group, const QString &text, bool checked);

    void applyGridStyle(GridStyle style);
    void applyCursorStyle(CursorStyle style);
    // Keeps 'color' as the paper of the sheets and shows it.
    void applySheetBackground(const QColor &color);
    // The canvas takes the paper of the sheets when a sheet is shown, white
    // under the board and a library file.
    void updateCanvasBackground();
    // The sheet shown, if one is, takes 'color' until the next
    // updateCanvasBackground().
    void previewSheetBackground(const QColor &color);
    // A sheet of the project is on the canvas, not the board or a library file.
    bool isSheetShown() const;
    void applyZoomCenterAndWarp(bool on);
    void applyWireRouting(WireRouting routing);
    // Asks for the common folder of the project folders.
    void chooseProjectsFolder();
    // Keeps 'language' (a code, or empty for the system's) for the next
    // start, and says so.
    void setInterfaceLanguage(const QString &language);

    void autosaveProject();
    // After a project is opened: offers to take back the changes of a
    // session that did not end with a save, or drops them.
    void offerAutosave();
    void chooseNewProject();
    void chooseProjectToOpen();
    // Fills the menu of recent projects from the settings.
    void updateRecentMenu();
    // Opens a project from that menu; one that is gone leaves the list.
    void openRecentProject(const QString &path);
    // Opens 'path' once the open project may be left, and remembers its folder.
    void leaveAndOpenProject(const QString &path);
    // Both return true when the project was saved.
    bool saveProject();
    bool chooseProjectToSaveAs();
    // File > Project properties: what the dialog gives is one step.
    void editProjectProperties();
    // Puts the project on the canvas in the place of a library file.
    void showProject();
    // The key of page 'page' among the views: a sheet's id, or the board's key.
    QString pageViewKey(int page) const;
    // Keeps how the project page shown is looked at; nothing when a library
    // file is shown instead.
    void rememberView();
    // Shows the page as it was last looked at, or fitted when never yet.
    void showPageView();
    // Keeps the view shown and writes the views beside the project file,
    // when the project has one.
    void storeViews();
    // Brings the tabs and the drawing in line with the project's content.
    void projectChanged();
    // After a change of the content: the page shown stays shown when its
    // sheet has moved; when the sheet is gone, the one in its place is.
    void keepShownPage();
    void updateTitle();
    // The mark in the bottom left corner: saved, not saved, or new.
    void updateSavedMark();
    // Puts 'text' into the status bar until the next one. Not
    // QStatusBar::showMessage(), which would hide the saved mark.
    void showStatus(const QString &text);

    void chooseLibraryFile();
    void openLibraryFolder();
    // Shows the loaded libraries and the open project's in the panel.
    void updateLibraryPanel();
    // True when a sheet is shown to place on; else says so in the status bar.
    bool canPlace();
    // The library a part or power symbol of the panel is taken from.
    const Library &sourceLibrary(LibraryPanel::Source source) const;
    // Reads 'path' and puts it on the canvas; the common part of opening
    // and reloading.
    bool showLibraryFile(const QString &path);
    void watchLibraryFile();

    // The BOM takes the place of the canvas; the sheet editor rests, so that
    // its keys do nothing to a sheet not shown.
    void showBom();
    // The canvas comes back in its place.
    void showCanvas();
    // View > BOM: the tab of the BOM is added and shown, or removed, the
    // page under it shown again; the choice is kept in the settings.
    void setBomTabShown(bool shown);
    // Shows the sheet of the first of 'ids' with those of them on it chosen.
    void showComponents(const QSet<ObjectId> &ids);
    // Undo or Redo of a step: its page is shown and what it touched chosen.
    void showStep(int page, const QSet<ObjectId> &ids);
    // A new step: written into the history of steps when it is kept.
    void recordStep(const QString &text, int page, const Design &before);
    // The canvas takes the grid step the project keeps for the page shown,
    // and a step chosen by the user goes into the project.
    void applyProjectGrid();
    void storeGridStep(Coord step);
    // "2.54 mm": a grid step in the status bar and in a step's name.
    static QString gridStepText(Coord step);

    void showCursorPosition(Coord x, Coord y);
    void showGridStep(Coord step);

    CanvasView *m_canvas = nullptr;
    SheetTabs *m_sheetTabs = nullptr;
    // Under the tabs: the canvas, or the BOM while its tab is chosen.
    QStackedWidget *m_pages = nullptr;
    BomPanel *m_bomPanel = nullptr;
    bool m_bomShown = false;
    Document *m_document = nullptr;
    // Owned by the canvas scene.
    DesignViewItem *m_designItem = nullptr;
    SheetEditor *m_sheetEditor = nullptr;
    // The view is fitted to the page after the window is first shown.
    bool m_shownOnce = false;
    QLabel *m_positionLabel = nullptr;
    QPushButton *m_savedMark = nullptr;
    QLabel *m_statusText = nullptr;
    QComboBox *m_gridStepBox = nullptr;
    QMenu *m_recentMenu = nullptr;

    QString m_libraryPath;
    // Owned by the canvas scene.
    LibraryViewItem *m_libraryItem = nullptr;
    QTimer *m_autosaveTimer = nullptr;

    // How the pages of the open project were last looked at, and the key of
    // the page shown; none right after another project came, whose view is
    // not this one's.
    ProjectView m_views;
    QString m_shownViewKey;

    MergedLibrary m_library;
    // The power symbol P places: where it was taken from, and its name.
    LibraryPanel::Source m_lastPowerSource = LibraryPanel::Source::Library;
    QString m_lastPowerSymbol = QStringLiteral("ground");
    LibraryPanel *m_libraryPanel = nullptr;
    QDockWidget *m_libraryDock = nullptr;
    PropertiesPanel *m_propertiesPanel = nullptr;
    QDockWidget *m_propertiesDock = nullptr;
    NetsPanel *m_netsPanel = nullptr;
    QDockWidget *m_netsDock = nullptr;
    ErcPanel *m_ercPanel = nullptr;
    QDockWidget *m_ercDock = nullptr;

    QFileSystemWatcher *m_libraryWatcher = nullptr;
    // Delays the reload a little: editors often save in several steps.
    QTimer *m_reloadTimer = nullptr;
};

} // namespace ppcb
