#include "mainwindow.h"

#include "canvasview.h"
#include "designviewitem.h"
#include "document.h"
#include "format.h"
#include "grid.h"
#include "librarypanel.h"
#include "propertiespanel.h"
#include "sheeteditor.h"
#include "libraryviewitem.h"
#include "saveprojectdialog.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGraphicsScene>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

namespace ppcb {

namespace {

const int DEFAULT_WIDTH = 1100;
const int DEFAULT_HEIGHT = 750;
// Wait after a change of the watched file before reading it, milliseconds.
const int RELOAD_DELAY_MS = 150;
const int MS_PER_MINUTE = 60 * 1000;

const char APP_TITLE[] = "PocketPCB";

// The saved mark: white bold text on soft green, red or grey.
const char SAVED_MARK_STYLE[] =
    "QPushButton { color: white; font-weight: bold; padding: 0px 6px; border: none;"
    " border-radius: 2px; background: %1; }";
const char SAVED_COLOR[] = "#5f9462";
const char NOT_SAVED_COLOR[] = "#c25450";
const char NEW_COLOR[] = "#8c8c8c";

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_canvas = new CanvasView(this);
    m_canvas->setGridStyle(AppSettings::gridStyle());
    m_canvas->setCursorStyle(AppSettings::cursorStyle());
    m_canvas->setZoomCenterAndWarp(AppSettings::zoomCenterAndWarp());

    // The pages of the project as tabs over the canvas.
    m_tabs = new QTabBar(this);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_tabs);
    layout->addWidget(m_canvas);
    setCentralWidget(central);

    m_document = new Document(this);
    m_document->setBackupCount(AppSettings::backupCount());
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosaveProject);
    if (AppSettings::autosaveMinutes() > 0)
        m_autosaveTimer->start(AppSettings::autosaveMinutes() * MS_PER_MINUTE);
    m_designItem = new DesignViewItem(m_document, m_canvas->scene()->sceneRect());
    m_canvas->scene()->addItem(m_designItem);
    m_sheetEditor = new SheetEditor(m_document, m_canvas, this);
    m_designItem->setEditor(m_sheetEditor);
    connect(m_sheetEditor, &SheetEditor::changed, this, [this] { m_designItem->update(); });
    connect(m_document, &Document::stepApplied, this, &MainWindow::showStep);
    // The part being placed follows the cursor.
    connect(m_canvas, &CanvasView::cursorMoved, m_sheetEditor,
            [this](Coord x, Coord y) { m_sheetEditor->hover(Point{x, y}); });
    // A short right click ends placing and drawing wires, as Esc does.
    connect(m_canvas, &CanvasView::contextMenuRequested, m_sheetEditor, [this] {
        if (m_sheetEditor->isPlacing() || m_sheetEditor->isWiring())
            m_sheetEditor->cancel();
    });
    connect(m_document, &Document::designChanged, this, &MainWindow::projectChanged);
    connect(m_document, &Document::modifiedChanged, this, &MainWindow::updateTitle);
    // 'currentChanged' covers the keyboard; 'tabBarClicked' adds the click
    // on the current tab, which brings the project back from a library file.
    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index >= 0)
            showPage(index);
    });
    connect(m_tabs, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index >= 0)
            showPage(index);
    });

    m_libraryWatcher = new QFileSystemWatcher(this);
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(RELOAD_DELAY_MS);
    connect(m_libraryWatcher, &QFileSystemWatcher::fileChanged, m_reloadTimer,
            qOverload<>(&QTimer::start));
    connect(m_reloadTimer, &QTimer::timeout, this, &MainWindow::reloadLibraryFile);

    m_libraryPanel = new LibraryPanel(this);
    m_libraryDock = new QDockWidget(LibraryPanel::libraryTitle(), this);
    // The name keeps the dock's place in the saved window state.
    m_libraryDock->setObjectName(QStringLiteral("libraryDock"));
    m_libraryDock->setWidget(m_libraryPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_libraryDock);
    // The properties of what is chosen, under the library.
    m_propertiesPanel = new PropertiesPanel(m_document, m_sheetEditor, this);
    m_propertiesDock = new QDockWidget(PropertiesPanel::propertiesTitle(), this);
    m_propertiesDock->setObjectName(QStringLiteral("propertiesDock"));
    m_propertiesDock->setWidget(m_propertiesPanel);
    splitDockWidget(m_libraryDock, m_propertiesDock, Qt::Vertical);
    connect(m_libraryPanel, &LibraryPanel::reloadRequested, this, &MainWindow::loadLibrary);
    connect(m_libraryPanel, &LibraryPanel::placeRequested, this, &MainWindow::placePart);
    // A component double-clicked on the sheet is shown in the panel: its
    // part, with its footprint below the list.
    connect(m_sheetEditor, &SheetEditor::componentOpened, m_libraryPanel,
            &LibraryPanel::showProjectPart);
    // A part dragged out of the panel is placed the same way: it follows the
    // mouse, R and X turn it, and letting go over the sheet puts it down and
    // placing goes on; letting go anywhere else calls it off.
    connect(m_libraryPanel, &LibraryPanel::dragStarted, this, &MainWindow::placePart);
    connect(m_libraryPanel, &LibraryPanel::dragMoved, this, [this](const QPoint &at) {
        if (m_sheetEditor->isPlacing())
            m_canvas->followGlobalPos(at);
    });
    connect(m_libraryPanel, &LibraryPanel::dragEnded, this, [this](const QPoint &at) {
        if (!m_sheetEditor->isPlacing())
            return;
        QPointF scenePos;
        if (m_canvas->sceneAtGlobal(at, scenePos))
            m_sheetEditor->press(scenePos, false);
        else
            m_sheetEditor->stopPlacing();
    });

    createMenus();
    createStatusBar();
    loadLibrary();
    projectChanged();
    updateTitle();

    resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    // Both do nothing when given data that was never saved.
    restoreGeometry(AppSettings::windowGeometry());
    restoreState(AppSettings::windowState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!confirmLeavingProject()) {
        event->ignore();
        return;
    }
    storeViews();
    AppSettings::setWindowGeometry(saveGeometry());
    AppSettings::setWindowState(saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_shownOnce)
        return;
    m_shownOnce = true;
    // Once the window has been laid out and the canvas knows its size.
    QTimer::singleShot(0, this, [this] {
        if (m_designItem->isVisible())
            showPageView();
    });
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));

    QAction *newAction = fileMenu->addAction(tr("&New project"));
    newAction->setObjectName(QStringLiteral("actionNewProject"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::chooseNewProject);

    QAction *openAction = fileMenu->addAction(tr("&Open project") + QStringLiteral("…"));
    openAction->setObjectName(QStringLiteral("actionOpenProject"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseProjectToOpen);

    m_recentMenu = fileMenu->addMenu(tr("Recent projects"));
    m_recentMenu->setObjectName(QStringLiteral("menuRecentProjects"));
    m_recentMenu->setToolTipsVisible(true);
    // The list may change in another window of the program as well.
    connect(fileMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentMenu);
    updateRecentMenu();

    QAction *saveAction = fileMenu->addAction(tr("&Save"));
    saveAction->setObjectName(QStringLiteral("actionSaveProject"));
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::saveProject);

    QAction *saveAsAction = fileMenu->addAction(tr("Save &as") + QStringLiteral("…"));
    saveAsAction->setObjectName(QStringLiteral("actionSaveProjectAs"));
    saveAsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::chooseProjectToSaveAs);
    fileMenu->addSeparator();

    QAction *openLibraryAction = fileMenu->addAction(tr("Open library file") + QStringLiteral("…"));
    openLibraryAction->setObjectName(QStringLiteral("actionOpenLibraryFile"));
    openLibraryAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_L));
    connect(openLibraryAction, &QAction::triggered, this, &MainWindow::chooseLibraryFile);
    QAction *libraryFolderAction = fileMenu->addAction(tr("Open library folder"));
    libraryFolderAction->setObjectName(QStringLiteral("actionOpenLibraryFolder"));
    connect(libraryFolderAction, &QAction::triggered, this, &MainWindow::openLibraryFolder);
    fileMenu->addSeparator();

    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setObjectName(QStringLiteral("actionQuit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    // The stack's own actions: they carry the name of the step ("Undo Move")
    // and switch themselves off when there is nothing to undo.
    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *undoAction = m_document->undoStack()->createUndoAction(this, tr("Undo"));
    undoAction->setObjectName(QStringLiteral("actionUndo"));
    undoAction->setShortcut(QKeySequence::Undo);
    editMenu->addAction(undoAction);
    QAction *redoAction = m_document->undoStack()->createRedoAction(this, tr("Redo"));
    redoAction->setObjectName(QStringLiteral("actionRedo"));
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();

    const auto addEditAction = [this, editMenu](const QString &text, const char *name,
                                                const QKeySequence &key, auto slot) {
        QAction *action = editMenu->addAction(text);
        action->setObjectName(QLatin1String(name));
        action->setShortcut(key);
        connect(action, &QAction::triggered, m_sheetEditor, slot);
    };
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Copy), "actionCopy",
                  QKeySequence::Copy, &SheetEditor::copySelection);
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Cut), "actionCut",
                  QKeySequence::Cut, &SheetEditor::cutSelection);
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Paste), "actionPaste",
                  QKeySequence::Paste, &SheetEditor::paste);
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Duplicate), "actionDuplicate",
                  QKeySequence(Qt::CTRL | Qt::Key_D), &SheetEditor::duplicateSelection);
    editMenu->addSeparator();
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Wire), "actionWire",
                  QKeySequence(Qt::Key_W), &SheetEditor::startWiring);
    addEditAction(tr("Turn the wire corner the other way"), "actionWireCorner",
                  QKeySequence(Qt::Key_Space), &SheetEditor::swapWireCorner);
    addEditAction(tr("Slant the wire corner"), "actionWireSlant", QKeySequence(Qt::Key_Slash),
                  &SheetEditor::toggleWireSlant);
    editMenu->addSeparator();
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Rotate), "actionRotate",
                  QKeySequence(Qt::Key_R), &SheetEditor::rotateSelection);
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Mirror), "actionMirror",
                  QKeySequence(Qt::Key_M), &SheetEditor::mirrorSelection);
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Delete), "actionDelete",
                  QKeySequence::Delete, &SheetEditor::deleteSelection);
    editMenu->addSeparator();
    addEditAction(tr("Select all"), "actionSelectAll", QKeySequence::SelectAll,
                  &SheetEditor::selectAll);
    addEditAction(tr("Deselect"), "actionDeselect", QKeySequence(Qt::Key_Escape),
                  &SheetEditor::cancel);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));

    QAction *nextStepAction = viewMenu->addAction(tr("Next grid step"));
    nextStepAction->setObjectName(QStringLiteral("actionGridNext"));
    nextStepAction->setShortcut(QKeySequence(Qt::Key_N));
    connect(nextStepAction, &QAction::triggered, this,
            [this] { m_canvas->setGridStep(nextGridStep(m_canvas->gridStep())); });

    QAction *prevStepAction = viewMenu->addAction(tr("Previous grid step"));
    prevStepAction->setObjectName(QStringLiteral("actionGridPrevious"));
    prevStepAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_N));
    connect(prevStepAction, &QAction::triggered, this,
            [this] { m_canvas->setGridStep(prevGridStep(m_canvas->gridStep())); });

    viewMenu->addSeparator();

    QMenu *gridStyleMenu = viewMenu->addMenu(tr("Grid style"));
    QActionGroup *gridStyleGroup = new QActionGroup(this);
    const GridStyle gridStyle = m_canvas->gridStyle();
    QAction *linesAction = addChoice(gridStyleMenu, gridStyleGroup, tr("Lines"),
                                     gridStyle == GridStyle::Lines);
    connect(linesAction, &QAction::triggered, this,
            [this] { applyGridStyle(GridStyle::Lines); });
    QAction *dotsAction = addChoice(gridStyleMenu, gridStyleGroup, tr("Dots"),
                                    gridStyle == GridStyle::Dots);
    connect(dotsAction, &QAction::triggered, this,
            [this] { applyGridStyle(GridStyle::Dots); });

    QMenu *cursorMenu = viewMenu->addMenu(tr("Cursor"));
    QActionGroup *cursorGroup = new QActionGroup(this);
    const CursorStyle cursorStyle = m_canvas->cursorStyle();
    QAction *arrowAction = addChoice(cursorMenu, cursorGroup, tr("Arrow only"),
                                     cursorStyle == CursorStyle::Arrow);
    connect(arrowAction, &QAction::triggered, this,
            [this] { applyCursorStyle(CursorStyle::Arrow); });
    QAction *smallCrossAction = addChoice(cursorMenu, cursorGroup, tr("Small cross"),
                                          cursorStyle == CursorStyle::SmallCross);
    connect(smallCrossAction, &QAction::triggered, this,
            [this] { applyCursorStyle(CursorStyle::SmallCross); });
    QAction *fullCrossAction = addChoice(cursorMenu, cursorGroup, tr("Full-window cross"),
                                         cursorStyle == CursorStyle::FullCross);
    connect(fullCrossAction, &QAction::triggered, this,
            [this] { applyCursorStyle(CursorStyle::FullCross); });

    viewMenu->addSeparator();

    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_propertiesDock->toggleViewAction());
    viewMenu->addSeparator();

    QAction *warpAction = viewMenu->addAction(tr("Center and warp cursor on zoom"));
    warpAction->setCheckable(true);
    warpAction->setChecked(m_canvas->zoomCenterAndWarp());
    connect(warpAction, &QAction::toggled, this, &MainWindow::applyZoomCenterAndWarp);
}

void MainWindow::createStatusBar()
{
    // On the left, the saved mark, then the messages.
    // A click on the mark saves, as File > Save does.
    m_savedMark = new QPushButton(this);
    m_savedMark->setObjectName(QStringLiteral("savedMark"));
    m_savedMark->setCursor(Qt::PointingHandCursor);
    m_savedMark->setFocusPolicy(Qt::NoFocus);
    connect(m_savedMark, &QPushButton::clicked, this, &MainWindow::saveProject);
    statusBar()->addWidget(m_savedMark);
    m_statusText = new QLabel(this);
    statusBar()->addWidget(m_statusText, 1);

    m_positionLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_positionLabel);

    m_gridStepBox = new QComboBox(this);
    m_gridStepBox->setObjectName(QStringLiteral("gridStepBox"));
    for (Coord step : gridSteps()) {
        m_gridStepBox->addItem(gridStepText(step),
                               QVariant::fromValue<qlonglong>(step));
    }
    statusBar()->addPermanentWidget(m_gridStepBox);

    // 'activated' fires only on the user's choice, so showGridStep() below
    // cannot loop back into setGridStep().
    connect(m_gridStepBox, &QComboBox::activated, this, [this](int index) {
        m_canvas->setGridStep(m_gridStepBox->itemData(index).toLongLong());
    });
    connect(m_canvas, &CanvasView::gridStepChanged, this, &MainWindow::showGridStep);
    connect(m_canvas, &CanvasView::gridStepChanged, this, &MainWindow::storeGridStep);
    connect(m_canvas, &CanvasView::cursorMoved, this, &MainWindow::showCursorPosition);

    showGridStep(m_canvas->gridStep());
    showCursorPosition(0, 0);
}

QAction *MainWindow::addChoice(QMenu *menu, QActionGroup *group, const QString &text, bool checked)
{
    QAction *action = menu->addAction(text);
    action->setCheckable(true);
    action->setChecked(checked);
    group->addAction(action);
    return action;
}

void MainWindow::applyGridStyle(GridStyle style)
{
    m_canvas->setGridStyle(style);
    AppSettings::setGridStyle(style);
}

void MainWindow::applyCursorStyle(CursorStyle style)
{
    m_canvas->setCursorStyle(style);
    AppSettings::setCursorStyle(style);
}

void MainWindow::applyZoomCenterAndWarp(bool on)
{
    m_canvas->setZoomCenterAndWarp(on);
    AppSettings::setZoomCenterAndWarp(on);
}

void MainWindow::newProject()
{
    storeViews();
    m_views.clear();
    m_shownViewKey.clear();
    m_document->startNew();
    showPage(0);
    updateTitle();
    showStatus(QString());
}

bool MainWindow::openProject(const QString &path)
{
    QString error;
    QStringList problems;
    storeViews();
    if (!m_document->open(path, error, &problems)) {
        showStatus(error);
        return false;
    }
    m_views = readProjectView(path);
    m_shownViewKey.clear();
    showPage(0);
    updateTitle();
    AppSettings::addRecentProject(path);
    // The first problem in full, and how many more there are.
    QString message = problems.value(0, path);
    if (problems.size() > 1) {
        message += QStringLiteral(" — ") + tr("more problems with the project library")
                   + QStringLiteral(": ") + QString::number(problems.size() - 1);
    }
    showStatus(message);
    offerAutosave();
    return true;
}

bool MainWindow::saveProjectAs(const QString &parentFolder, const QString &name)
{
    QString error;
    if (!m_document->saveAsNewFolder(parentFolder, name, error)) {
        showStatus(error);
        return false;
    }
    updateTitle();
    AppSettings::addRecentProject(m_document->projectPath());
    showStatus(m_document->projectPath());
    return true;
}

bool MainWindow::confirmLeavingProject()
{
    if (!m_document->isModified()) {
        // Changes undone back to the files may have left a copy behind.
        m_document->dropAutosave();
        return true;
    }
    QMessageBox box(QMessageBox::Warning, tr("Unsaved changes"),
                    tr("Save the changes to the project") + QLatin1Char('?'),
                    QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
    box.setObjectName(QStringLiteral("unsavedChangesBox"));
    box.setDefaultButton(QMessageBox::Save);
    switch (box.exec()) {
    case QMessageBox::Save:
        // A save that failed or was cancelled keeps the project open.
        return saveProject();
    case QMessageBox::Discard:
        m_document->dropAutosave();
        return true;
    default:
        return false;
    }
}

void MainWindow::autosaveProject()
{
    QString error;
    if (!m_document->autosave(error))
        showStatus(error);
}

void MainWindow::offerAutosave()
{
    if (!Document::hasAutosave(m_document->projectPath()))
        return;
    QMessageBox box(QMessageBox::Question, tr("Unsaved changes"),
                    tr("The project has changes that were not saved last time") + QStringLiteral(".\n")
                        + tr("Restore them") + QLatin1Char('?'),
                    QMessageBox::Yes | QMessageBox::No, this);
    box.setObjectName(QStringLiteral("restoreAutosaveBox"));
    box.setDefaultButton(QMessageBox::Yes);
    if (box.exec() != QMessageBox::Yes) {
        m_document->dropAutosave();
        return;
    }
    QString error;
    if (!m_document->restoreAutosave(error))
        showStatus(error);
}

void MainWindow::chooseNewProject()
{
    if (confirmLeavingProject())
        newProject();
}

void MainWindow::chooseProjectToOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open project"), AppSettings::lastProjectFolder(),
        tr("PocketPCB projects") + QStringLiteral(" (*") + QLatin1String(FILE_EXT_PROJECT)
            + QLatin1Char(')'));
    // Asked once a project is chosen: a dialog closed without one leaves
    // the open project as it is.
    if (!path.isEmpty())
        leaveAndOpenProject(path);
}

void MainWindow::leaveAndOpenProject(const QString &path)
{
    if (!confirmLeavingProject())
        return;
    // The project file lies in the project's own folder.
    AppSettings::setLastProjectFolder(QFileInfo(QFileInfo(path).absolutePath()).absolutePath());
    openProject(path);
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();
    const QStringList paths = AppSettings::recentProjects();
    for (const QString &path : paths) {
        QAction *action = m_recentMenu->addAction(QFileInfo(path).completeBaseName());
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] { openRecentProject(path); });
    }
    m_recentMenu->addSeparator();
    QAction *clearAction = m_recentMenu->addAction(tr("Clear list"));
    clearAction->setObjectName(QStringLiteral("actionClearRecentProjects"));
    connect(clearAction, &QAction::triggered, this, [] { AppSettings::clearRecentProjects(); });
    m_recentMenu->setEnabled(!paths.isEmpty());
}

void MainWindow::openRecentProject(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        AppSettings::removeRecentProject(path);
        showStatus(tr("The project is no longer there") + QStringLiteral(": ")
                   + QDir::toNativeSeparators(path));
        return;
    }
    leaveAndOpenProject(path);
}

bool MainWindow::saveProject()
{
    if (m_document->projectPath().isEmpty())
        return chooseProjectToSaveAs();
    QString error;
    if (!m_document->save(error)) {
        showStatus(error);
        return false;
    }
    showStatus(m_document->projectPath());
    return true;
}

bool MainWindow::chooseProjectToSaveAs()
{
    const QString path = m_document->projectPath();
    const QString name = path.isEmpty() ? tr("untitled") : QFileInfo(path).completeBaseName();
    QString parentFolder = AppSettings::lastProjectFolder();
    if (parentFolder.isEmpty())
        parentFolder = QDir::homePath();

    SaveProjectDialog dialog(name, parentFolder, this);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    AppSettings::setLastProjectFolder(dialog.parentFolder());
    return saveProjectAs(dialog.parentFolder(), dialog.name());
}

void MainWindow::showPage(int page, bool fit)
{
    rememberView();
    showProject();
    m_designItem->setPage(page);
    m_shownViewKey = pageViewKey(m_designItem->page());
    {
        // The tab follows the page, not the other way round.
        const QSignalBlocker blocker(m_tabs);
        m_tabs->setCurrentIndex(m_designItem->page());
    }
    const int shown = m_designItem->page();
    m_sheetEditor->setSheet(shown == m_designItem->boardPage() ? -1 : shown);
    applyProjectGrid();
    if (fit)
        showPageView();
}

QString MainWindow::pageViewKey(int page) const
{
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    if (page >= 0 && page < sheets.size())
        return idToString(sheets.at(page).id);
    return QLatin1String(BOARD_VIEW_KEY);
}

void MainWindow::rememberView()
{
    if (m_designItem->isVisible() && !m_shownViewKey.isEmpty()) {
        m_views.insert(m_shownViewKey,
                       PageView{m_canvas->viewCenter(), m_canvas->pixelsPerMm()});
    }
}

void MainWindow::showPageView()
{
    const auto found = m_views.constFind(pageViewKey(m_designItem->page()));
    if (found != m_views.constEnd())
        m_canvas->setView(found->center, found->pixelsPerMm);
    else
        m_canvas->fitRect(m_designItem->pageBounds());
}

void MainWindow::storeViews()
{
    rememberView();
    // The views of sheets that are gone go too.
    QSet<QString> keys{QLatin1String(BOARD_VIEW_KEY)};
    for (const Sheet &sheet : m_document->design().schematic.sheets)
        keys.insert(idToString(sheet.id));
    for (auto it = m_views.begin(); it != m_views.end();) {
        if (keys.contains(it.key()))
            ++it;
        else
            it = m_views.erase(it);
    }
    const QString path = m_document->projectPath();
    if (path.isEmpty())
        return;
    // A view not written is not worth a word: the page is fitted next time.
    QString error;
    writeProjectView(path, m_views, error);
}

void MainWindow::showStep(int page, const QSet<ObjectId> &ids)
{
    if (page < 0)
        return;
    if (page != m_designItem->page() || !m_designItem->isVisible())
        showPage(page, false);
    m_sheetEditor->setSelection(ids);
}

void MainWindow::applyProjectGrid()
{
    const Project &project = m_document->design().project;
    const bool board = m_designItem->page() == m_designItem->boardPage();
    m_canvas->setGridStep(board ? project.boardGridStep : project.schematicGridStep);
}

void MainWindow::storeGridStep(Coord step)
{
    // A library file on the canvas has no project grid.
    if (!m_designItem->isVisible())
        return;
    const bool board = m_designItem->page() == m_designItem->boardPage();
    Design design = m_document->design();
    Coord &stored = board ? design.project.boardGridStep : design.project.schematicGridStep;
    if (stored == step)
        return;
    stored = step;
    m_document->change(tr("Grid step") + QLatin1Char(' ') + gridStepText(step), design);
}

QString MainWindow::gridStepText(Coord step)
{
    return formatMmShort(step) + QLatin1Char(' ') + tr("mm");
}

void MainWindow::showProject()
{
    // The library file is let go: nothing of it is shown or watched.
    if (m_libraryItem != nullptr)
        m_libraryItem->setVisible(false);
    m_libraryPath.clear();
    watchLibraryFile();
    m_designItem->setVisible(true);
}

void MainWindow::projectChanged()
{
    m_designItem->contentChanged();

    const Design &design = m_document->design();
    const QSignalBlocker blocker(m_tabs);
    while (m_tabs->count() > 0)
        m_tabs->removeTab(0);
    for (int i = 0; i < design.schematic.sheets.size(); ++i) {
        m_tabs->addTab(SheetEditor::sheetName(design.schematic.sheets.at(i), i));
    }
    m_tabs->addTab(tr("Board"));
    m_tabs->setCurrentIndex(m_designItem->page());
    // The sheet edited may be gone, and so may what was chosen on it.
    const int shown = m_designItem->page();
    const int sheet = shown == m_designItem->boardPage() ? -1 : shown;
    if (m_sheetEditor->sheet() != sheet)
        m_sheetEditor->setSheet(sheet);
    else
        m_sheetEditor->setSelection(m_sheetEditor->selection());
    applyProjectGrid();
    updateLibraryPanel();
}

void MainWindow::updateTitle()
{
    const QString path = m_document->projectPath();
    const QString name = path.isEmpty() ? tr("untitled") : QFileInfo(path).fileName();
    // "[*]" is where Qt puts the mark of unsaved changes.
    setWindowTitle(name + QStringLiteral("[*] — ") + QLatin1String(APP_TITLE));
    setWindowModified(m_document->isModified());
    updateSavedMark();
}

void MainWindow::updateSavedMark()
{
    if (m_savedMark == nullptr)
        return;
    const char *color = SAVED_COLOR;
    if (m_document->isModified()) {
        m_savedMark->setText(tr("Not saved"));
        color = NOT_SAVED_COLOR;
    } else if (m_document->projectPath().isEmpty()) {
        m_savedMark->setText(tr("New"));
        color = NEW_COLOR;
    } else {
        m_savedMark->setText(tr("Saved"));
    }
    m_savedMark->setStyleSheet(QString::fromLatin1(SAVED_MARK_STYLE).arg(QLatin1String(color)));
}

void MainWindow::showStatus(const QString &text)
{
    m_statusText->setText(text);
}

QString MainWindow::statusText() const
{
    return m_statusText->text();
}

void MainWindow::loadLibrary()
{
    QStringList problems;
    QString error;
    if (!AppSettings::makeUserLibraryFolders(error))
        problems.append(error);
    const Library builtIn = LibraryFolder::read(AppSettings::builtInLibraryFolder(), problems);
    const Library user = LibraryFolder::read(AppSettings::userLibraryFolder(), problems);
    m_library = mergeLibraries(builtIn, user);
    updateLibraryPanel();
    if (!problems.isEmpty())
        showStatus(problems.first());
}

bool MainWindow::placePart(LibraryPanel::Source source, const QString &part,
                           const QString &footprint)
{
    if (!m_designItem->isVisible() || m_sheetEditor->sheet() < 0) {
        showStatus(tr("Open a sheet to place parts on"));
        return false;
    }
    const Library &library =
        source == LibraryPanel::Source::Project ? m_document->design().library : m_library.items;
    m_sheetEditor->startPlacing(library, part, footprint);
    m_canvas->setFocus();
    return m_sheetEditor->isPlacing();
}

void MainWindow::updateLibraryPanel()
{
    const Design &design = m_document->design();
    m_libraryPanel->setLibraries(m_library, design.library, design.project.designatorStyle);
}

void MainWindow::openLibraryFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(AppSettings::userLibraryFolder()));
}

void MainWindow::chooseLibraryFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open library file"), AppSettings::lastLibraryFolder(),
        tr("Library files") + QStringLiteral(" (*.json)"));
    if (path.isEmpty())
        return;
    AppSettings::setLastLibraryFolder(QFileInfo(path).absolutePath());
    openLibraryFile(path);
}

bool MainWindow::openLibraryFile(const QString &path)
{
    if (!showLibraryFile(path))
        return false;
    m_libraryPath = path;
    watchLibraryFile();
    m_canvas->fitRect(m_libraryItem->boundingRect());
    return true;
}

bool MainWindow::reloadLibraryFile()
{
    if (m_libraryPath.isEmpty())
        return false;
    // An editor that saves by replacing the file ends the watch on the old
    // one, so the watch is set again on every reload.
    watchLibraryFile();
    return showLibraryFile(m_libraryPath);
}

bool MainWindow::showLibraryFile(const QString &path)
{
    LibraryItem content;
    QString error;
    if (!LibraryFile::read(path, content, error)) {
        showStatus(error);
        return false;
    }

    if (m_libraryItem == nullptr) {
        m_libraryItem = new LibraryViewItem(content);
        m_canvas->scene()->addItem(m_libraryItem);
    } else {
        m_libraryItem->setContent(content);
    }
    // The library file takes the place of the project on the canvas.
    rememberView();
    m_libraryItem->setVisible(true);
    m_designItem->setVisible(false);

    const QString name = QFileInfo(path).fileName();
    QString message;
    switch (content.kind) {
    case LibraryKind::Symbol:
        message = tr("Symbol") + QLatin1String(": ") + name;
        break;
    case LibraryKind::Footprint:
        message = PropertiesPanel::footprintTitle() + QLatin1String(": ") + name;
        break;
    case LibraryKind::Part:
        message = tr("Part") + QLatin1String(": ") + name + QStringLiteral(" — ") + tr("symbol")
                  + QLatin1Char(' ') + content.part.symbol + QStringLiteral(" — ")
                  + tr("footprints") + QLatin1Char(' ')
                  + content.part.footprints.join(QLatin1String(", "));
        break;
    }
    showStatus(message);
    return true;
}

void MainWindow::watchLibraryFile()
{
    const QStringList watched = m_libraryWatcher->files();
    for (const QString &path : watched) {
        if (path != m_libraryPath)
            m_libraryWatcher->removePath(path);
    }
    if (!watched.contains(m_libraryPath) && QFileInfo::exists(m_libraryPath))
        m_libraryWatcher->addPath(m_libraryPath);
}

void MainWindow::showCursorPosition(Coord x, Coord y)
{
    m_positionLabel->setText(QStringLiteral("X %1   Y %2 ").arg(formatMmFixed(x), formatMmFixed(y))
                             + tr("mm"));
}

void MainWindow::showGridStep(Coord step)
{
    // -1 (no selection) for a step that is not in the list.
    m_gridStepBox->setCurrentIndex(m_gridStepBox->findData(QVariant::fromValue<qlonglong>(step)));
}

} // namespace ppcb
