#include "mainwindow.h"

#include "canvasview.h"
#include "format.h"
#include "grid.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>

namespace ppcb {

namespace {

const int DEFAULT_WIDTH = 1100;
const int DEFAULT_HEIGHT = 750;

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("PocketPCB"));

    m_canvas = new CanvasView(this);
    m_canvas->setGridStyle(AppSettings::gridStyle());
    m_canvas->setCursorStyle(AppSettings::cursorStyle());
    m_canvas->setZoomCenterAndWarp(AppSettings::zoomCenterAndWarp());
    setCentralWidget(m_canvas);

    createMenus();
    createStatusBar();

    resize(DEFAULT_WIDTH, DEFAULT_HEIGHT);
    // Both do nothing when given data that was never saved.
    restoreGeometry(AppSettings::windowGeometry());
    restoreState(AppSettings::windowState());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    AppSettings::setWindowGeometry(saveGeometry());
    AppSettings::setWindowState(saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *quitAction = fileMenu->addAction(tr("&Quit"));
    quitAction->setObjectName(QStringLiteral("actionQuit"));
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

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

    QAction *warpAction = viewMenu->addAction(tr("Center and warp cursor on zoom"));
    warpAction->setCheckable(true);
    warpAction->setChecked(m_canvas->zoomCenterAndWarp());
    connect(warpAction, &QAction::toggled, this, &MainWindow::applyZoomCenterAndWarp);
}

void MainWindow::createStatusBar()
{
    m_positionLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_positionLabel);

    m_gridStepBox = new QComboBox(this);
    for (Coord step : gridSteps()) {
        m_gridStepBox->addItem(formatMmShort(step) + QLatin1Char(' ') + tr("mm"),
                               QVariant::fromValue<qlonglong>(step));
    }
    statusBar()->addPermanentWidget(m_gridStepBox);

    // 'activated' fires only on the user's choice, so showGridStep() below
    // cannot loop back into setGridStep().
    connect(m_gridStepBox, &QComboBox::activated, this, [this](int index) {
        m_canvas->setGridStep(m_gridStepBox->itemData(index).toLongLong());
    });
    connect(m_canvas, &CanvasView::gridStepChanged, this, &MainWindow::showGridStep);
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
