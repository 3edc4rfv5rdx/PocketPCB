#include "mainwindow.h"

#include "bompanel.h"
#include "canvasview.h"
#include "designviewitem.h"
#include "ercpanel.h"
#include "document.h"
#include "format.h"
#include "grid.h"
#include "language.h"
#include "librarypanel.h"
#include "netlist.h"
#include "paint.h"
#include "netspanel.h"
#include "projectpropertiesdialog.h"
#include "propertiespanel.h"
#include "sheeteditor.h"
#include "sheettabs.h"
#include "stephistory.h"
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
#include <QGridLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QShowEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QUrl>
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

    // The pages of the project as tabs over the canvas.
    m_sheetTabs = new SheetTabs(m_document, m_sheetEditor, this);
    QWidget *central = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_bomPanel = new BomPanel(m_document, this);
    m_pages = new QStackedWidget(central);
    m_pages->addWidget(m_canvas);
    m_pages->addWidget(m_bomPanel);
    layout->addWidget(m_sheetTabs);
    layout->addWidget(m_pages);
    setCentralWidget(central);
    connect(m_sheetTabs, &SheetTabs::pageChosen, this, [this](int page) { showPage(page); });
    connect(m_sheetTabs, &SheetTabs::bomChosen, this, &MainWindow::showBom);
    m_sheetTabs->setBomTabShown(AppSettings::bomTabShown());
    connect(m_bomPanel, &BomPanel::componentsChosen, this, &MainWindow::showComponents);

    m_designItem->setEditor(m_sheetEditor);
    connect(m_sheetEditor, &SheetEditor::changed, this, [this] { m_designItem->update(); });
    connect(m_document, &Document::stepApplied, this, &MainWindow::showStep);
    connect(m_document, &Document::stepMade, this, &MainWindow::recordStep);
    // The part being placed follows the cursor.
    connect(m_canvas, &CanvasView::cursorMoved, m_sheetEditor,
            [this](Coord x, Coord y) { m_sheetEditor->hover(Point{x, y}); });
    // A short right click ends placing and drawing wires, as Esc does.
    // A short right click ends placing and drawing wires, as Esc does; else
    // it opens the menu of the sheet.
    connect(m_canvas, &CanvasView::contextMenuRequested, this, [this](const QPoint &globalPos) {
        if (m_sheetEditor->isPlacing() || m_sheetEditor->isWiring())
            m_sheetEditor->cancel();
        else
            showSheetMenu(globalPos);
    });
    connect(m_document, &Document::designChanged, this, &MainWindow::projectChanged);
    connect(m_document, &Document::modifiedChanged, this, &MainWindow::updateTitle);

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
    createStatusBar();
    loadLibrary();
    projectChanged();
    updateTitle();

    resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    // Both do nothing when given data that was never saved.
    restoreGeometry(AppSettings::windowGeometry());
    restoreState(AppSettings::windowState());
    m_designItem->setErcMarksShown(!m_ercDock->isHidden());
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

    QAction *propertiesAction =
        fileMenu->addAction(ProjectPropertiesDialog::title() + QStringLiteral("…"));
    propertiesAction->setObjectName(QStringLiteral("actionProjectProperties"));
    connect(propertiesAction, &QAction::triggered, this, &MainWindow::editProjectProperties);
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
    addEditAction(tr("Turn the wire corner the other way"), "actionWireCorner",
                  QKeySequence(Qt::Key_Space), &SheetEditor::swapWireCorner);
    QAction *slantAction = addEditAction(tr("Slant the wire corner"), "actionWireSlant",
                                         QKeySequence(Qt::Key_Slash),
                                         &SheetEditor::toggleWireSlant);
    // The key of '/' gives '.' on the Russian layout, which Qt does not take
    // for '/' as it takes Latin letters for the shortcuts on other layouts.
    slantAction->setShortcuts({QKeySequence(Qt::Key_Slash), QKeySequence(Qt::Key_Period)});
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

    QAction *bomAction = viewMenu->addAction(BomPanel::bomTitle());
    bomAction->setObjectName(QStringLiteral("actionBomTab"));
    bomAction->setCheckable(true);
    bomAction->setChecked(AppSettings::bomTabShown());
    connect(bomAction, &QAction::toggled, this, &MainWindow::setBomTabShown);
    connect(m_sheetTabs, &SheetTabs::bomClosed, bomAction, [bomAction] {
        bomAction->setChecked(false);
    });

    viewMenu->addSeparator();

    viewMenu->addAction(m_libraryDock->toggleViewAction());
    viewMenu->addAction(m_propertiesDock->toggleViewAction());
    viewMenu->addAction(m_netsDock->toggleViewAction());
    viewMenu->addAction(m_ercDock->toggleViewAction());
    viewMenu->addSeparator();

    QAction *warpAction = viewMenu->addAction(tr("Center and warp cursor on zoom"));
    warpAction->setCheckable(true);
    warpAction->setChecked(m_canvas->zoomCenterAndWarp());
    connect(warpAction, &QAction::toggled, this, &MainWindow::applyZoomCenterAndWarp);

    // The language of the interface, each named in itself; it is taken at
    // the next start.
    QMenu *settingsMenu = menuBar()->addMenu(tr("&Settings"));
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
        menu.addSeparator();
        add("actionCopy");
        add("actionCut");
        add("actionDuplicate");
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
    m_canvas->setBackgroundColor(isSheetShown() ? AppSettings::sheetBackground()
                                                : paintcolor::background());
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
    const QString parentFolder = AppSettings::projectDialogFolder();

    SaveProjectDialog dialog(name, parentFolder, this);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    AppSettings::setLastProjectFolder(dialog.parentFolder());
    return saveProjectAs(dialog.parentFolder(), dialog.name());
}

void MainWindow::editProjectProperties()
{
    ProjectPropertiesDialog dialog(m_document->design(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    m_document->change(ProjectPropertiesDialog::title(), dialog.design());
}

void MainWindow::showBom()
{
    m_bomShown = true;
    m_pages->setCurrentWidget(m_bomPanel);
    m_sheetEditor->setSheet(-1);
    m_sheetTabs->setBomCurrent();
}

void MainWindow::showCanvas()
{
    m_bomShown = false;
    m_pages->setCurrentWidget(m_canvas);
}

void MainWindow::setBomTabShown(bool shown)
{
    AppSettings::setBomTabShown(shown);
    m_sheetTabs->setBomTabShown(shown);
    if (shown)
        showBom();
    else if (m_bomShown)
        showPage(m_designItem->page());
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
    showCanvas();
    showProject();
    m_designItem->setPage(page);
    m_shownViewKey = pageViewKey(m_designItem->page());
    updateCanvasBackground();
    // The tab follows the page, not the other way round.
    m_sheetTabs->setCurrentPage(m_designItem->page());
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

void MainWindow::recordStep(const QString &text, int page, const Design &before)
{
    if (!AppSettings::keepStepHistory())
        return;
    const QString folder = AppSettings::stepHistoryFolder();
    if (!writeStep(folder, text, page, m_document->projectPath(), AppSettings::wireRouting(),
                   before.schematic, m_document->design().schematic))
        showStatus(tr("Cannot write the history of steps into") + QLatin1Char(' ')
                   + QDir::toNativeSeparators(folder));
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
    return formatMmShort(step) + QLatin1Char(' ') + mmUnit();
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
    if (m_bomShown) {
        // The BOM stays in front, the sheet editor resting.
        m_sheetTabs->setBomCurrent();
        applyProjectGrid();
        updateLibraryPanel();
        return;
    }
    m_sheetTabs->setCurrentPage(m_designItem->page());
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

bool MainWindow::placeLastPower()
{
    return placePower(m_lastPowerSource, m_lastPowerSymbol);
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
    if (m_bomShown) {
        showCanvas();
        m_sheetEditor->setSheet(m_designItem->page() == m_designItem->boardPage()
                                    ? -1
                                    : m_designItem->page());
    }
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
                             + mmUnit());
}

void MainWindow::showGridStep(Coord step)
{
    // -1 (no selection) for a step that is not in the list.
    m_gridStepBox->setCurrentIndex(m_gridStepBox->findData(QVariant::fromValue<qlonglong>(step)));
}

} // namespace ppcb
