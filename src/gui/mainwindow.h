#pragma once

#include "appsettings.h"
#include "library.h"
#include "librarypanel.h"
#include "projectview.h"
#include "units.h"

#include <QHash>
#include <QMainWindow>
#include <QTemporaryDir>

#include <memory>

class QActionGroup;
class QFileSystemWatcher;
class QTimer;
class QComboBox;
class QDockWidget;
class QLabel;
class QPushButton;
class QToolButton;
class QMenu;

namespace ppcb {

class BoardEditor;
class BomPanel;
class DrcPanel;
class VersionsPanel;
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
    // Opens the first of the recent projects; says so in the status bar
    // when there is none.
    void openLastProject();
    // Shows version 'number', the archive 'path', unpacked into a folder of
    // the window's own, for reading only (Document::setReadOnly()), without
    // making it a recent project. False, with the reason in the status bar,
    // when it cannot be read.
    bool showVersionCopy(int number, const QString &path);
    // Saves the project into the new folder 'name' inside 'parentFolder'
    // (see Document::saveAsNewFolder()).
    bool saveProjectAs(const QString &parentFolder, const QString &name);
    // Asks what to do with unsaved changes before the project is closed,
    // replaced by a new one or by another one opened: save them, drop them
    // or stay. True when the project may go; always true without changes.
    bool confirmLeavingProject();
    SheetEditor *sheetEditor() const { return m_sheetEditor; }
    BoardEditor *boardEditor() const { return m_boardEditor; }
    // Starts placing a part from the panel on the sheet shown. False, with
    // the reason in the status bar, when no sheet is shown.
    bool placePart(LibraryPanel::Source source, const QString &part, const QString &footprint);
    // The same for a power symbol; it becomes the one P places.
    bool placePower(LibraryPanel::Source source, const QString &symbol);
    // P and the Power button: places again the power symbol placed last,
    // the chassis at first.
    bool placeLastPower();
    // The menu of the Power button's arrow, filled as it opens: the power
    // symbols with their drawings, the one P places ticked; the one chosen
    // is placed and becomes it.
    void fillPowerMenu(QMenu *menu);
    // The diameters of the drill set of the project's rules, the one chosen
    // checked, and Other… for a diameter typed. Choosing one places holes of
    // it; with 'chosenHoles' it is given to the holes chosen instead.
    void fillHoleMenu(QMenu *menu, bool chosenHoles);
    // "No net", then the nets of the board, those of the power class first,
    // the one of the holes chosen checked: choosing one gives it to their
    // pads.
    void fillPadNetMenu(QMenu *menu);
    // The Power button shows the power symbol P places; its own picture
    // while that symbol cannot be found.
    void updatePowerIcon();
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
    // The menu of the board at 'globalPos': for a part, a text or the
    // outline; none on the empty board yet.
    void showBoardMenu(const QPoint &globalPos);

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
    // The strip of tools over the canvas (see updateTools()): the menu, save,
    // undo and redo, the scale, the modes of the sheet in the middle and the
    // switches of the panels on the right. F10 opens the menu.
    void createToolBar();
    // The mode buttons show what the sheet or the board editor does now:
    // those of the sheet while a sheet is edited, those of the board while
    // the board is. The tools of each work only there.
    void updateTools();
    // Esc and the Select button go to the editor of the page shown.
    void cancelEditing();
    void chooseSelectTool();
    // Asks for the size of a rectangular board and makes the outline one.
    void makeRectangleBoard();
    // Runs the autorouter over every net, or over the nets of what is chosen,
    // as one step; Esc or Stop in its progress box stops it.
    void routeBoard(bool chosen);
    // The whole page, or the library file, fills the canvas.
    void fitView();
    // What is drawn on the page, without the empty paper round it, or the
    // library file, fills the canvas.
    void fitDrawing();
    // The canvas shows millimetres as millimetres on the screen, as far as
    // the screen tells its size.
    void showActualSize();
    // Help > About: the name and version of the program, the day of its
    // build, what it is, its license.
    void showAbout();
    // Opens help.html (AppSettings::helpFile()) in the browser.
    void showUserGuide();
    static QString aboutTitle();
    void createStatusBar();
    // Adds a checkable, mutually exclusive item to a menu.
    QAction *addChoice(QMenu *menu, QActionGroup *group, const QString &text, bool checked);

    void applyGridStyle(GridStyle style);
    void applyCursorStyle(CursorStyle style);
    // Keeps 'color' as the paper of the sheets and shows it.
    void applySheetBackground(const QColor &color);
    // The canvas takes the paper of the sheets when a sheet is shown, black
    // under the board, white under a library file.
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
    void chooseScreenSize();
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
    // The name of the command that opens the last project.
    static QString lastProjectTitle();
    // Fills 'menu' with the recent projects and Clear list; with none, a
    // line that says so.
    void fillRecentMenu(QMenu *menu);
    // Opens a project from that menu; one that is gone leaves the list.
    void openRecentProject(const QString &path);
    // Opens 'path' once the open project may be left, and remembers its folder.
    void leaveAndOpenProject(const QString &path);
    // Both return true when the project was saved.
    bool saveProject();
    bool chooseProjectToSaveAs();
    // Writes 'revision' into the title block as one step, when it is
    // another.
    void setRevision(const QString &revision);
    // File > Export Gerber and drill files: asks the revision and writes it
    // into the project (setRevision()), so that the title block and the
    // archive's name agree; saves the project, writes the files and keeps a
    // version when the project changed since the last one. A box says it
    // is done, offering to start the next revision, or what went wrong.
    void exportGerberFiles();
    // File > Save version: asks what changed, saves the project when it is
    // not saved, and makes its next version (saveProjectVersion()). True
    // when the version was made.
    bool saveVersion();
    // Puts version 'number', the archive 'path', in place of the design as
    // one step, asking first whether to keep what changed since the newest
    // version as a version; written to disk only by saving.
    void restoreVersion(int number, const QString &path);
    // Opens version 'number' in a new window for reading (showVersionCopy()).
    void openVersionCopy(int number, const QString &path);
    // File > Update parts from library: the parts chosen in the dialog
    // (partDifferences(), withPartsUpdated()) as one step.
    void updatePartsFromLibrary();
    // File > Update board from schematic (boardUpdatedFromSchematic()):
    // the changes are shown first and made as one step, then the board.
    void updateBoardFromSchematic();
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
    // Also colours the background of the Save button.
    void updateSavedMark();
    // The connections still to be routed and all there are, in the status
    // bar while the board is shown.
    void updateUnrouted();
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
    Document *m_document = nullptr;
    // Owned by the canvas scene.
    DesignViewItem *m_designItem = nullptr;
    SheetEditor *m_sheetEditor = nullptr;
    BoardEditor *m_boardEditor = nullptr;
    // The view is fitted to the page after the window is first shown.
    bool m_shownOnce = false;
    // The folder of the copy of a version shown, and its name for the title.
    std::unique_ptr<QTemporaryDir> m_versionFolder;
    QString m_versionTitle;
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
    QString m_lastPowerSymbol = QStringLiteral("chassis");
    LibraryPanel *m_libraryPanel = nullptr;
    QDockWidget *m_libraryDock = nullptr;
    PropertiesPanel *m_propertiesPanel = nullptr;
    QDockWidget *m_propertiesDock = nullptr;
    NetsPanel *m_netsPanel = nullptr;
    QDockWidget *m_netsDock = nullptr;
    ErcPanel *m_ercPanel = nullptr;
    QDockWidget *m_ercDock = nullptr;
    DrcPanel *m_drcPanel = nullptr;
    QDockWidget *m_drcDock = nullptr;
    BomPanel *m_bomPanel = nullptr;
    QDockWidget *m_bomDock = nullptr;
    VersionsPanel *m_versionsPanel = nullptr;
    QDockWidget *m_versionsDock = nullptr;
    // File, Edit, View and Settings, behind the menu button: there is no
    // menu bar.
    QMenu *m_mainMenu = nullptr;
    // The mode buttons of the strip of tools, by the tool each stands for.
    QHash<int, QAction *> m_toolActions;
    // The actions that work only while a sheet is edited; the Select button
    // is not among them.
    QList<QAction *> m_sheetActions;
    // The mode buttons of the board, by the tool each stands for, and the
    // actions that work only while the board is edited.
    QHash<int, QAction *> m_boardToolActions;
    QList<QAction *> m_boardActions;
    // The one button of the outline tools: it shows the mode on, else the
    // tool chosen last from its arrow.
    QToolButton *m_outlineButton = nullptr;
    QToolButton *m_saveButton = nullptr;
    QLabel *m_unroutedLabel = nullptr;
    // The band under the canvas of a copy of a version.
    QLabel *m_versionBand = nullptr;
    // The actions of the outline's button, stopped while it is locked.
    QList<QAction *> m_outlineActions;

    QFileSystemWatcher *m_libraryWatcher = nullptr;
    // Delays the reload a little: editors often save in several steps.
    QTimer *m_reloadTimer = nullptr;
};

} // namespace ppcb
