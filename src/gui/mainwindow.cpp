#include "mainwindow.h"

#include "autoroute.h"
#include "boardeditor.h"
#include "boardprintdialog.h"
#include "bompanel.h"
#include "canvasview.h"
#include "designviewitem.h"
#include "drcpanel.h"
#include "ercpanel.h"
#include "document.h"
#include "format.h"
#include "gerberexport.h"
#include "grid.h"
#include "language.h"
#include "librarypanel.h"
#include "netlist.h"
#include "paint.h"
#include "netspanel.h"
#include "projectpropertiesdialog.h"
#include "propertiespanel.h"
#include "ratsnest.h"
#include "rectangleboarddialog.h"
#include "sheeteditor.h"
#include "sheettabs.h"
#include "toolicons.h"
#include "version.h"
#include "updateboarddialog.h"
#include "updatepartsdialog.h"
#include "stephistory.h"
#include "projectversions.h"
#include "versionspanel.h"
#include "libraryviewitem.h"
#include "saveprojectdialog.h"
#include "schematicprint.h"
#include "screensizedialog.h"

#include <QAction>
#include <QApplication>
#include <QScreen>
#include <QToolBar>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDockWidget>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QDateTime>
#include <QInputDialog>
#include <QLineEdit>
#include <QWidgetAction>
#include <QGraphicsScene>
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QElapsedTimer>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPainter>
#include <QPixmap>
#include <QAbstractButton>
#include <QPushButton>
#include <QShowEvent>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QUrl>

#include <functional>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <algorithm>

namespace ppcb {

namespace {

const int DEFAULT_WIDTH = 1100;
const int DEFAULT_HEIGHT = 750;
// Wait after a change of the watched file before reading it, milliseconds.
const int RELOAD_DELAY_MS = 150;
const int MS_PER_MINUTE = 60 * 1000;
// The size Rectangular board offers for a board without an outline.
const Coord DEFAULT_BOARD_WIDTH = 100 * NM_PER_MM;
const Coord DEFAULT_BOARD_HEIGHT = 80 * NM_PER_MM;
// The buttons of the strip of tools: how much of the colour of the text
// shows behind one under the mouse and one held down, and how much of the
// colour of a choice behind one that stays pressed, 0 to 255; a frame of
// that colour goes round it. Every button has a frame, unseen unless
// pressed, so that none grows when pressed and moves the others.
const int HOVER_ALPHA = 30;
const int HELD_ALPHA = 60;
const int PRESSED_ALPHA = 90;
// The arrow beside a button that has a menu, in pixels.
const int MENU_ARROW_WIDTH = 12;
// How long the autorouter runs before its progress box shows, milliseconds.
const int ROUTE_BOX_DELAY_MS = 500;
// The question mark of a question asked before deleting much, in pixels.
const int QUESTION_PIXELS = 48;
// The gaps between the file buttons and Undo, and between the zoom buttons
// and the modes, in pixels.
const int GROUP_GAP = TOOL_ICON_SIZE;
// The table of the colours of the paper: its swatches, in pixels, and how
// many stand in a row. The one chosen has a frame in the selection colour.
const int SWATCH_WIDTH = 72;
const int SWATCH_HEIGHT = 45;
const int SWATCH_SPACING = 6;
const int SWATCH_COLUMNS = 5;
const int SWATCH_EDGE = 3;
// The property of a swatch that holds its colour, for the preview.
const char SWATCH_COLOR_PROPERTY[] = "paperColor";
const char SWATCH_STYLE[] = "QToolButton { border: 1px solid #a0a0a0; padding: 2px; }"
                            "QToolButton:checked { border: 3px solid %1; padding: 0px; }";
// The menu of the sheet stands out from the paper under it: a dark frame,
// the item under the mouse in the colour of the selection.
// The columns of the drills in the menu of a hole's diameter.
const int DRILL_MENU_COLUMNS = 3;

const char SHEET_MENU_STYLE[] =
    "QMenu { background: #ffffff; border: 1px solid #505050; padding: 2px; }"
    "QMenu::item { padding: 3px 20px 3px 10px; }"
    "QMenu::item:selected { background: %1; color: #ffffff; }"
    "QMenu::separator { height: 1px; background: #c8c8c8; margin: 2px 6px; }";

const char APP_TITLE[] = "PocketPCB";

// The saved mark: white bold text on soft green, red or grey.
const char SAVED_MARK_STYLE[] =
    "QPushButton { color: white; font-weight: bold; padding: 0px 6px; border: none;"
    " border-radius: 2px; background: %1; }";
const char SAVED_COLOR[] = "#5f9462";
// The count of unrouted connections: black, with a gap after it.
const char UNROUTED_STYLE[] = "QLabel { color: black; margin-right: 12px; }";
const char NOT_SAVED_COLOR[] = "#c25450";
const char NEW_COLOR[] = "#8c8c8c";

// A piece of paper of 'color' with a wire and a symbol on it, edged in
// black so that a pale one stands out from the menu.
QPixmap paperSwatch(const QColor &color)
{
    QPixmap pixmap(SWATCH_WIDTH, SWATCH_HEIGHT);
    pixmap.fill(color);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int middle = SWATCH_HEIGHT / 2;
    painter.setPen(QPen(paintcolor::wire(), 2));
    painter.drawLine(4, middle, SWATCH_WIDTH / 2, middle);
    painter.setPen(QPen(paintcolor::symbol(), 2));
    painter.drawRect(SWATCH_WIDTH / 2, middle - SWATCH_HEIGHT / 4, SWATCH_WIDTH / 3,
                     SWATCH_HEIGHT / 2);
    painter.setPen(QPen(Qt::black, SWATCH_EDGE, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::NoBrush);
    const qreal inset = SWATCH_EDGE / 2.0;
    painter.drawRect(QRectF(inset, inset, SWATCH_WIDTH - SWATCH_EDGE, SWATCH_HEIGHT - SWATCH_EDGE));
    return pixmap;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_canvas = new CanvasView(this);
    m_canvas->setGridStyle(AppSettings::gridStyle());
    m_canvas->setCursorStyle(AppSettings::cursorStyle());
    m_canvas->setZoomCenterAndWarp(AppSettings::zoomCenterAndWarp());

    m_document = new Document(this);
    m_document->setBackupCount(AppSettings::backupCount());
    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, &MainWindow::autosaveProject);
    if (AppSettings::autosaveMinutes() > 0)
        m_autosaveTimer->start(AppSettings::autosaveMinutes() * MS_PER_MINUTE);
    m_designItem = new DesignViewItem(m_document, m_canvas->scene()->sceneRect());
    m_canvas->scene()->addItem(m_designItem);
    m_sheetEditor = new SheetEditor(m_document, m_canvas, this);
    m_sheetEditor->setWireRouting(AppSettings::wireRouting());
    m_boardEditor = new BoardEditor(m_document, m_canvas, this);

    // The pages of the project as tabs over the canvas.
    m_sheetTabs = new SheetTabs(m_document, m_sheetEditor, this);
    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_sheetTabs);
    layout->addWidget(m_canvas);
    setCentralWidget(central);
    connect(m_sheetTabs, &SheetTabs::pageChosen, this, [this](int page) { showPage(page); });

    m_designItem->setEditor(m_sheetEditor);
    m_designItem->setBoardEditor(m_boardEditor);
    m_designItem->setDrawingCoordinatesShown(AppSettings::drawingCoordinatesShown());
    m_designItem->setRatsnestArcs(AppSettings::ratsnestArcs());
    connect(m_sheetEditor, &SheetEditor::changed, this, [this] { m_designItem->update(); });
    connect(m_boardEditor, &BoardEditor::changed, this, [this] { m_designItem->update(); });
    connect(m_boardEditor, &BoardEditor::message, this, &MainWindow::showStatus);
    connect(m_sheetEditor, &SheetEditor::message, this, &MainWindow::showStatus);
    connect(m_document, &Document::stepApplied, this, &MainWindow::showStep);
    connect(m_document, &Document::stepMade, this, &MainWindow::recordStep);
    // The part being placed follows the cursor.
    connect(m_canvas, &CanvasView::cursorMoved, m_sheetEditor,
            [this](Coord x, Coord y) { m_sheetEditor->hover(Point{x, y}); });
    // The side of the board outline being drawn follows it too.
    connect(m_canvas, &CanvasView::cursorMoved, m_boardEditor,
            [this](Coord x, Coord y) { m_boardEditor->hover(Point{x, y}); });
    // A short right click ends placing and drawing wires, as Esc does; else
    // it opens the menu of the sheet. On the board it ends drawing the
    // outline or a cut-out.
    connect(m_canvas, &CanvasView::contextMenuRequested, this, [this](const QPoint &globalPos) {
        // On the board it ends placing holes too.
        if (m_boardEditor->isActive()) {
            if (m_boardEditor->isDrawing())
                m_boardEditor->cancel();
            else
                showBoardMenu(globalPos);
        } else if (m_sheetEditor->isPlacing() || m_sheetEditor->isWiring()) {
            m_sheetEditor->cancel();
        } else {
            showSheetMenu(globalPos);
        }
    });
    connect(m_document, &Document::designChanged, this, &MainWindow::projectChanged);
    connect(m_document, &Document::modifiedChanged, this, &MainWindow::updateTitle);
    // The title block shows the date of the last save.
    connect(m_document, &Document::savedDateChanged, this, [this] { m_designItem->update(); });

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
    // The table of nets, under the properties.
    m_netsPanel = new NetsPanel(m_document, m_sheetEditor, this);
    m_netsDock = new QDockWidget(NetsPanel::netsTitle(), this);
    m_netsDock->setObjectName(QStringLiteral("netsDock"));
    m_netsDock->setWidget(m_netsPanel);
    splitDockWidget(m_propertiesDock, m_netsDock, Qt::Vertical);
    // The problems of the rules check, under the nets.
    m_ercPanel = new ErcPanel(m_document, m_sheetEditor, this);
    m_ercDock = new QDockWidget(ErcPanel::ercTitle(), this);
    m_ercDock->setObjectName(QStringLiteral("ercDock"));
    m_ercDock->setWidget(m_ercPanel);
    splitDockWidget(m_netsDock, m_ercDock, Qt::Vertical);
    // The problems of the board's rules check, under those of the sheets;
    // hidden until asked for.
    m_drcPanel = new DrcPanel(m_document, m_boardEditor, this);
    m_drcDock = new QDockWidget(DrcPanel::drcTitle(), this);
    m_drcDock->setObjectName(QStringLiteral("drcDock"));
    m_drcDock->setWidget(m_drcPanel);
    splitDockWidget(m_ercDock, m_drcDock, Qt::Vertical);
    m_drcDock->hide();
    // The BOM, under the problems; hidden until asked for.
    m_bomPanel = new BomPanel(m_document, this);
    m_bomDock = new QDockWidget(BomPanel::bomTitle(), this);
    m_bomDock->setObjectName(QStringLiteral("bomDock"));
    m_bomDock->setWidget(m_bomPanel);
    splitDockWidget(m_drcDock, m_bomDock, Qt::Vertical);
    m_bomDock->hide();
    // The versions of the project, under the BOM; hidden until asked for.
    m_versionsPanel = new VersionsPanel(m_document, this);
    m_versionsDock = new QDockWidget(VersionsPanel::versionsTitle(), this);
    m_versionsDock->setObjectName(QStringLiteral("versionsDock"));
    m_versionsDock->setWidget(m_versionsPanel);
    splitDockWidget(m_bomDock, m_versionsDock, Qt::Vertical);
    m_versionsDock->hide();
    connect(m_versionsPanel, &VersionsPanel::restoreRequested, this, &MainWindow::restoreVersion);
    connect(m_bomPanel, &BomPanel::componentsChosen, this, &MainWindow::showComponents);
    // A problem chosen there is shown: its sheet, centred on it, with what
    // it is about chosen; a missing value is typed in at once.
    connect(m_ercPanel, &ErcPanel::problemChosen, this, [this](const ErcProblem &problem) {
        showPage(problem.sheet);
        m_canvas->setView(toMm(problem.at), m_canvas->pixelsPerMm());
        m_sheetEditor->setSelection({problem.object});
        if (problem.kind == ErcKind::EmptyValue)
            m_sheetEditor->editInPlace(problem.object);
    });
    // The rings on the sheet go with the panel.
    connect(m_ercDock->toggleViewAction(), &QAction::toggled, this,
            [this](bool shown) { m_designItem->setErcMarksShown(shown); });
    // A problem of the board chosen is shown: the board, centred on it,
    // with what it is about chosen; Check shows the board.
    connect(m_drcPanel, &DrcPanel::problemChosen, this, [this](const DrcProblem &problem) {
        if (m_designItem->page() != m_designItem->boardPage())
            showPage(m_designItem->boardPage());
        m_canvas->setView(toMm(problem.at), m_canvas->pixelsPerMm());
        m_boardEditor->setChosen(DrcPanel::problemObjects(problem));
    });
    connect(m_drcPanel, &DrcPanel::checkRequested, this, [this] {
        if (m_designItem->page() != m_designItem->boardPage())
            showPage(m_designItem->boardPage());
    });
    connect(m_drcDock->toggleViewAction(), &QAction::toggled, this,
            [this](bool shown) { m_designItem->setDrcMarksShown(shown); });
    // A net chosen there is shown: on the first sheet it is on, unless the
    // sheet shown has it.
    connect(m_netsPanel, &NetsPanel::netChosen, this, [this](const QString &name) {
        const Design &design = m_document->design();
        for (const Net &net : m_sheetEditor->nets()) {
            if (net.name != name)
                continue;
            const QList<int> sheets = netSheets(net, design.schematic);
            if (!sheets.isEmpty() && !sheets.contains(m_designItem->page()))
                showPage(sheets.first());
            return;
        }
    });
    connect(m_libraryPanel, &LibraryPanel::reloadRequested, this, &MainWindow::loadLibrary);
    connect(m_libraryPanel, &LibraryPanel::placeRequested, this, &MainWindow::placePart);
    connect(m_libraryPanel, &LibraryPanel::placePowerRequested, this, &MainWindow::placePower);
    // A component double-clicked on the sheet is shown in the panel: its
    // part, with its footprint below the list.
    connect(m_sheetEditor, &SheetEditor::componentOpened, m_libraryPanel,
            &LibraryPanel::showProjectPart);
    // A part dragged out of the panel is placed the same way: it follows the
    // mouse, R and X turn it, and letting go over the sheet puts it down and
    // placing goes on; letting go anywhere else calls it off.
    connect(m_libraryPanel, &LibraryPanel::dragStarted, this, &MainWindow::placePart);
    connect(m_libraryPanel, &LibraryPanel::powerDragStarted, this, &MainWindow::placePower);
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
    createToolBar();
    createStatusBar();
    loadLibrary();
    projectChanged();
    updateTitle();

    resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    // Both do nothing when given data that was never saved.
    restoreGeometry(AppSettings::windowGeometry());
    restoreState(AppSettings::windowState());
    m_designItem->setErcMarksShown(!m_ercDock->isHidden());
    m_designItem->setDrcMarksShown(!m_drcDock->isHidden());
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
    m_mainMenu = new QMenu(this);
    QMenu *fileMenu = m_mainMenu->addMenu(tr("&File"));

    QAction *newAction = fileMenu->addAction(tr("&New project"));
    newAction->setObjectName(QStringLiteral("actionNewProject"));
    newAction->setShortcut(QKeySequence::New);
    connect(newAction, &QAction::triggered, this, &MainWindow::chooseNewProject);

    QAction *openAction = fileMenu->addAction(tr("&Open project") + QStringLiteral("…"));
    openAction->setObjectName(QStringLiteral("actionOpenProject"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::chooseProjectToOpen);

    QAction *lastAction = fileMenu->addAction(lastProjectTitle());
    lastAction->setObjectName(QStringLiteral("actionOpenLastProject"));
    lastAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    connect(lastAction, &QAction::triggered, this, &MainWindow::openLastProject);

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
    QAction *versionAction = fileMenu->addAction(tr("Save version") + QStringLiteral("…"));
    versionAction->setObjectName(QStringLiteral("actionSaveVersion"));
    connect(versionAction, &QAction::triggered, this, &MainWindow::saveVersion);
    fileMenu->addSeparator();

    QAction *printAction = fileMenu->addAction(printSchematicTitle() + QStringLiteral("…"));
    printAction->setObjectName(QStringLiteral("actionPrintSchematic"));
    printAction->setShortcut(QKeySequence::Print);
    connect(printAction, &QAction::triggered, this, [this] {
        const QString message = printSchematic(this, *m_document, m_sheetEditor->sheet());
        if (!message.isEmpty())
            showStatus(message);
    });
    QAction *pdfAction = fileMenu->addAction(exportSchematicPdfTitle());
    pdfAction->setObjectName(QStringLiteral("actionExportSchematicPdf"));
    connect(pdfAction, &QAction::triggered, this, [this] {
        const QString message = exportSchematicPdf(this, *m_document, m_sheetEditor->sheet());
        if (!message.isEmpty())
            showStatus(message);
    });
    QAction *boardPrintAction = fileMenu->addAction(printBoardTitle() + QStringLiteral("…"));
    boardPrintAction->setObjectName(QStringLiteral("actionPrintBoard"));
    connect(boardPrintAction, &QAction::triggered, this, [this] {
        const QString message = printBoard(this, *m_document);
        if (!message.isEmpty())
            showStatus(message);
    });
    QAction *gerberAction = fileMenu->addAction(exportGerberTitle());
    gerberAction->setObjectName(QStringLiteral("actionExportGerber"));
    connect(gerberAction, &QAction::triggered, this, &MainWindow::exportGerberFiles);
    fileMenu->addSeparator();

    QAction *propertiesAction =
        fileMenu->addAction(ProjectPropertiesDialog::title() + QStringLiteral("…"));
    propertiesAction->setObjectName(QStringLiteral("actionProjectProperties"));
    connect(propertiesAction, &QAction::triggered, this, &MainWindow::editProjectProperties);
    QAction *updatePartsAction =
        fileMenu->addAction(UpdatePartsDialog::title() + QStringLiteral("…"));
    updatePartsAction->setObjectName(QStringLiteral("actionUpdateParts"));
    connect(updatePartsAction, &QAction::triggered, this, &MainWindow::updatePartsFromLibrary);
    QAction *updateBoardAction =
        fileMenu->addAction(UpdateBoardDialog::title() + QStringLiteral("…"));
    updateBoardAction->setObjectName(QStringLiteral("actionUpdateBoard"));
    updateBoardAction->setShortcut(QKeySequence(Qt::Key_F8));
    connect(updateBoardAction, &QAction::triggered, this, &MainWindow::updateBoardFromSchematic);
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
    QMenu *editMenu = m_mainMenu->addMenu(tr("&Edit"));
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
        return action;
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
    QAction *powerAction = editMenu->addAction(tr("Place power symbol"));
    powerAction->setObjectName(QStringLiteral("actionPlacePower"));
    powerAction->setShortcut(QKeySequence(Qt::Key_P));
    connect(powerAction, &QAction::triggered, this, &MainWindow::placeLastPower);
    QAction *labelAction = editMenu->addAction(tr("Place label"));
    labelAction->setObjectName(QStringLiteral("actionPlaceLabel"));
    labelAction->setShortcut(QKeySequence(Qt::Key_L));
    connect(labelAction, &QAction::triggered, this, &MainWindow::placeLabel);
    QAction *noConnectAction = editMenu->addAction(tr("Place no-connect mark"));
    noConnectAction->setObjectName(QStringLiteral("actionPlaceNoConnect"));
    noConnectAction->setShortcut(QKeySequence(Qt::Key_X));
    connect(noConnectAction, &QAction::triggered, this, &MainWindow::placeNoConnect);
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Wire), "actionWire",
                  QKeySequence(Qt::Key_W), &SheetEditor::startWiring);
    addEditAction(tr("Draw bus"), "actionBus", QKeySequence(Qt::Key_B),
                  &SheetEditor::startDrawingBus);
    QAction *cornerAction =
        addEditAction(tr("Turn the wire corner the other way"), "actionWireCorner",
                      QKeySequence(Qt::Key_Space), &SheetEditor::swapWireCorner);
    // On the board it turns the corner of the track being laid.
    connect(cornerAction, &QAction::triggered, this, [this] {
        if (m_boardEditor->isActive())
            m_boardEditor->swapTrackCorner();
    });
    QAction *slantAction = addEditAction(tr("Slant the wire corner"), "actionWireSlant",
                                         QKeySequence(Qt::Key_Slash),
                                         &SheetEditor::toggleWireSlant);
    // The key of '/' gives '.' on the Russian layout, which Qt does not take
    // for '/' as it takes Latin letters for the shortcuts on other layouts.
    slantAction->setShortcuts({QKeySequence(Qt::Key_Slash), QKeySequence(Qt::Key_Period)});
    editMenu->addSeparator();
    QAction *rotateAction =
        addEditAction(SheetEditor::actionName(SheetEditor::Action::Rotate), "actionRotate",
                      QKeySequence(Qt::Key_R), &SheetEditor::rotateSelection);
    // On the board the placements chosen turn; the sheet editor has no sheet then.
    connect(rotateAction, &QAction::triggered, this, [this] {
        if (m_boardEditor->isActive())
            m_boardEditor->rotateChosen();
    });
    addEditAction(SheetEditor::actionName(SheetEditor::Action::Mirror), "actionMirror",
                  QKeySequence(Qt::Key_M), &SheetEditor::mirrorSelection);
    QAction *deleteAction =
        addEditAction(SheetEditor::actionName(SheetEditor::Action::Delete), "actionDelete",
                      QKeySequence::Delete, &SheetEditor::deleteSelection);
    // On the board the holes chosen go; the sheet editor has no sheet then.
    connect(deleteAction, &QAction::triggered, this, [this] {
        if (m_boardEditor->isActive())
            m_boardEditor->deleteChosen();
    });
    editMenu->addSeparator();
    QAction *selectAllAction = editMenu->addAction(tr("Select all"));
    selectAllAction->setObjectName(QStringLiteral("actionSelectAll"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, this, [this] {
        if (m_boardEditor->isActive()) {
            // What is chosen all is shown: the silkscreen and the tracks come
            // back.
            for (const char *name : {"actionSilkscreen", "actionHideTracks"}) {
                if (QAction *hide = findChild<QAction *>(QLatin1String(name)))
                    hide->setChecked(false);
            }
            m_boardEditor->chooseAll();
        } else
            m_sheetEditor->selectAll();
    });
    // Locks or unlocks what is chosen, whatever it is; pressed while it is
    // locked.
    QAction *lockAction = editMenu->addAction(lockName(true));
    lockAction->setObjectName(QStringLiteral("actionLock"));
    lockAction->setCheckable(true);
    connect(lockAction, &QAction::triggered, this, [this](bool locked) {
        if (m_boardEditor->isActive())
            m_boardEditor->setChosenLocked(locked);
        else
            m_sheetEditor->setChosenLocked(locked);
        updateTools();
    });
    QAction *deselectAction = editMenu->addAction(tr("Deselect"));
    deselectAction->setObjectName(QStringLiteral("actionDeselect"));
    deselectAction->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(deselectAction, &QAction::triggered, this, &MainWindow::cancelEditing);
    editMenu->addSeparator();

    // The board.
    const auto addBoardAction = [this, editMenu](BoardEditor::Action action, const char *name,
                                                 auto slot) {
        QAction *item = editMenu->addAction(BoardEditor::actionName(action));
        item->setObjectName(QLatin1String(name));
        connect(item, &QAction::triggered, m_boardEditor, slot);
        return item;
    };
    addBoardAction(BoardEditor::Action::DrawOutline, "actionDrawOutline",
                   &BoardEditor::startDrawingOutline);
    addBoardAction(BoardEditor::Action::CutOut, "actionCutOut", &BoardEditor::startCuttingOut);
    QAction *rectangleAction = editMenu->addAction(
        BoardEditor::actionName(BoardEditor::Action::RectangleBoard) + QStringLiteral("…"));
    rectangleAction->setObjectName(QStringLiteral("actionRectangleBoard"));
    connect(rectangleAction, &QAction::triggered, this, &MainWindow::makeRectangleBoard);
    addBoardAction(BoardEditor::Action::PlaceHole, "actionPlaceHole",
                   &BoardEditor::startPlacingHoles);
    QAction *layTrack = addBoardAction(BoardEditor::Action::LayTrack, "actionLayTrack",
                                       &BoardEditor::startLayingTracks);
    layTrack->setShortcut(QKeySequence(Qt::Key_T));
    addBoardAction(BoardEditor::Action::ViaAndOtherSide, "actionVia",
                   &BoardEditor::viaAndOtherSide)
        ->setShortcut(QKeySequence(Qt::Key_V));
    QAction *routeAll = editMenu->addAction(tr("Route all"));
    routeAll->setObjectName(QStringLiteral("actionRouteAll"));
    connect(routeAll, &QAction::triggered, this, [this] { routeBoard(false); });
    QAction *routeChosen = editMenu->addAction(tr("Route chosen"));
    routeChosen->setObjectName(QStringLiteral("actionRouteChosen"));
    connect(routeChosen, &QAction::triggered, this, [this] { routeBoard(true); });

    QMenu *viewMenu = m_mainMenu->addMenu(tr("&View"));

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

    // A table of pale colours, each a piece of paper with a wire and a
    // symbol on it; its code is its tip. The sheet shown takes the colour
    // under the mouse until the mouse leaves it or the menu closes.
    QMenu *backgroundMenu = viewMenu->addMenu(tr("Sheet background"));
    connect(backgroundMenu, &QMenu::aboutToHide, this, &MainWindow::updateCanvasBackground);
    QWidget *table = new QWidget(backgroundMenu);
    table->setStyleSheet(QString::fromLatin1(SWATCH_STYLE).arg(paintcolor::selection().name()));
    QGridLayout *grid = new QGridLayout(table);
    grid->setSpacing(SWATCH_SPACING);
    grid->setContentsMargins(SWATCH_SPACING, SWATCH_SPACING, SWATCH_SPACING, SWATCH_SPACING);
    const QColor background = AppSettings::sheetBackground();
    const QList<QColor> colors = AppSettings::sheetBackgrounds();
    for (int i = 0; i < colors.size(); ++i) {
        const QColor color = colors.at(i);
        QToolButton *button = new QToolButton(table);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setChecked(color == background);
        button->setIcon(QIcon(paperSwatch(color)));
        button->setIconSize(QSize(SWATCH_WIDTH, SWATCH_HEIGHT));
        button->setToolTip(color.name().toUpper());
        button->setProperty(SWATCH_COLOR_PROPERTY, color);
        button->installEventFilter(this);
        grid->addWidget(button, i / SWATCH_COLUMNS, i % SWATCH_COLUMNS);
        connect(button, &QToolButton::clicked, this, [this, color, viewMenu] {
            applySheetBackground(color);
            viewMenu->close();
        });
    }
    QWidgetAction *tableAction = new QWidgetAction(backgroundMenu);
    tableAction->setDefaultWidget(table);
    backgroundMenu->addAction(tableAction);

    // What the menu of a sheet's tab does, for the sheet shown.
    QMenu *sheetMenu = viewMenu->addMenu(tr("Sheet"));
    connect(sheetMenu, &QMenu::aboutToShow, this, [this, sheetMenu] {
        m_sheetTabs->fillMenu(sheetMenu, isSheetShown() ? m_designItem->page() : -1);
    });

    viewMenu->addSeparator();

    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_propertiesDock->toggleViewAction());
    viewMenu->addAction(m_netsDock->toggleViewAction());
    viewMenu->addAction(m_ercDock->toggleViewAction());
    viewMenu->addAction(m_drcDock->toggleViewAction());
    viewMenu->addAction(m_bomDock->toggleViewAction());
    viewMenu->addAction(m_versionsDock->toggleViewAction());
    viewMenu->addSeparator();

    QAction *zoomInAction = viewMenu->addAction(tr("Zoom in"));
    zoomInAction->setObjectName(QStringLiteral("actionZoomIn"));
    zoomInAction->setShortcut(QKeySequence::ZoomIn);
    connect(zoomInAction, &QAction::triggered, this, [this] {
        m_canvas->setView(m_canvas->viewCenter(), m_canvas->pixelsPerMm() * CanvasView::ZOOM_STEP);
    });
    QAction *zoomOutAction = viewMenu->addAction(tr("Zoom out"));
    zoomOutAction->setObjectName(QStringLiteral("actionZoomOut"));
    zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    connect(zoomOutAction, &QAction::triggered, this, [this] {
        m_canvas->setView(m_canvas->viewCenter(), m_canvas->pixelsPerMm() / CanvasView::ZOOM_STEP);
    });
    QAction *fitAction = viewMenu->addAction(tr("Fit the page"));
    fitAction->setObjectName(QStringLiteral("actionZoomFit"));
    fitAction->setShortcut(QKeySequence(Qt::Key_Home));
    connect(fitAction, &QAction::triggered, this, &MainWindow::fitView);
    QAction *fitDrawingAction = viewMenu->addAction(tr("Fit the drawing"));
    fitDrawingAction->setObjectName(QStringLiteral("actionZoomFitDrawing"));
    fitDrawingAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Home));
    connect(fitDrawingAction, &QAction::triggered, this, &MainWindow::fitDrawing);
    QAction *actualSizeAction = viewMenu->addAction(tr("Actual size"));
    actualSizeAction->setObjectName(QStringLiteral("actionActualSize"));
    actualSizeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    connect(actualSizeAction, &QAction::triggered, this, &MainWindow::showActualSize);
    viewMenu->addSeparator();

    QAction *warpAction = viewMenu->addAction(tr("Center and warp cursor on zoom"));
    warpAction->setCheckable(true);
    warpAction->setChecked(m_canvas->zoomCenterAndWarp());
    connect(warpAction, &QAction::toggled, this, &MainWindow::applyZoomCenterAndWarp);

    QAction *coordinatesAction = viewMenu->addAction(tr("Coordinates while drawing the board"));
    coordinatesAction->setObjectName(QStringLiteral("actionDrawingCoordinates"));
    coordinatesAction->setCheckable(true);
    coordinatesAction->setChecked(AppSettings::drawingCoordinatesShown());
    connect(coordinatesAction, &QAction::toggled, this, [this](bool on) {
        AppSettings::setDrawingCoordinatesShown(on);
        m_designItem->setDrawingCoordinatesShown(on);
    });

    // What the board shows, on the screen only. The silkscreen of both
    // sides hidden: pads, holes and copper stay.
    QAction *silkscreen = viewMenu->addAction(tr("Hide silkscreen"));
    silkscreen->setObjectName(QStringLiteral("actionSilkscreen"));
    silkscreen->setCheckable(true);
    silkscreen->setChecked(!m_designItem->boardLayers().contains(BoardLayer::SilkscreenTop));
    connect(silkscreen, &QAction::toggled, this, [this](bool hidden) {
        const bool shown = !hidden;
        BoardLayerSet layers = m_designItem->boardLayers();
        layers.set(BoardLayer::SilkscreenTop, shown);
        layers.set(BoardLayer::SilkscreenBottom, shown);
        m_designItem->setBoardLayers(layers);
        m_boardEditor->setSilkscreenShown(shown);
    });
    // The tracks and vias hidden for a while, to see what is still to be
    // joined; laying a track shows them again.
    QAction *hideTracks = viewMenu->addAction(tr("Hide tracks"));
    hideTracks->setObjectName(QStringLiteral("actionHideTracks"));
    hideTracks->setCheckable(true);
    connect(hideTracks, &QAction::toggled, this, [this](bool hidden) {
        m_boardEditor->setTracksShown(!hidden);
        updateTools();
    });
    connect(layTrack, &QAction::triggered, hideTracks,
            [hideTracks] { hideTracks->setChecked(false); });
    // What is still to be joined, bold and with its ends marked.
    QAction *unrouted = viewMenu->addAction(tr("Bold unrouted"));
    unrouted->setObjectName(QStringLiteral("actionShowUnrouted"));
    unrouted->setCheckable(true);
    connect(unrouted, &QAction::toggled, this,
            [this](bool bright) { m_designItem->setRatsnestBright(bright); });

    QAction *arcsAction = viewMenu->addAction(tr("Unrouted as arcs"));
    arcsAction->setObjectName(QStringLiteral("actionRatsnestArcs"));
    arcsAction->setCheckable(true);
    arcsAction->setChecked(AppSettings::ratsnestArcs());
    connect(arcsAction, &QAction::toggled, this, [this](bool on) {
        AppSettings::setRatsnestArcs(on);
        m_designItem->setRatsnestArcs(on);
    });

    // The language of the interface, each named in itself; it is taken at
    // the next start.
    QMenu *settingsMenu = m_mainMenu->addMenu(tr("&Settings"));
    QMenu *languageMenu = settingsMenu->addMenu(tr("Language"));
    QActionGroup *languageGroup = new QActionGroup(this);
    const QString language = AppSettings::language();
    QAction *systemAction =
        addChoice(languageMenu, languageGroup, tr("System language"), language.isEmpty());
    connect(systemAction, &QAction::triggered, this, [this] { setInterfaceLanguage(QString()); });
    languageMenu->addSeparator();
    for (const QString &code : supportedLanguages()) {
        QAction *action =
            addChoice(languageMenu, languageGroup, languageName(code), language == code);
        connect(action, &QAction::triggered, this, [this, code] { setInterfaceLanguage(code); });
    }

    QAction *screenAction = settingsMenu->addAction(ScreenSizeDialog::title() + QStringLiteral("…"));
    screenAction->setObjectName(QStringLiteral("actionScreenSize"));
    connect(screenAction, &QAction::triggered, this, &MainWindow::chooseScreenSize);

    // How wires follow what they are joined to: a few ways tried, or the
    // cheapest way round everything on the grid.
    QMenu *routingMenu = settingsMenu->addMenu(tr("Wire routing"));
    QActionGroup *routingGroup = new QActionGroup(this);
    const WireRouting routing = AppSettings::wireRouting();
    QAction *handAction = addChoice(routingMenu, routingGroup, tr("Simple"),
                                    routing == WireRouting::Hand);
    connect(handAction, &QAction::triggered, this,
            [this] { applyWireRouting(WireRouting::Hand); });
    QAction *gridAction = addChoice(routingMenu, routingGroup, tr("Around obstacles"),
                                    routing == WireRouting::Grid);
    connect(gridAction, &QAction::triggered, this,
            [this] { applyWireRouting(WireRouting::Grid); });

    QAction *projectsAction = settingsMenu->addAction(tr("Projects folder") + QStringLiteral("…"));
    projectsAction->setObjectName(QStringLiteral("actionProjectsFolder"));
    connect(projectsAction, &QAction::triggered, this, &MainWindow::chooseProjectsFolder);

    QAction *historyAction = settingsMenu->addAction(tr("Keep a history of steps"));
    historyAction->setCheckable(true);
    historyAction->setChecked(AppSettings::keepStepHistory());
    connect(historyAction, &QAction::toggled, this, &AppSettings::setKeepStepHistory);

    QMenu *helpMenu = m_mainMenu->addMenu(tr("&Help"));
    QAction *guideAction = helpMenu->addAction(tr("User guide"));
    guideAction->setObjectName(QStringLiteral("actionUserGuide"));
    guideAction->setShortcut(QKeySequence::HelpContents);
    connect(guideAction, &QAction::triggered, this, &MainWindow::showUserGuide);
    QAction *aboutAction = helpMenu->addAction(aboutTitle() + QStringLiteral("…"));
    aboutAction->setObjectName(QStringLiteral("actionAbout"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::setInterfaceLanguage(const QString &language)
{
    if (language == AppSettings::language())
        return;
    AppSettings::setLanguage(language);
    QMessageBox::information(this, tr("Language"),
                             tr("The language changes after a restart") + QLatin1Char('.'));
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

    // How much of the board is still to be routed, while it is shown.
    m_unroutedLabel = new QLabel(this);
    m_unroutedLabel->setObjectName(QStringLiteral("unroutedLabel"));
    m_unroutedLabel->setStyleSheet(QLatin1String(UNROUTED_STYLE));
    statusBar()->addPermanentWidget(m_unroutedLabel);

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

void MainWindow::showSheetMenu(const QPoint &globalPos)
{
    QPointF at;
    if (m_sheetEditor->sheet() < 0 || !m_canvas->sceneAtGlobal(globalPos, at))
        return;
    const ObjectId editable = m_sheetEditor->chooseForMenu(at);
    QMenu menu(this);
    menu.setStyleSheet(
        QString::fromLatin1(SHEET_MENU_STYLE).arg(paintcolor::selection().name()));
    // The items of the Edit menu, with their keys.
    const auto add = [this, &menu](const char *name) {
        if (QAction *action = findChild<QAction *>(QLatin1String(name)))
            menu.addAction(action);
    };
    // Wires crossing at the point clicked, joined there or not.
    if (m_sheetEditor->canJoinWiresAt(at)) {
        QAction *join = menu.addAction(SheetEditor::actionName(SheetEditor::Action::JoinWires));
        connect(join, &QAction::triggered, m_sheetEditor,
                [this, at] { m_sheetEditor->joinWiresAt(at); });
        menu.addSeparator();
    } else if (m_sheetEditor->canSeparateWiresAt(at)) {
        QAction *separate =
            menu.addAction(SheetEditor::actionName(SheetEditor::Action::SeparateWires));
        connect(separate, &QAction::triggered, m_sheetEditor,
                [this, at] { m_sheetEditor->separateWiresAt(at); });
        menu.addSeparator();
    }
    if (m_sheetEditor->hasChosen()) {
        if (!editable.isNull()) {
            QAction *edit = menu.addAction(m_sheetEditor->editInPlaceName(editable));
            connect(edit, &QAction::triggered, m_sheetEditor,
                    [this, editable] { m_sheetEditor->editInPlace(editable); });
            if (m_sheetEditor->isBus(editable)) {
                QAction *name = menu.addAction(tr("Add name here"));
                connect(name, &QAction::triggered, m_sheetEditor,
                        [this, editable, at] { m_sheetEditor->addBusName(editable, at); });
            }
            menu.addSeparator();
        }
        // The rated power of the resistors chosen, without the panel.
        const QList<Component> chosen = m_sheetEditor->chosenComponents();
        if (PropertiesPanel::havePower(chosen, m_document->design().library)) {
            QMenu *power = menu.addMenu(PropertiesPanel::powerTitle());
            QSet<ObjectId> ids;
            for (const Component &component : chosen)
                ids.insert(component.id);
            for (const auto &[text, powerMw] : PropertiesPanel::powerChoices()) {
                QAction *choice = power->addAction(text);
                choice->setCheckable(true);
                choice->setChecked(std::all_of(chosen.cbegin(), chosen.cend(),
                                               [powerMw](const Component &c) {
                                                   return c.powerMw == powerMw;
                                               }));
                connect(choice, &QAction::triggered, m_sheetEditor,
                        [this, ids, powerMw] { m_sheetEditor->setComponentPower(ids, powerMw); });
            }
            menu.addSeparator();
        }
        add("actionRotate");
        add("actionMirror");
        add("actionDelete");
        // Locking what is chosen, or unlocking it when all of it is locked.
        if (m_sheetEditor->canLockChosen()) {
            const bool locked = m_sheetEditor->chosenLocked();
            QAction *lock = menu.addAction(lockName(!locked));
            connect(lock, &QAction::triggered, this, [this, locked] {
                m_sheetEditor->setChosenLocked(!locked);
                updateTools();
            });
        }
        menu.addSeparator();
        add("actionCopy");
        add("actionCut");
        add("actionDuplicate");
        // The panel of properties, shown, the keyboard in its first field.
        if (!chosen.isEmpty()) {
            menu.addSeparator();
            QAction *properties = menu.addAction(PropertiesPanel::propertiesTitle());
            connect(properties, &QAction::triggered, this, [this] {
                m_propertiesDock->show();
                m_propertiesDock->raise();
                m_propertiesPanel->focusFirstField();
            });
        }
    } else {
        add("actionPaste");
        menu.addSeparator();
        add("actionWire");
        add("actionBus");
        add("actionPlaceLabel");
        add("actionPlacePower");
        menu.addSeparator();
        add("actionSelectAll");
    }
    menu.exec(globalPos);
}

void MainWindow::showBoardMenu(const QPoint &globalPos)
{
    QPointF at;
    if (!m_canvas->sceneAtGlobal(globalPos, at))
        return;
    const BoardEditor::MenuTarget target = m_boardEditor->chooseForMenu(at);
    QMenu menu(this);
    menu.setStyleSheet(
        QString::fromLatin1(SHEET_MENU_STYLE).arg(paintcolor::selection().name()));
    const auto add = [this, &menu](const char *name) {
        if (QAction *action = findChild<QAction *>(QLatin1String(name)))
            menu.addAction(action);
    };
    // Locking what is chosen, or unlocking it when all of it is locked.
    const auto addLock = [this, &menu] {
        const bool locked = m_boardEditor->chosenLocked();
        QAction *lock = menu.addAction(lockName(!locked));
        connect(lock, &QAction::triggered, this, [this, locked] {
            m_boardEditor->setChosenLocked(!locked);
            updateTools();
        });
    };
    switch (target) {
    case BoardEditor::MenuTarget::Placement: {
        add("actionRotate");
        addLock();
        const bool shown = m_boardEditor->chosenValuesShown();
        QAction *value = menu.addAction(BoardEditor::actionName(
            shown ? BoardEditor::Action::HideValue : BoardEditor::Action::ShowValue));
        connect(value, &QAction::triggered, m_boardEditor,
                [this, shown] { m_boardEditor->setChosenValuesShown(!shown); });
        // The components of the parts chosen, on their sheet.
        QSet<ObjectId> components;
        for (const Placement &placement : m_document->design().board.placements) {
            if (m_boardEditor->chosenPlacements().contains(placement.id))
                components.insert(placement.component);
        }
        // Silkscreen lines from the texts to the parts.
        QAction *line =
            menu.addAction(BoardEditor::actionName(BoardEditor::Action::LineToPart));
        line->setCheckable(true);
        line->setChecked(m_boardEditor->chosenLined());
        connect(line, &QAction::triggered, m_boardEditor, &BoardEditor::setChosenLined);
        QAction *show = menu.addAction(tr("Show on schematic"));
        connect(show, &QAction::triggered, this,
                [this, components] { showComponents(components); });
        // Moves them off the board (BoardEditor::deleteChosen()).
        menu.addSeparator();
        add("actionDelete");
        break;
    }
    case BoardEditor::MenuTarget::Hole: {
        addLock();
        // A locked hole keeps its diameter, plating and ring.
        const bool locked = m_boardEditor->chosenHolesLocked();
        QMenu *diameter =
            menu.addMenu(BoardEditor::actionName(BoardEditor::Action::HoleDiameter));
        diameter->setEnabled(!locked);
        fillHoleMenu(diameter, true);
        const bool plated = m_boardEditor->chosenHolesPlated();
        QAction *plating =
            menu.addAction(BoardEditor::actionName(BoardEditor::Action::PlatedHoles));
        plating->setCheckable(true);
        plating->setChecked(plated);
        plating->setEnabled(!locked);
        connect(plating, &QAction::triggered, m_boardEditor,
                &BoardEditor::setChosenHolesPlated);
        QAction *ring = menu.addAction(
            BoardEditor::actionName(BoardEditor::Action::CopperRing) + QStringLiteral("…"));
        ring->setEnabled(!locked);
        connect(ring, &QAction::triggered, m_boardEditor, &BoardEditor::editMenuHolePad);
        // The side of the pad, on a board of two sides; a plated hole has
        // it on both.
        const bool pads = m_boardEditor->chosenHolesHavePads();
        if (m_document->design().board.copper == CopperSides::Both) {
            QMenu *sides = menu.addMenu(BoardEditor::actionName(BoardEditor::Action::PadSides));
            sides->setEnabled(!locked && pads && m_boardEditor->chosenHolesPlatedNone());
            const std::optional<CopperSides> current = m_boardEditor->chosenHolesPadSides();
            const QList<QPair<CopperSides, QString>> choices = {
                {CopperSides::Both, tr("Both sides")},
                {CopperSides::Top, tr("Top")},
                {CopperSides::Bottom, tr("Bottom")}};
            for (const auto &[value, text] : choices) {
                QAction *choice = sides->addAction(text);
                choice->setCheckable(true);
                choice->setChecked(current == value);
                connect(choice, &QAction::triggered, m_boardEditor,
                        [this, value] { m_boardEditor->setChosenHolesPadSides(value); });
            }
        }
        QMenu *net = menu.addMenu(BoardEditor::actionName(BoardEditor::Action::PadNet));
        net->setEnabled(!locked && pads);
        fillPadNetMenu(net);
        menu.addSeparator();
        add("actionDelete");
        break;
    }
    case BoardEditor::MenuTarget::Track: {
        // Width and side for the tracks chosen; a via has neither.
        const Board &board = m_document->design().board;
        const QSet<ObjectId> chosen = m_boardEditor->chosenTracks();
        bool onTop = false;
        bool onBottom = false;
        for (const Track &track : board.tracks) {
            if (chosen.contains(track.id))
                (track.side == BoardSide::Top ? onTop : onBottom) = true;
        }
        const bool tracks = onTop || onBottom;
        QMenu *width = menu.addMenu(BoardEditor::actionName(BoardEditor::Action::TrackWidth));
        width->setEnabled(tracks);
        const Coord current = m_boardEditor->chosenTracksWidth();
        for (const Coord size : m_document->design().project.rules.trackWidths) {
            QAction *choice = width->addAction(formatMmShortUnit(size));
            choice->setCheckable(true);
            choice->setChecked(size == current);
            connect(choice, &QAction::triggered, m_boardEditor,
                    [this, size] { m_boardEditor->setChosenTracksWidth(size); });
        }
        width->addSeparator();
        QAction *other = width->addAction(tr("Other") + QStringLiteral("…"));
        connect(other, &QAction::triggered, m_boardEditor, &BoardEditor::editMenuTrackWidth);
        if (board.copper == CopperSides::Both) {
            QMenu *layer = menu.addMenu(BoardEditor::actionName(BoardEditor::Action::TrackSide));
            layer->setEnabled(tracks);
            for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
                QAction *choice = layer->addAction(BoardEditor::sideName(side));
                choice->setCheckable(true);
                choice->setChecked(side == BoardSide::Top ? onTop && !onBottom
                                                          : onBottom && !onTop);
                connect(choice, &QAction::triggered, m_boardEditor,
                        [this, side] { m_boardEditor->setChosenTracksSide(side); });
            }
        }
        QAction *spread =
            menu.addAction(BoardEditor::actionName(BoardEditor::Action::SpreadTracks));
        spread->setEnabled(tracks);
        connect(spread, &QAction::triggered, m_boardEditor, &BoardEditor::spreadChosenTracks);
        if (m_boardEditor->linksChosen()) {
            QAction *replace =
                menu.addAction(BoardEditor::actionName(BoardEditor::Action::ReplaceLinks));
            connect(replace, &QAction::triggered, m_boardEditor,
                    &BoardEditor::replaceChosenLinks);
        }
        menu.addSeparator();
        add("actionDelete");
        break;
    }
    case BoardEditor::MenuTarget::Corner:
    case BoardEditor::MenuTarget::Side:
        addLock();
        if (target == BoardEditor::MenuTarget::Corner && !m_boardEditor->isOutlineLocked()) {
            QAction *round =
                menu.addAction(BoardEditor::actionName(BoardEditor::Action::RoundCorner));
            connect(round, &QAction::triggered, m_boardEditor,
                    &BoardEditor::editMenuCornerRadius);
        }
        break;
    case BoardEditor::MenuTarget::None:
        // What the board shows.
        add("actionSilkscreen");
        add("actionHideTracks");
        add("actionShowUnrouted");
        add("actionRatsnestArcs");
        menu.addSeparator();
        {
            QAction *spread =
                menu.addAction(BoardEditor::actionName(BoardEditor::Action::SpreadTracks));
            spread->setEnabled(!m_document->design().board.tracks.isEmpty());
            connect(spread, &QAction::triggered, m_boardEditor, &BoardEditor::spreadTracks);
        }
        {
            const QString title = BoardEditor::actionName(BoardEditor::Action::DeleteAllTracks);
            QAction *clear = menu.addAction(title + QStringLiteral("…"));
            const Board &board = m_document->design().board;
            clear->setEnabled(!board.tracks.isEmpty() || !board.vias.isEmpty());
            connect(clear, &QAction::triggered, this, [this, title] {
                QMessageBox box(QMessageBox::NoIcon, title,
                                title + QLatin1Char('?'),
                                QMessageBox::Yes | QMessageBox::No, this);
                box.setIconPixmap(questionPixmap(paintcolor::deleteQuestion(), QUESTION_PIXELS,
                                                 devicePixelRatioF()));
                if (box.exec() == QMessageBox::Yes)
                    m_boardEditor->deleteAllTracks();
            });
        }
        break;
    }
    menu.exec(globalPos);
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

void MainWindow::applySheetBackground(const QColor &color)
{
    AppSettings::setSheetBackground(color);
    updateCanvasBackground();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
        const QVariant color = watched->property(SWATCH_COLOR_PROPERTY);
        if (color.isValid() && event->type() == QEvent::Enter)
            previewSheetBackground(color.value<QColor>());
        else if (color.isValid())
            updateCanvasBackground();
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::previewSheetBackground(const QColor &color)
{
    if (isSheetShown())
        m_canvas->setBackgroundColor(color);
}

bool MainWindow::isSheetShown() const
{
    return m_designItem->isVisible() && m_designItem->page() != m_designItem->boardPage();
}

void MainWindow::updateCanvasBackground()
{
    if (isSheetShown())
        m_canvas->setBackgroundColor(AppSettings::sheetBackground());
    else if (m_designItem->isVisible())
        m_canvas->setBackgroundColor(paintcolor::boardBackground());
    else
        m_canvas->setBackgroundColor(paintcolor::background());
}

void MainWindow::applyZoomCenterAndWarp(bool on)
{
    m_canvas->setZoomCenterAndWarp(on);
    AppSettings::setZoomCenterAndWarp(on);
}

void MainWindow::applyWireRouting(WireRouting routing)
{
    m_sheetEditor->setWireRouting(routing);
    AppSettings::setWireRouting(routing);
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
    // Opened from the board, the project opens on its board.
    const bool fromBoard = m_boardEditor->isActive();
    storeViews();
    if (!m_document->open(path, error, &problems)) {
        showStatus(error);
        return false;
    }
    m_views = readProjectView(path);
    m_shownViewKey.clear();
    showPage(fromBoard ? m_designItem->boardPage() : 0);
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

void MainWindow::chooseProjectsFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Projects folder"),
                                                             AppSettings::projectsFolder());
    if (folder.isEmpty())
        return;
    AppSettings::setProjectsFolder(folder);
    // The dialogs start there from now on.
    AppSettings::setLastProjectFolder(folder);
}

void MainWindow::chooseNewProject()
{
    if (confirmLeavingProject())
        newProject();
}

void MainWindow::chooseProjectToOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open project"), AppSettings::projectDialogFolder(),
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
    fillRecentMenu(m_recentMenu);
    const bool any = !AppSettings::recentProjects().isEmpty();
    m_recentMenu->setEnabled(any);
    if (QAction *last = findChild<QAction *>(QStringLiteral("actionOpenLastProject")))
        last->setEnabled(any);
}

QString MainWindow::lastProjectTitle()
{
    return tr("Open last project");
}

void MainWindow::openLastProject()
{
    const QStringList paths = AppSettings::recentProjects();
    if (paths.isEmpty()) {
        showStatus(tr("No recent projects"));
        return;
    }
    openRecentProject(paths.first());
}

void MainWindow::fillRecentMenu(QMenu *menu)
{
    menu->clear();
    const QStringList paths = AppSettings::recentProjects();
    if (paths.isEmpty()) {
        menu->addAction(tr("No recent projects"))->setEnabled(false);
        return;
    }
    for (const QString &path : paths) {
        QAction *action = menu->addAction(QFileInfo(path).completeBaseName());
        action->setToolTip(QDir::toNativeSeparators(path));
        connect(action, &QAction::triggered, this, [this, path] { openRecentProject(path); });
    }
    menu->addSeparator();
    QAction *clearAction = menu->addAction(tr("Clear list"));
    clearAction->setObjectName(QStringLiteral("actionClearRecentProjects"));
    connect(clearAction, &QAction::triggered, this, [] { AppSettings::clearRecentProjects(); });
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

void MainWindow::setRevision(const QString &revision)
{
    if (revision == m_document->design().project.titleBlock.revision)
        return;
    Design changed = m_document->design();
    changed.project.titleBlock.revision = revision;
    m_document->change(revision.isEmpty() ? revisionWord()
                                          : revisionWord() + QLatin1Char(' ') + revision,
                       changed);
}

void MainWindow::exportGerberFiles()
{
    QString message = gerberExportProblem(m_document->design());
    if (message.isEmpty()) {
        QString revision = m_document->design().project.titleBlock.revision;
        if (!askGerberRevision(this, revision))
            return;
        setRevision(revision);
        // What goes to the factory is saved, and kept as a version.
        if ((m_document->projectPath().isEmpty() || m_document->isModified()) && !saveProject())
            return;
        if (exportGerber(*m_document, message)) {
            QString versionPath;
            QString error;
            if (!saveChangedProjectVersion(m_document->projectPath(),
                                           m_document->design().project,
                                           QStringLiteral("Gerber"),
                                           QDateTime::currentDateTime(), versionPath, error)) {
                QMessageBox::warning(this, exportGerberTitle(),
                                     message + QLatin1Char('\n') + error);
                return;
            }
            if (!versionPath.isEmpty()) {
                message += QLatin1Char('\n') + writtenMessage(versionPath);
                m_versionsPanel->refresh();
            }
            // The work after the files went to the factory goes on under the
            // next revision, when the user says so.
            QMessageBox box(QMessageBox::Information, exportGerberTitle(), message,
                            QMessageBox::Ok, this);
            box.setObjectName(QStringLiteral("gerberDoneBox"));
            const QString next = nextRevision(revision);
            QAbstractButton *nextButton =
                box.addButton(tr("Start revision %1").arg(next), QMessageBox::ActionRole);
            nextButton->setObjectName(QStringLiteral("startNextRevision"));
            box.exec();
            if (box.clickedButton() == nextButton)
                setRevision(next);
            return;
        }
    }
    QMessageBox::warning(this, exportGerberTitle(), message);
}

bool MainWindow::saveVersion()
{
    bool ok = false;
    const QString description =
        QInputDialog::getText(this, tr("Save version"), tr("What changed") + QLatin1Char(':'),
                              QLineEdit::Normal, QString(), &ok)
            .trimmed();
    if (!ok)
        return false;
    if ((m_document->projectPath().isEmpty() || m_document->isModified()) && !saveProject())
        return false;
    QString path;
    QString error;
    if (!saveProjectVersion(m_document->projectPath(), m_document->design().project, description,
                            QDateTime::currentDateTime(), path, error)) {
        QMessageBox::warning(this, tr("Save version"), error);
        return false;
    }
    showStatus(writtenMessage(path));
    m_versionsPanel->refresh();
    return true;
}

void MainWindow::restoreVersion(int number, const QString &path)
{
    // What is not kept in a version yet may be kept first.
    bool changed = m_document->isModified();
    QString error;
    if (!changed
        && !projectChangedSinceVersion(m_document->projectPath(), m_document->design().project,
                                       changed, error))
        changed = true;
    if (changed) {
        QMessageBox box(QMessageBox::Question, VersionsPanel::restoreTitle(),
                        tr("Save the current project as a version first") + QLatin1Char('?'),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, this);
        box.setObjectName(QStringLiteral("saveBeforeRestoreBox"));
        box.setDefaultButton(QMessageBox::Yes);
        const int answer = box.exec();
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Yes && !saveVersion()))
            return;
    }
    Design design;
    if (!readProjectVersion(path, design, error)) {
        QMessageBox::warning(this, VersionsPanel::restoreTitle(), error);
        return;
    }
    m_document->change(tr("Restore version %1").arg(number), design);
}

bool MainWindow::chooseProjectToSaveAs()
{
    const QString path = m_document->projectPath();
    const QString name = path.isEmpty() ? tr("untitled") : QFileInfo(path).completeBaseName();
    const QString parentFolder = AppSettings::projectDialogFolder();

    SaveProjectDialog dialog(name, parentFolder, this);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    AppSettings::setLastProjectFolder(dialog.parentFolder());
    return saveProjectAs(dialog.parentFolder(), dialog.name());
}

void MainWindow::updatePartsFromLibrary()
{
    // A copy: the dialog waits for the user.
    const Design design = m_document->design();
    const QList<PartDifference> differences = partDifferences(design, m_library.items);
    if (differences.isEmpty()) {
        QMessageBox::information(this, UpdatePartsDialog::title(),
                                 UpdatePartsDialog::nothingToUpdate() + QLatin1Char('.'));
        return;
    }
    UpdatePartsDialog dialog(differences, m_library.items, this);
    if (dialog.exec() != QDialog::Accepted || dialog.chosenParts().isEmpty())
        return;
    m_document->change(UpdatePartsDialog::title(),
                       withPartsUpdated(design, m_library.items, dialog.chosenParts()));
}

void MainWindow::updateBoardFromSchematic()
{
    const BoardUpdate update = boardUpdatedFromSchematic(m_document->design());
    if (!update.sharedNames.isEmpty()) {
        QMessageBox::warning(this, UpdateBoardDialog::title(),
                             UpdateBoardDialog::sharedNamesText(update.sharedNames));
        return;
    }
    if (update.changes.isEmpty()) {
        QMessageBox::information(this, UpdateBoardDialog::title(),
                                 UpdateBoardDialog::nothingToUpdate() + QLatin1Char('.'));
        return;
    }
    UpdateBoardDialog dialog(update.changes, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    Design design = m_document->design();
    design.board = update.board;
    design.project = update.project;
    const int board = m_designItem->boardPage();
    m_document->change(UpdateBoardDialog::title(), design, board);
    showPage(board);
}

void MainWindow::editProjectProperties()
{
    ProjectPropertiesDialog dialog(m_document->design(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_document->change(ProjectPropertiesDialog::title(), dialog.design());
}

void MainWindow::showComponents(const QSet<ObjectId> &ids)
{
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    for (int page = 0; page < sheets.size(); ++page) {
        for (const Component &component : sheets.at(page).components) {
            if (ids.contains(component.id)) {
                showPage(page);
                m_sheetEditor->setSelection(ids);
                return;
            }
        }
    }
}

void MainWindow::showPage(int page, bool fit)
{
    rememberView();
    showProject();
    m_designItem->setPage(page);
    m_shownViewKey = pageViewKey(m_designItem->page());
    updateCanvasBackground();
    // The tab follows the page, not the other way round.
    m_sheetTabs->setCurrentPage(m_designItem->page());
    const int shown = m_designItem->page();
    m_sheetEditor->setSheet(shown == m_designItem->boardPage() ? -1 : shown);
    m_boardEditor->setActive(shown == m_designItem->boardPage());
    applyProjectGrid();
    updateUnrouted();
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

void MainWindow::createToolBar()
{
    QToolBar *bar = new QToolBar(tr("Tools"), this);
    bar->setObjectName(QStringLiteral("mainBar"));
    bar->setMovable(false);
    // The menu is on it: it cannot be hidden.
    bar->toggleViewAction()->setVisible(false);
    bar->setIconSize(QSize(TOOL_ICON_SIZE, TOOL_ICON_SIZE));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::TopToolBarArea, bar);
    const QColor color = bar->palette().color(QPalette::WindowText);
    const QColor pressed = bar->palette().color(QPalette::Highlight);
    const auto rgba = [](const QColor &c, int alpha) {
        return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
    };
    bar->setStyleSheet(
        QStringLiteral("QToolButton { border: 1px solid transparent; border-radius: 3px; "
                       "padding: 2px; background: transparent; }"
                       "QToolButton:hover { background-color: %1; }"
                       "QToolButton:pressed { background-color: %2; }"
                       "QToolButton:checked { background-color: %3; border-color: %4; }"
                       "QToolButton::menu-indicator { image: none; }"
                       "QToolButton[popupMode=\"1\"] { padding-right: %5px; }"
                       "QToolButton::menu-button { border: none; width: %5px; }")
            .arg(rgba(color, HOVER_ALPHA), rgba(color, HELD_ALPHA), rgba(pressed, PRESSED_ALPHA),
                 pressed.name(), QString::number(MENU_ARROW_WIDTH)));

    // The menus hold the actions but are not on the window: the window
    // takes their keys itself.
    const std::function<void(const QMenu *)> takeKeys = [this, &takeKeys](const QMenu *menu) {
        for (QAction *action : menu->actions()) {
            if (action->menu() != nullptr)
                takeKeys(action->menu());
            else if (!action->shortcuts().isEmpty())
                addAction(action);
        }
    };
    takeKeys(m_mainMenu);

    QToolButton *menuButton = new QToolButton(bar);
    menuButton->setObjectName(QStringLiteral("mainMenuButton"));
    menuButton->setIcon(toolIcon(ToolIcon::Menu, color));
    menuButton->setMenu(m_mainMenu);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    const QKeySequence menuKey(Qt::Key_F10);
    menuButton->setToolTip(tr("Menu") + QStringLiteral(" (")
                           + menuKey.toString(QKeySequence::NativeText) + QLatin1Char(')'));
    bar->addWidget(menuButton);
    QAction *menuKeyAction = new QAction(this);
    menuKeyAction->setShortcut(menuKey);
    connect(menuKeyAction, &QAction::triggered, menuButton, &QToolButton::showMenu);
    addAction(menuKeyAction);

    // An action of the menus with its picture, and its key in its tip.
    const auto dress = [this, &color](const char *name, ToolIcon icon) {
        QAction *action = findChild<QAction *>(QLatin1String(name));
        Q_ASSERT(action != nullptr);
        action->setIcon(toolIcon(icon, color));
        QString tip = action->text();
        tip.remove(QLatin1Char('&'));
        if (!action->shortcut().isEmpty())
            tip += QStringLiteral(" (") + action->shortcut().toString(QKeySequence::NativeText)
                   + QLatin1Char(')');
        action->setToolTip(tip);
        return action;
    };
    // The same, put on the strip.
    const auto put = [bar, &dress](const char *name, ToolIcon icon) {
        QAction *action = dress(name, icon);
        bar->addAction(action);
        return action;
    };
    // A gap that grows with the window: the modes stay in the middle.
    const auto stretch = [bar] {
        QWidget *spacer = new QWidget(bar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        bar->addWidget(spacer);
    };

    // The recent projects, read again each time the button opens them: the
    // list may change in another window of the program as well.
    QMenu *recentMenu = new QMenu(this);
    recentMenu->setToolTipsVisible(true);
    connect(recentMenu, &QMenu::aboutToShow, this,
            [this, recentMenu] { fillRecentMenu(recentMenu); });
    // A click opens the last project, the arrow the list.
    QAction *lastTool = new QAction(lastProjectTitle(), this);
    lastTool->setObjectName(QStringLiteral("actionOpenLastProjectTool"));
    lastTool->setIcon(toolIcon(ToolIcon::Recent, color));
    lastTool->setToolTip(
        lastProjectTitle() + QStringLiteral(" (")
        + QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O).toString(QKeySequence::NativeText)
        + QStringLiteral("), ") + m_recentMenu->title().toLower());
    connect(lastTool, &QAction::triggered, this, &MainWindow::openLastProject);
    QToolButton *recentButton = new QToolButton(bar);
    recentButton->setObjectName(QStringLiteral("recentProjectsButton"));
    recentButton->setDefaultAction(lastTool);
    recentButton->setMenu(recentMenu);
    recentButton->setPopupMode(QToolButton::MenuButtonPopup);
    bar->addWidget(recentButton);
    // Without the dots of the menu item in its tip: the button opens it.
    QAction *properties = put("actionProjectProperties", ToolIcon::ProjectProperties);
    properties->setToolTip(ProjectPropertiesDialog::title());
    m_saveButton = qobject_cast<QToolButton *>(
        bar->widgetForAction(put("actionSaveProject", ToolIcon::Save)));
    // On red while something is not saved.
    updateSavedMark();
    // Print and PDF act on the page shown: the schematic's commands, or on
    // the board the dialog of its print (see updateTools()).
    struct PageTool
    {
        const char *name;
        ToolIcon icon;
        const char *schematic;
    };
    for (const PageTool &page : {PageTool{"actionPrintTool", ToolIcon::Print, "actionPrintSchematic"},
                                 PageTool{"actionPdfTool", ToolIcon::Pdf,
                                          "actionExportSchematicPdf"}}) {
        // The menu items keep their pictures.
        dress(page.schematic, page.icon);
        QAction *tool = new QAction(this);
        tool->setObjectName(QLatin1String(page.name));
        tool->setIcon(toolIcon(page.icon, color));
        const QString schematicName = QLatin1String(page.schematic);
        connect(tool, &QAction::triggered, this, [this, schematicName] {
            const QString target = m_boardEditor->isActive() ? QStringLiteral("actionPrintBoard")
                                                             : schematicName;
            findChild<QAction *>(target)->trigger();
        });
        bar->addAction(tool);
    }
    dress("actionPrintBoard", ToolIcon::Print);
    // A gap that does not grow.
    const auto gap = [bar] {
        QWidget *spacer = new QWidget(bar);
        spacer->setFixedWidth(GROUP_GAP);
        return bar->addWidget(spacer);
    };
    // Undo and Redo a gap away from the file buttons, the zoom buttons a
    // gap after them.
    gap();
    put("actionUndo", ToolIcon::Undo);
    put("actionRedo", ToolIcon::Redo);
    gap();
    put("actionZoomIn", ToolIcon::ZoomIn);
    put("actionZoomOut", ToolIcon::ZoomOut);
    put("actionZoomFit", ToolIcon::ZoomFit);
    put("actionZoomFitDrawing", ToolIcon::ZoomFitDrawing);
    put("actionActualSize", ToolIcon::ActualSize);
    // The grid lit while the button is held down.
    QAction *brightGrid = new QAction(tr("Brighter grid while held"), this);
    brightGrid->setObjectName(QStringLiteral("actionBrightGrid"));
    put("actionBrightGrid", ToolIcon::BrightGrid);
    if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(brightGrid))) {
        connect(button, &QToolButton::pressed, m_canvas, [this] { m_canvas->setGridBright(true); });
        connect(button, &QToolButton::released, m_canvas,
                [this] { m_canvas->setGridBright(false); });
    }
    stretch();

    // The modes in the middle, one pressed: those of the sheet editor or of
    // the board editor, whichever page is shown; Select serves both.
    // Update board from schematic, on the strip of the board a gap before
    // the lock: an action of its own, so that the File menu keeps its item.
    // Its gap goes with it.
    QAction *updateBoardTool = new QAction(
        findChild<QAction *>(QStringLiteral("actionUpdateBoard"))->text(), this);
    updateBoardTool->setObjectName(QStringLiteral("actionUpdateBoardTool"));
    updateBoardTool->setIcon(toolIcon(ToolIcon::UpdateBoard, color));
    QString updateTip = updateBoardTool->text();
    updateTip.remove(QStringLiteral("…"));
    updateBoardTool->setToolTip(updateTip + QStringLiteral(" (")
                                + QKeySequence(Qt::Key_F8).toString(QKeySequence::NativeText)
                                + QLatin1Char(')'));
    connect(updateBoardTool, &QAction::triggered, this, &MainWindow::updateBoardFromSchematic);
    bar->addAction(updateBoardTool);
    QAction *updateGap = gap();
    updateGap->setObjectName(QStringLiteral("updateBoardGap"));
    // The lock a gap before the modes.
    put("actionLock", ToolIcon::Lock);
    gap();
    QAction *select = new QAction(tr("Select"), this);
    select->setObjectName(QStringLiteral("actionSelectTool"));
    select->setCheckable(true);
    connect(select, &QAction::triggered, this, &MainWindow::chooseSelectTool);
    put("actionSelectTool", ToolIcon::Select);
    select->setToolTip(select->text() + QStringLiteral(" (")
                       + QKeySequence(Qt::Key_Escape).toString(QKeySequence::NativeText)
                       + QLatin1Char(')'));
    connect(select, &QAction::triggered, this, &MainWindow::updateTools);
    // After the action's own work: a click on a pressed button leaves it
    // pressed while the mode goes on.
    const auto addModes = [this](const QList<QPair<int, QAction *>> &modes,
                                 QHash<int, QAction *> &toolActions, QList<QAction *> &actions) {
        for (const auto &[tool, action] : modes) {
            action->setCheckable(true);
            toolActions.insert(tool, action);
            actions.append(action);
            connect(action, &QAction::triggered, this, &MainWindow::updateTools);
        }
    };
    addModes({{int(SheetEditor::Tool::Wire), put("actionWire", ToolIcon::Wire)},
              {int(SheetEditor::Tool::Bus), put("actionBus", ToolIcon::Bus)},
              {int(SheetEditor::Tool::Label), put("actionPlaceLabel", ToolIcon::Label)},
              {int(SheetEditor::Tool::Power), put("actionPlacePower", ToolIcon::Power)},
              {int(SheetEditor::Tool::NoConnect),
               put("actionPlaceNoConnect", ToolIcon::NoConnect)}},
             m_toolActions, m_sheetActions);
    // The outline of the board is drawn, cut and made a rectangle from one
    // button: it does what was chosen last from its arrow, the first of it,
    // Rectangular board, at first.
    QAction *rectangleBoard = put("actionRectangleBoard", ToolIcon::RectangleBoard);
    QAction *drawOutline = dress("actionDrawOutline", ToolIcon::Outline);
    QAction *cutOut = dress("actionCutOut", ToolIcon::CutOut);
    addModes({{int(BoardEditor::Tool::Outline), drawOutline},
              {int(BoardEditor::Tool::CutOut), cutOut}},
             m_boardToolActions, m_boardActions);
    m_boardActions.append(rectangleBoard);
    // A locked outline is not drawn anew, cut or made a rectangle (see
    // updateTools()); these are the actions it stops.
    m_outlineActions = m_boardActions;
    auto *outlineButton = qobject_cast<QToolButton *>(bar->widgetForAction(rectangleBoard));
    Q_ASSERT(outlineButton != nullptr);
    m_outlineButton = outlineButton;
    QMenu *outlineMenu = new QMenu(this);
    outlineMenu->addActions({rectangleBoard, cutOut, drawOutline});
    outlineButton->setMenu(outlineMenu);
    outlineButton->setPopupMode(QToolButton::MenuButtonPopup);
    connect(outlineMenu, &QMenu::triggered, outlineButton, &QToolButton::setDefaultAction);
    // Holes: the button places them, its arrow chooses their diameter.
    QAction *placeHole = put("actionPlaceHole", ToolIcon::Hole);
    addModes({{int(BoardEditor::Tool::Hole), placeHole},
              {int(BoardEditor::Tool::Track), put("actionLayTrack", ToolIcon::Track)}},
             m_boardToolActions, m_boardActions);
    // The via key is only in the menu, and only on the board.
    m_boardActions.append(findChild<QAction *>(QStringLiteral("actionVia")));
    // The layer tracks are laid on: a click goes to the other one, as V
    // does, the arrow chooses one; the picture shows the one chosen in front
    // (see updateTools()).
    QAction *layer = new QAction(tr("Layer"), this);
    layer->setObjectName(QStringLiteral("actionLayer"));
    layer->setToolTip(layer->text() + QStringLiteral(" (")
                      + QKeySequence(Qt::Key_V).toString(QKeySequence::NativeText)
                      + QLatin1Char(')'));
    connect(layer, &QAction::triggered, m_boardEditor, &BoardEditor::viaAndOtherSide);
    bar->addAction(layer);
    m_boardActions.append(layer);
    auto *layerButton = qobject_cast<QToolButton *>(bar->widgetForAction(layer));
    Q_ASSERT(layerButton != nullptr);
    QMenu *layerMenu = new QMenu(this);
    connect(layerMenu, &QMenu::aboutToShow, this, [this, layerMenu] {
        layerMenu->clear();
        for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
            QAction *choice = layerMenu->addAction(BoardEditor::sideName(side));
            choice->setCheckable(true);
            choice->setChecked(m_boardEditor->trackSide() == side);
            connect(choice, &QAction::triggered, m_boardEditor,
                    [this, side] { m_boardEditor->setTrackSide(side); });
        }
    });
    layerButton->setMenu(layerMenu);
    layerButton->setPopupMode(QToolButton::MenuButtonPopup);
    // The autorouter: the button lays every net, its arrow chooses which.
    QAction *autoroute =
        new QAction(BoardEditor::actionName(BoardEditor::Action::Autoroute), this);
    autoroute->setObjectName(QStringLiteral("actionAutoroute"));
    autoroute->setIcon(toolIcon(ToolIcon::Autoroute, color));
    connect(autoroute, &QAction::triggered, this, [this] { routeBoard(false); });
    bar->addAction(autoroute);
    QMenu *routeMenu = new QMenu(this);
    routeMenu->addActions({findChild<QAction *>(QStringLiteral("actionRouteAll")),
                           findChild<QAction *>(QStringLiteral("actionRouteChosen"))});
    auto *routeButton = qobject_cast<QToolButton *>(bar->widgetForAction(autoroute));
    Q_ASSERT(routeButton != nullptr);
    routeButton->setMenu(routeMenu);
    routeButton->setPopupMode(QToolButton::MenuButtonPopup);
    for (const char *name : {"actionAutoroute", "actionRouteAll", "actionRouteChosen"})
        m_boardActions.append(findChild<QAction *>(QLatin1String(name)));
    // What is shown of the board, in View and the menu of the empty board.
    for (const char *name : {"actionSilkscreen", "actionHideTracks", "actionShowUnrouted"})
        m_boardActions.append(findChild<QAction *>(QLatin1String(name)));
    auto *holeButton = qobject_cast<QToolButton *>(bar->widgetForAction(placeHole));
    Q_ASSERT(holeButton != nullptr);
    QMenu *holeMenu = new QMenu(this);
    connect(holeMenu, &QMenu::aboutToShow, this,
            [this, holeMenu] { fillHoleMenu(holeMenu, false); });
    holeButton->setMenu(holeMenu);
    holeButton->setPopupMode(QToolButton::MenuButtonPopup);
    // The arrow of the Power button chooses the symbol it places.
    QMenu *powerMenu = new QMenu(this);
    connect(powerMenu, &QMenu::aboutToShow, this, [this, powerMenu] { fillPowerMenu(powerMenu); });
    auto *powerButton = qobject_cast<QToolButton *>(
        bar->widgetForAction(m_toolActions.value(int(SheetEditor::Tool::Power))));
    Q_ASSERT(powerButton != nullptr);
    powerButton->setMenu(powerMenu);
    powerButton->setPopupMode(QToolButton::MenuButtonPopup);
    // In the menu and the menu of the sheet only, with their pictures.
    // Turning and deleting serve the board as well.
    dress("actionRotate", ToolIcon::Rotate);
    m_sheetActions.append(dress("actionMirror", ToolIcon::Mirror));
    dress("actionDelete", ToolIcon::Delete);

    // The switches of the panels, on the right.
    stretch();
    const QList<QPair<QAction *, ToolIcon>> panels = {
        {m_libraryDock->toggleViewAction(), ToolIcon::Library},
        {m_propertiesDock->toggleViewAction(), ToolIcon::Properties},
        {m_netsDock->toggleViewAction(), ToolIcon::Nets},
        {m_ercDock->toggleViewAction(), ToolIcon::Erc},
        {m_drcDock->toggleViewAction(), ToolIcon::Drc},
        {m_bomDock->toggleViewAction(), ToolIcon::Bom}};
    for (const auto &[action, icon] : panels) {
        action->setIcon(toolIcon(icon, color));
        bar->addAction(action);
    }

    connect(m_sheetEditor, &SheetEditor::changed, this, &MainWindow::updateTools);
    connect(m_boardEditor, &BoardEditor::changed, this, &MainWindow::updateTools);
    updateTools();
}

void MainWindow::updateTools()
{
    const bool sheet = m_sheetEditor->sheet() >= 0;
    const bool board = m_boardEditor->isActive();
    // The modes of the other page step aside, from the strip and the menu.
    for (QAction *action : std::as_const(m_sheetActions)) {
        action->setEnabled(sheet);
        action->setVisible(!board);
    }
    for (QAction *action : std::as_const(m_boardActions)) {
        action->setEnabled(board);
        action->setVisible(board);
    }
    const int sheetTool = int(m_sheetEditor->tool());
    for (auto it = m_toolActions.cbegin(); it != m_toolActions.cend(); ++it)
        it.value()->setChecked(sheet && it.key() == sheetTool);
    const int boardTool = int(m_boardEditor->tool());
    for (auto it = m_boardToolActions.cbegin(); it != m_boardToolActions.cend(); ++it)
        it.value()->setChecked(board && it.key() == boardTool);
    // A mode of the outline on shows on its button, pressed.
    if (QAction *on = m_boardToolActions.value(boardTool); board && on != nullptr
        && m_outlineActions.contains(on) && m_outlineButton != nullptr
        && m_outlineButton->defaultAction() != on)
        m_outlineButton->setDefaultAction(on);
    // A locked outline is not drawn anew, cut or made a rectangle.
    if (board && m_boardEditor->isOutlineLocked()) {
        for (QAction *action : std::as_const(m_outlineActions))
            action->setEnabled(false);
    }
    // The layer button shows the side chosen in front, and goes only on a
    // board with copper on both sides.
    if (QAction *layer = findChild<QAction *>(QStringLiteral("actionLayer"))) {
        const BoardSide side = m_boardEditor->trackSide();
        const BoardSide other = side == BoardSide::Top ? BoardSide::Bottom : BoardSide::Top;
        layer->setIcon(layerIcon(boardLayerColor(copperLayer(side)),
                                 boardLayerColor(copperLayer(other)),
                                 palette().color(QPalette::WindowText)));
        layer->setEnabled(board && m_document->design().board.copper == CopperSides::Both);
    }
    findChild<QAction *>(QStringLiteral("actionRotate"))->setEnabled(sheet || board);
    findChild<QAction *>(QStringLiteral("actionDelete"))->setEnabled(sheet || board);
    if (QAction *updateBoardTool = findChild<QAction *>(QStringLiteral("actionUpdateBoardTool"))) {
        updateBoardTool->setVisible(board);
        updateBoardTool->setEnabled(board);
    }
    if (QAction *updateGap = findChild<QAction *>(QStringLiteral("updateBoardGap")))
        updateGap->setVisible(board);
    QAction *lock = findChild<QAction *>(QStringLiteral("actionLock"));
    lock->setEnabled(board ? m_boardEditor->hasChosen() : sheet && m_sheetEditor->canLockChosen());
    lock->setChecked(board ? m_boardEditor->chosenLocked()
                           : sheet && m_sheetEditor->chosenLocked());
    // Ctrl+P prints the page shown; the strip's Print and PDF name what
    // they do there.
    QAction *printSchematicAction = findChild<QAction *>(QStringLiteral("actionPrintSchematic"));
    QAction *printBoardAction = findChild<QAction *>(QStringLiteral("actionPrintBoard"));
    printSchematicAction->setShortcut(board ? QKeySequence() : QKeySequence(QKeySequence::Print));
    printBoardAction->setShortcut(board ? QKeySequence(QKeySequence::Print) : QKeySequence());
    const QString printKey = QStringLiteral(" (")
                             + QKeySequence(QKeySequence::Print).toString(QKeySequence::NativeText)
                             + QLatin1Char(')');
    if (QAction *printTool = findChild<QAction *>(QStringLiteral("actionPrintTool")))
        printTool->setToolTip((board ? printBoardTitle() : printSchematicTitle()) + printKey);
    if (QAction *pdfTool = findChild<QAction *>(QStringLiteral("actionPdfTool")))
        pdfTool->setToolTip(board ? exportBoardPdfTitle() : exportSchematicPdfTitle());
    QAction *select = findChild<QAction *>(QStringLiteral("actionSelectTool"));
    select->setEnabled(sheet || board);
    select->setChecked((sheet && m_sheetEditor->tool() == SheetEditor::Tool::Select)
                       || (board && m_boardEditor->tool() == BoardEditor::Tool::Select));
}

void MainWindow::cancelEditing()
{
    if (m_boardEditor->isActive())
        m_boardEditor->cancel();
    else
        m_sheetEditor->cancel();
}

void MainWindow::chooseSelectTool()
{
    if (m_boardEditor->isActive())
        m_boardEditor->chooseSelectTool();
    else
        m_sheetEditor->chooseSelectTool();
}

void MainWindow::makeRectangleBoard()
{
    if (!m_boardEditor->isActive())
        return;
    // The size of the outline there is, else a common one.
    const QRectF bounds = m_boardEditor->outlineBounds();
    const Coord width = bounds.isEmpty() ? DEFAULT_BOARD_WIDTH : mmToNm(bounds.width());
    const Coord height = bounds.isEmpty() ? DEFAULT_BOARD_HEIGHT : mmToNm(bounds.height());
    RectangleBoardDialog dialog(width, height, m_canvas->gridStep(), this);
    if (dialog.exec() == QDialog::Accepted)
        m_boardEditor->makeRectangleBoard(dialog.boardWidth(), dialog.boardHeight());
}

void MainWindow::routeBoard(bool chosen)
{
    if (!m_boardEditor->isActive())
        return;
    QSet<QString> nets;
    if (chosen) {
        nets = m_boardEditor->chosenNets();
        if (nets.isEmpty()) {
            showStatus(tr("Choose parts or tracks to route"));
            return;
        }
    }
    const QString title = BoardEditor::actionName(BoardEditor::Action::Autoroute);
    QElapsedTimer clock;
    clock.start();
    QProgressDialog box(title, tr("Stop"), 0, 0, this);
    box.setWindowModality(Qt::WindowModal);
    box.setMinimumDuration(ROUTE_BOX_DELAY_MS);
    const AutorouteResult result =
        autorouted(m_document->design(), nets, [&box](int done, int total) {
            box.setMaximum(total);
            box.setValue(done);
            QCoreApplication::processEvents();
            return !box.wasCanceled();
        });
    box.reset();
    m_boardEditor->applyRouted(result.board);
    const QString routed = tr("Routed") + QStringLiteral(": %1").arg(result.routed);
    showStatus(result.stopped ? tr("Routing stopped") + QStringLiteral(". ") + routed : routed);
    // What was done, in a box.
    const auto row = [](const QString &name, const QString &value) {
        return QStringLiteral("<tr><td>%1:</td><td align=\"right\">&nbsp;%2</td></tr>")
            .arg(name.toHtmlEscaped(), value);
    };
    QString report;
    if (result.stopped)
        report += QStringLiteral("<p>%1.</p>").arg(tr("Routing stopped").toHtmlEscaped());
    report += QStringLiteral("<table>") + row(tr("Connections"), QString::number(result.total))
              + row(tr("Routed"), QString::number(result.routed))
              + row(tr("Unrouted"), QString::number(result.unrouted))
              + row(BoardEditor::viasCountName(), QString::number(result.vias))
              + row(tr("Wire links"), QString::number(result.links))
              + row(tr("Time"), QLocale().toString(clock.elapsed() / 1000.0, 'f', 1)
                                    + QLatin1Char(' ') + tr("s"))
              + QStringLiteral("</table>");
    QMessageBox::information(this, title, report);
}

void MainWindow::fitView()
{
    if (m_libraryItem != nullptr && m_libraryItem->isVisible())
        m_canvas->fitRect(m_libraryItem->boundingRect());
    else
        m_canvas->fitRect(m_designItem->pageBounds());
}

void MainWindow::fitDrawing()
{
    if (m_libraryItem != nullptr && m_libraryItem->isVisible())
        m_canvas->fitRect(m_libraryItem->boundingRect());
    else
        m_canvas->fitRect(m_designItem->drawingBounds());
}

void MainWindow::showActualSize()
{
    const QScreen *screen = m_canvas->screen();
    if (screen == nullptr)
        return;
    m_canvas->setView(m_canvas->viewCenter(), screenPixelsPerMm(screen));
}

QString MainWindow::aboutTitle()
{
    return tr("About %1").arg(QCoreApplication::applicationName());
}

void MainWindow::showUserGuide()
{
    const QString path = AppSettings::helpFile();
    if (!QFileInfo(path).isFile() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
        showStatus(tr("Cannot open the user guide") + QLatin1Char(' ')
                   + QDir::toNativeSeparators(path));
}

void MainWindow::showAbout()
{
    QString text = QStringLiteral("<h3>%1 %2</h3>").arg(QCoreApplication::applicationName(),
                                                         QLatin1String(appVersion()));
    const QString buildDate = QLatin1String(appBuildDate());
    if (!buildDate.isEmpty())
        text += QStringLiteral("<p>") + tr("Built on %1").arg(buildDate) + QStringLiteral("</p>");
    text += QStringLiteral("<p>")
            + tr("A small, simple editor for schematics and double-sided printed circuit boards")
            + QStringLiteral(".</p><p>") + tr("License") + QStringLiteral(": GPL-3.0-or-later.<br>")
            + tr("The Newstroke font is taken from KiCad") + QStringLiteral(".</p>");
    QMessageBox::about(this, aboutTitle(), text);
}

void MainWindow::chooseScreenSize()
{
    const QScreen *screen = m_canvas->screen();
    if (screen == nullptr)
        return;
    ScreenSizeDialog dialog(screen, this);
    if (dialog.exec() == QDialog::Accepted)
        AppSettings::setScreenDotsPerMm(dialog.dotsPerMm());
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

void MainWindow::recordStep(const QString &text, int page, const Design &before)
{
    // Into the project's folder, once it has one.
    if (!AppSettings::keepStepHistory() || m_document->projectPath().isEmpty())
        return;
    const QString folder = DesignFiles::historyPath(m_document->projectPath());
    if (!writeStep(folder, text, page, m_document->projectPath(), AppSettings::wireRouting(),
                   before.schematic, m_document->design().schematic, before.board,
                   m_document->design().board))
        showStatus(tr("Cannot write the history of steps into") + QLatin1Char(' ')
                   + QDir::toNativeSeparators(folder));
}

void MainWindow::showStep(int page, const QSet<ObjectId> &ids)
{
    if (page < 0)
        return;
    if (page != m_designItem->page() || !m_designItem->isVisible())
        showPage(page, false);
    if (m_boardEditor->isActive())
        m_boardEditor->setChosen(ids);
    else
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
    return formatMmShortUnit(step);
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
    keepShownPage();
    m_designItem->contentChanged();

    m_sheetTabs->updatePages();
    m_sheetTabs->setCurrentPage(m_designItem->page());
    // The sheet edited may be gone, and so may what was chosen on it.
    const int shown = m_designItem->page();
    const int sheet = shown == m_designItem->boardPage() ? -1 : shown;
    if (m_sheetEditor->sheet() != sheet)
        m_sheetEditor->setSheet(sheet);
    else
        m_sheetEditor->setSelection(m_sheetEditor->selection());
    const bool board = shown == m_designItem->boardPage();
    if (m_boardEditor->isActive() != board)
        m_boardEditor->setActive(board);
    applyProjectGrid();
    updateLibraryPanel();
    updateUnrouted();
}

void MainWindow::updateUnrouted()
{
    if (m_unroutedLabel == nullptr)
        return;
    const bool board = m_boardEditor->isActive();
    m_unroutedLabel->setVisible(board);
    if (!board)
        return;
    const Design &design = m_document->design();
    const RoutingProgress progress = routingProgress(design.board, design.library);
    m_unroutedLabel->setText(tr("Unrouted") + QStringLiteral(": %1/%2")
                                                  .arg(progress.unrouted)
                                                  .arg(progress.total));
}

void MainWindow::keepShownPage()
{
    // Nothing shown yet, or a project just started.
    if (m_shownViewKey.isEmpty())
        return;
    for (int page = 0; page < m_designItem->pageCount(); ++page) {
        if (pageViewKey(page) == m_shownViewKey) {
            m_designItem->setPage(page);
            return;
        }
    }
    // The sheet shown is gone: the one now in its place, or the last one,
    // never the board.
    const int sheets = int(m_document->design().schematic.sheets.size());
    m_designItem->setPage(qMin(m_designItem->page(), sheets - 1));
    m_shownViewKey = pageViewKey(m_designItem->page());
    if (m_designItem->isVisible())
        showPageView();
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
    // The Save button stands on red while something is not saved.
    if (m_saveButton != nullptr) {
        m_saveButton->setStyleSheet(
            m_document->isModified()
                ? QStringLiteral("QToolButton { background: %1; }").arg(QLatin1String(NOT_SAVED_COLOR))
                : QString());
    }
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
    updatePowerIcon();
    if (!problems.isEmpty())
        showStatus(problems.first());
}

bool MainWindow::placePart(LibraryPanel::Source source, const QString &part,
                           const QString &footprint)
{
    if (!canPlace())
        return false;
    m_sheetEditor->startPlacing(sourceLibrary(source), part, footprint);
    m_canvas->setFocus();
    return m_sheetEditor->isPlacing();
}

bool MainWindow::placePower(LibraryPanel::Source source, const QString &symbol)
{
    if (!canPlace())
        return false;
    m_sheetEditor->startPlacingPower(sourceLibrary(source), symbol);
    m_canvas->setFocus();
    if (!m_sheetEditor->isPlacing())
        return false;
    m_lastPowerSource = source;
    m_lastPowerSymbol = symbol;
    updatePowerIcon();
    return true;
}

bool MainWindow::placeLabel()
{
    if (!canPlace())
        return false;
    m_sheetEditor->startPlacingLabel();
    m_canvas->setFocus();
    return m_sheetEditor->isPlacing();
}

bool MainWindow::placeNoConnect()
{
    if (!canPlace())
        return false;
    m_sheetEditor->startPlacingNoConnect();
    m_canvas->setFocus();
    return m_sheetEditor->isPlacing();
}

void MainWindow::updatePowerIcon()
{
    QAction *action = findChild<QAction *>(QStringLiteral("actionPlacePower"));
    if (action == nullptr)
        return;
    const QColor color = palette().color(QPalette::WindowText);
    const Symbol *symbol = sourceLibrary(m_lastPowerSource).findSymbol(m_lastPowerSymbol);
    action->setIcon(symbol != nullptr ? symbolIcon(*symbol, TOOL_ICON_SIZE, color)
                                      : toolIcon(ToolIcon::Power, color));
}

bool MainWindow::placeLastPower()
{
    return placePower(m_lastPowerSource, m_lastPowerSymbol);
}

void MainWindow::fillPadNetMenu(QMenu *menu)
{
    const Design &design = m_document->design();
    // The nets of the board: of the pads of its parts and of its holes.
    QSet<QString> boardNets;
    for (const Placement &placement : design.board.placements) {
        for (const QString &name : placement.padNets)
            boardNets.insert(name);
    }
    for (const MountingHole &hole : design.board.holes)
        boardNets.insert(hole.net);
    boardNets.remove(QString());
    // Those of the power class first, then the others, each by name.
    QSet<QString> power;
    for (const Net &net : m_sheetEditor->nets()) {
        if (netClassOf(net, design.project, design.schematic) == design.project.powerClass)
            power.insert(net.name);
    }
    QStringList names(boardNets.cbegin(), boardNets.cend());
    std::sort(names.begin(), names.end(), [&power](const QString &a, const QString &b) {
        if (power.contains(a) != power.contains(b))
            return power.contains(a);
        return QString::localeAwareCompare(a, b) < 0;
    });
    names.prepend(QString());

    const std::optional<QString> current = m_boardEditor->chosenHolesNet();
    for (const QString &name : std::as_const(names)) {
        QAction *choice = menu->addAction(name.isEmpty() ? tr("No net") : name);
        choice->setCheckable(true);
        choice->setChecked(current == name);
        connect(choice, &QAction::triggered, m_boardEditor,
                [this, name] { m_boardEditor->setChosenHolesNet(name); });
    }
}

void MainWindow::fillHoleMenu(QMenu *menu, bool chosenHoles)
{
    menu->clear();
    const Coord current =
        chosenHoles ? m_boardEditor->chosenHolesDrill() : m_boardEditor->holeDrill();
    // The drills in a grid of buttons, row by row, the one in use pressed.
    QWidget *grid = new QWidget(menu);
    auto *layout = new QGridLayout(grid);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    const QList<Coord> &drills = m_document->design().project.rules.drills;
    for (int i = 0; i < drills.size(); ++i) {
        const Coord drill = drills.at(i);
        auto *button = new QToolButton(grid);
        button->setText(formatMmShortUnit(drill));
        button->setCheckable(true);
        button->setChecked(drill == current);
        button->setAutoRaise(true);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        layout->addWidget(button, i / DRILL_MENU_COLUMNS, i % DRILL_MENU_COLUMNS);
        connect(button, &QToolButton::clicked, this, [this, drill, chosenHoles] {
            // A button in a menu does not close it as an item does: the
            // menu and those it opened from close.
            while (QWidget *popup = QApplication::activePopupWidget())
                popup->close();
            if (chosenHoles)
                m_boardEditor->setChosenHolesDrill(drill);
            else
                m_boardEditor->setHoleDrill(drill);
            updateTools();
        });
    }
    auto *gridAction = new QWidgetAction(menu);
    gridAction->setDefaultWidget(grid);
    menu->addAction(gridAction);
    menu->addSeparator();
    QAction *other = menu->addAction(tr("Other") + QStringLiteral("…"));
    if (chosenHoles) {
        // Typed in place, over the hole.
        connect(other, &QAction::triggered, m_boardEditor, &BoardEditor::editMenuHoleDrill);
        return;
    }
    connect(other, &QAction::triggered, this, [this] {
        bool ok = false;
        const QString text = QInputDialog::getText(
            this, BoardEditor::actionName(BoardEditor::Action::HoleDiameter),
            BoardEditor::actionName(BoardEditor::Action::HoleDiameter) + QStringLiteral(", ")
                + mmUnit() + QLatin1Char(':'),
            QLineEdit::Normal, formatMmShort(m_boardEditor->holeDrill()), &ok);
        if (!ok)
            return;
        Coord drill = 0;
        if (!parseMm(text, drill) || drill <= 0) {
            showStatus(BoardEditor::diameterHint());
            return;
        }
        m_boardEditor->setHoleDrill(drill);
        updateTools();
    });
}

void MainWindow::fillPowerMenu(QMenu *menu)
{
    menu->clear();
    // The library's power symbols, then those only the project has, each
    // by name.
    struct Choice
    {
        LibraryPanel::Source source;
        QString symbol;
        QString name;
    };
    QList<Choice> choices;
    const QStringList languages = nameLanguages();
    const auto collect = [&choices, &languages](LibraryPanel::Source source, const Library &library,
                                                const Library *skipped) {
        QList<Choice> found;
        for (auto it = library.symbols.constBegin(); it != library.symbols.constEnd(); ++it) {
            if (it.value().isPower()
                && (skipped == nullptr || !skipped->symbols.contains(it.key())))
                found.append({source, it.key(), it.value().displayName(languages, it.key())});
        }
        std::sort(found.begin(), found.end(), [](const Choice &a, const Choice &b) {
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });
        choices += found;
    };
    collect(LibraryPanel::Source::Library, m_library.items, nullptr);
    collect(LibraryPanel::Source::Project, m_document->design().library, &m_library.items);

    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize);
    const QColor color = palette().color(QPalette::WindowText);
    for (const Choice &choice : std::as_const(choices)) {
        const Symbol &symbol = *sourceLibrary(choice.source).findSymbol(choice.symbol);
        QAction *action = menu->addAction(symbolIcon(symbol, iconSize, color),
                                          choice.name + QStringLiteral(" (") + symbol.power
                                              + QLatin1Char(')'));
        // The one the button places now is ticked.
        action->setCheckable(true);
        action->setChecked(choice.source == m_lastPowerSource
                           && choice.symbol == m_lastPowerSymbol);
        const LibraryPanel::Source source = choice.source;
        const QString name = choice.symbol;
        connect(action, &QAction::triggered, this,
                [this, source, name] { placePower(source, name); });
    }
    if (choices.isEmpty())
        menu->addAction(tr("The library has no power symbols"))->setEnabled(false);
}

bool MainWindow::canPlace()
{
    if (m_designItem->isVisible() && m_sheetEditor->sheet() >= 0)
        return true;
    showStatus(tr("Open a sheet to place parts on"));
    return false;
}

const Library &MainWindow::sourceLibrary(LibraryPanel::Source source) const
{
    return source == LibraryPanel::Source::Project ? m_document->design().library
                                                   : m_library.items;
}

void MainWindow::updateLibraryPanel()
{
    const Design &design = m_document->design();
    m_libraryPanel->setLibraries(m_library, design.library);
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
    updateCanvasBackground();

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
        message = tr("Part") + QLatin1String(": ") + name + QStringLiteral(" — ") + symbolWord()
                  + QLatin1Char(' ') + content.part.symbol + QStringLiteral(" — ")
                  + footprintsWord() + QLatin1Char(' ')
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
    m_positionLabel->setText(formatPosition(x, y));
}

void MainWindow::showGridStep(Coord step)
{
    // -1 (no selection) for a step that is not in the list.
    m_gridStepBox->setCurrentIndex(m_gridStepBox->findData(QVariant::fromValue<qlonglong>(step)));
}

} // namespace ppcb
