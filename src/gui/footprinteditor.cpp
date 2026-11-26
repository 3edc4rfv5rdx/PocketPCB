#include "footprinteditor.h"

#include "appsettings.h"
#include "canvasview.h"
#include "format.h"
#include "inplaceedit.h"
#include "libraryfile.h"
#include "library.h"
#include "mainwindow.h"
#include "paint.h"
#include "propertiespanel.h"
#include "sheeteditor.h"
#include "toolicons.h"

#include <QAction>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLineF>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

// The size of the window at first, in pixels.
const int EDITOR_WIDTH = 900;
const int EDITOR_HEIGHT = 650;
// The grid a footprint is drawn on at first.
const Coord FOOTPRINT_GRID_STEP = 1270000;
// The pad placed first: round, 1.8 mm across, a hole of 1 mm, through.
const Coord FIRST_PAD_SIZE = 1800000;
const Coord FIRST_PAD_HOLE = 1000000;
// The pads offered by the arrow of the Pad button: size and hole.
const QList<QPair<Coord, Coord>> &padChoices()
{
    static const QList<QPair<Coord, Coord>> choices = {
        {1600000, 800000}, {1800000, 1000000}, {2000000, 1200000},
        {2500000, 1500000}, {3000000, 1800000},
    };
    return choices;
}
// How far the mouse goes with the button down before a press is a drag, in
// screen pixels.
const double DRAG_PIXELS = 3.0;
// The most pads a row may have.
const int MAX_ROW_PADS = 200;
// The pads following the cursor, half seen.
const double FLOATING_OPACITY = 0.5;
// The frame round a chosen pad, in screen pixels.
const double CHOSEN_PEN_PIXELS = 2.0;
// The cross of the origin about to be placed, its half length in mm.
const double ORIGIN_CROSS_MM = 2.0;
// What a footprint's name may hold: it is the name of its file.
const QRegularExpression &nameRule()
{
    static const QRegularExpression rule(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]*$"));
    return rule;
}

QString footprintPath(const QString &libraryFolder, const QString &name)
{
    return QDir(QDir(libraryFolder).filePath(QLatin1String(LIBRARY_FOOTPRINTS_FOLDER)))
        .filePath(name + QStringLiteral(".json"));
}

Pad firstPad()
{
    Pad pad;
    pad.sizeX = FIRST_PAD_SIZE;
    pad.sizeY = FIRST_PAD_SIZE;
    pad.hole = FIRST_PAD_HOLE;
    return pad;
}

QList<Pad> padsMovedBy(QList<Pad> pads, const Point &delta)
{
    for (Pad &pad : pads)
        pad.at = pad.at + delta;
    return pads;
}

// The footprint on the canvas: it hands the mouse to the editor and draws
// what the editor shows.
class FootprintItem : public QGraphicsItem
{
public:
    FootprintItem(FootprintEditor *editor, const QRectF &area)
        : m_editor(editor)
        , m_area(area)
    {
        setAcceptHoverEvents(true);
    }

    QRectF boundingRect() const override { return m_area; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override
    {
        Q_UNUSED(option);
        Q_UNUSED(widget);
        const Footprint shown = m_editor->shownFootprint();
        paintFootprint(*painter, shown);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen chosenPen(paintcolor::selection(), CHOSEN_PEN_PIXELS);
        chosenPen.setCosmetic(true);
        painter->setPen(chosenPen);
        painter->setBrush(Qt::NoBrush);
        for (const int i : m_editor->chosen()) {
            if (i >= 0 && i < shown.pads.size())
                painter->drawRect(padRect(shown.pads.at(i)));
        }
        const QList<Pad> floating = m_editor->floatingPads();
        if (!floating.isEmpty()) {
            painter->setOpacity(FLOATING_OPACITY);
            paintFootprintPads(*painter, floating);
            painter->setOpacity(1.0);
        }
        if (const std::optional<Point> origin = m_editor->floatingOrigin()) {
            QPen crossPen(paintcolor::boardOrigin(), CHOSEN_PEN_PIXELS);
            crossPen.setCosmetic(true);
            painter->setPen(crossPen);
            const QPointF at = toMm(*origin);
            painter->drawLine(at - QPointF(ORIGIN_CROSS_MM, 0), at + QPointF(ORIGIN_CROSS_MM, 0));
            painter->drawLine(at - QPointF(0, ORIGIN_CROSS_MM), at + QPointF(0, ORIGIN_CROSS_MM));
        }
        const QRectF frame = m_editor->frame();
        if (!frame.isNull()) {
            QPen framePen(paintcolor::selection(), 1, Qt::DashLine);
            framePen.setCosmetic(true);
            painter->setPen(framePen);
            painter->drawRect(frame);
        }
        painter->restore();
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }
        m_editor->press(event->scenePos(), event->modifiers().testFlag(Qt::ControlModifier));
        event->accept();
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        m_editor->move(event->scenePos());
    }
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override
    {
        m_editor->release(event->scenePos());
    }
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }
        m_editor->doubleClick(event->scenePos());
        event->accept();
    }
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override
    {
        m_editor->hover(event->scenePos());
    }

private:
    FootprintEditor *m_editor;
    QRectF m_area;
};

} // namespace

// One step of a footprint editor: the footprint before and after it.
class FootprintStep : public QUndoCommand
{
public:
    FootprintStep(FootprintEditor *editor, const QString &text, const Footprint &before,
                  const Footprint &after)
        : QUndoCommand(text)
        , m_editor(editor)
        , m_before(before)
        , m_after(after)
    {
    }

    void undo() override { m_editor->applyStep(m_before); }
    void redo() override { m_editor->applyStep(m_after); }

private:
    FootprintEditor *m_editor;
    Footprint m_before;
    Footprint m_after;
};

FootprintEditor::FootprintEditor(const QString &name, const Footprint &footprint,
                                 bool inUserLibrary, QWidget *parent)
    : QMainWindow(parent)
    , m_name(name)
    , m_inUserLibrary(inUserLibrary)
    , m_footprint(footprint)
    , m_nextPad(firstPad())
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Window);
    // The pads go on as the footprint's last one, if it has one.
    if (!m_footprint.pads.isEmpty())
        setNextPad(m_footprint.pads.last());

    m_canvas = new CanvasView(this);
    m_canvas->setObjectName(QStringLiteral("footprintCanvas"));
    m_canvas->setGridStyle(AppSettings::gridStyle());
    m_canvas->setCursorStyle(AppSettings::cursorStyle());
    m_canvas->setZoomCenterAndWarp(AppSettings::zoomCenterAndWarp());
    m_canvas->setGridStep(FOOTPRINT_GRID_STEP);
    m_canvas->scene()->addItem(new FootprintItem(this, m_canvas->scene()->sceneRect()));
    setCentralWidget(m_canvas);
    m_numberEdit = new InPlaceEdit(m_canvas, this);

    m_panel = new PadPanel(this, this);
    QDockWidget *dock = new QDockWidget(PadPanel::padTitle(), this);
    dock->setObjectName(QStringLiteral("padDock"));
    dock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    dock->setWidget(m_panel);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    m_statusText = new QLabel(this);
    statusBar()->addWidget(m_statusText, 1);
    m_positionLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_positionLabel);
    connect(m_canvas, &CanvasView::cursorMoved, this, [this](Coord x, Coord y) {
        m_positionLabel->setText(formatPosition(x, y));
    });
    m_positionLabel->setText(formatPosition(0, 0));

    createToolBar();
    connect(&m_undoStack, &QUndoStack::cleanChanged, this, &FootprintEditor::updateTitle);
    connect(this, &FootprintEditor::changed, m_canvas->scene(), [this] { m_canvas->scene()->update(); });
    connect(this, &FootprintEditor::changed, m_panel, [this] { m_panel->refresh(); });
    updateTitle();
    resize(EDITOR_WIDTH, EDITOR_HEIGHT);
    // Once the window is shown and the canvas knows its size.
    QTimer::singleShot(0, this, [this] { m_canvas->fitRect(footprintBounds(m_footprint)); });
}

QString FootprintEditor::editorTitle()
{
    return tr("Footprint editor");
}

QString FootprintEditor::padPropertiesStep()
{
    return tr("Pad properties");
}

QString FootprintEditor::newFootprintTitle()
{
    return tr("New footprint");
}

bool FootprintEditor::isInUserLibrary(const QString &name)
{
    return QFileInfo::exists(footprintPath(AppSettings::userLibraryFolder(), name));
}

void FootprintEditor::createToolBar()
{
    QToolBar *bar = new QToolBar(this);
    bar->setObjectName(QStringLiteral("footprintBar"));
    bar->setMovable(false);
    bar->toggleViewAction()->setVisible(false);
    styleToolBar(bar);
    addToolBar(Qt::TopToolBarArea, bar);
    const QColor color = bar->palette().color(QPalette::WindowText);

    // An action of the window with its picture and its key in its tip; on
    // the strip when it has a picture.
    const auto action = [this, bar, &color](const QString &text, const char *name,
                                            const QKeySequence &key, std::optional<ToolIcon> icon,
                                            auto slot) {
        QAction *result = new QAction(text, this);
        result->setObjectName(QLatin1String(name));
        QString tip = text;
        tip.remove(QLatin1Char('&'));
        if (!key.isEmpty()) {
            result->setShortcut(key);
            tip += QStringLiteral(" (") + key.toString(QKeySequence::NativeText)
                   + QLatin1Char(')');
        }
        result->setToolTip(tip);
        connect(result, &QAction::triggered, this, slot);
        addAction(result);
        if (icon) {
            result->setIcon(toolIcon(*icon, color));
            bar->addAction(result);
        }
        return result;
    };
    const auto gap = [bar] {
        QWidget *spacer = new QWidget(bar);
        spacer->setFixedWidth(TOOL_ICON_SIZE / 2);
        bar->addWidget(spacer);
    };
    const auto stretch = [bar] {
        QWidget *spacer = new QWidget(bar);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        bar->addWidget(spacer);
    };

    action(MainWindow::saveTitle(), "actionSaveFootprint", QKeySequence::Save, ToolIcon::Save,
           [this] { save(); });
    gap();
    QAction *undo = m_undoStack.createUndoAction(this, MainWindow::undoTitle());
    undo->setObjectName(QStringLiteral("actionUndoFootprint"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setIcon(toolIcon(ToolIcon::Undo, color));
    addAction(undo);
    bar->addAction(undo);
    QAction *redo = m_undoStack.createRedoAction(this, MainWindow::redoTitle());
    redo->setObjectName(QStringLiteral("actionRedoFootprint"));
    redo->setShortcut(QKeySequence::Redo);
    redo->setIcon(toolIcon(ToolIcon::Redo, color));
    addAction(redo);
    bar->addAction(redo);
    gap();
    action(MainWindow::zoomInTitle(), "actionZoomInFootprint", QKeySequence::ZoomIn,
           ToolIcon::ZoomIn, [this] {
               m_canvas->setView(m_canvas->viewCenter(),
                                 m_canvas->pixelsPerMm() * CanvasView::ZOOM_STEP);
           });
    action(MainWindow::zoomOutTitle(), "actionZoomOutFootprint", QKeySequence::ZoomOut,
           ToolIcon::ZoomOut, [this] {
               m_canvas->setView(m_canvas->viewCenter(),
                                 m_canvas->pixelsPerMm() / CanvasView::ZOOM_STEP);
           });
    action(MainWindow::fitDrawingTitle(), "actionFitFootprint", QKeySequence(Qt::Key_Home),
           ToolIcon::ZoomFitDrawing,
           [this] { m_canvas->fitRect(footprintBounds(shownFootprint())); });
    stretch();

    // The modes in the middle, the one going on pressed.
    QAction *select = action(MainWindow::selectTitle(), "actionSelectFootprint",
                             QKeySequence(Qt::Key_Escape), ToolIcon::Select, [this] { escape(); });
    m_modeActions.insert(int(Mode::Select), select);
    QAction *pad = action(PadPanel::padTitle(), "actionPadTool", QKeySequence(Qt::Key_P), std::nullopt,
                          [this] { setMode(Mode::Pad); });
    pad->setIcon(toolIcon(ToolIcon::Pad, color));
    m_modeActions.insert(int(Mode::Pad), pad);
    // The arrow offers pads of the usual sizes: the one chosen is placed.
    QMenu *padMenu = new QMenu(this);
    for (const auto &[size, hole] : padChoices()) {
        QAction *choice = padMenu->addAction(formatMmShort(size) + QStringLiteral(" / ")
                                             + formatMmShortUnit(hole));
        connect(choice, &QAction::triggered, this, [this, size = size, hole = hole] {
            Pad next = m_nextPad;
            next.sizeX = size;
            next.sizeY = size;
            next.hole = hole;
            next.layers = PadLayers::Both;
            setNextPad(next);
            setMode(Mode::Pad);
        });
    }
    QToolButton *padButton = new QToolButton(bar);
    padButton->setObjectName(QStringLiteral("padButton"));
    padButton->setDefaultAction(pad);
    padButton->setMenu(padMenu);
    padButton->setPopupMode(QToolButton::MenuButtonPopup);
    bar->addWidget(padButton);
    QAction *row = action(tr("Row of pads"), "actionPadRow", QKeySequence(),
                          ToolIcon::PadRow, [this] { askRow(); });
    m_modeActions.insert(int(Mode::Row), row);
    QAction *origin = action(tr("Origin here"), "actionOriginHere", QKeySequence(Qt::Key_O),
                             std::nullopt, [this] { setMode(Mode::Origin); });
    origin->setIcon(toolIcon(ToolIcon::Origin, color));
    m_modeActions.insert(int(Mode::Origin), origin);
    QMenu *originMenu = new QMenu(this);
    QAction *middle = originMenu->addAction(tr("Origin in the middle"));
    middle->setObjectName(QStringLiteral("actionOriginMiddle"));
    connect(middle, &QAction::triggered, this, &FootprintEditor::originAtMiddle);
    QToolButton *originButton = new QToolButton(bar);
    originButton->setObjectName(QStringLiteral("originButton"));
    originButton->setDefaultAction(origin);
    originButton->setMenu(originMenu);
    originButton->setPopupMode(QToolButton::MenuButtonPopup);
    bar->addWidget(originButton);
    for (QAction *mode : std::as_const(m_modeActions))
        mode->setCheckable(true);
    stretch();

    m_gridBox = new QComboBox(bar);
    m_gridBox->setObjectName(QStringLiteral("footprintGridBox"));
    for (const Coord step : gridSteps())
        m_gridBox->addItem(MainWindow::gridStepText(step), QVariant::fromValue<qlonglong>(step));
    m_gridBox->setCurrentIndex(m_gridBox->findData(QVariant::fromValue<qlonglong>(m_canvas->gridStep())));
    connect(m_gridBox, &QComboBox::activated, this, [this](int index) {
        m_canvas->setGridStep(m_gridBox->itemData(index).toLongLong());
    });
    connect(m_canvas, &CanvasView::gridStepChanged, this, [this](Coord step) {
        m_gridBox->setCurrentIndex(m_gridBox->findData(QVariant::fromValue<qlonglong>(step)));
    });
    bar->addWidget(m_gridBox);

    // Keys with no button.
    action(SheetEditor::actionName(SheetEditor::Action::Rotate), "actionRotatePads",
           QKeySequence(Qt::Key_R), std::nullopt, [this] { rotate(); });
    action(SheetEditor::actionName(SheetEditor::Action::Delete), "actionDeletePads",
           QKeySequence::Delete, std::nullopt, [this] { deleteChosen(); });
    action(MainWindow::selectAllTitle(), "actionChooseAllPads", QKeySequence::SelectAll,
           std::nullopt, [this] { chooseAll(); });
    updateModeButtons();
}

void FootprintEditor::updateTitle()
{
    QString title = m_name.isEmpty() ? newFootprintTitle() : m_name;
    if (!m_name.isEmpty() && !m_inUserLibrary)
        title += QStringLiteral(" (") + tr("built-in") + QLatin1Char(')');
    setWindowTitle(title + QStringLiteral("[*] — ") + editorTitle());
    setWindowModified(!m_undoStack.isClean());
}

void FootprintEditor::updateModeButtons()
{
    for (auto it = m_modeActions.constBegin(); it != m_modeActions.constEnd(); ++it)
        it.value()->setChecked(it.key() == int(m_mode));
}

void FootprintEditor::showStatus(const QString &text)
{
    m_statusText->setText(text);
}

Point FootprintEditor::snapped(const QPointF &at) const
{
    return m_canvas->snapped(at);
}

void FootprintEditor::setMode(Mode mode)
{
    m_mode = mode;
    m_dragging = false;
    m_framing = false;
    updateModeButtons();
    emit changed();
}

void FootprintEditor::setChosen(const QSet<int> &pads)
{
    QSet<int> valid;
    for (const int i : pads) {
        if (i >= 0 && i < m_footprint.pads.size())
            valid.insert(i);
    }
    if (valid == m_chosen)
        return;
    m_chosen = valid;
    emit changed();
}

void FootprintEditor::setNextPad(const Pad &pad)
{
    m_nextPad = pad;
    m_nextPad.number.clear();
    m_nextPad.at = Point{};
    emit changed();
}

void FootprintEditor::change(const QString &text, const Footprint &changed,
                             const QSet<int> &chosen)
{
    if (changed == m_footprint) {
        setChosen(chosen);
        return;
    }
    m_undoStack.push(new FootprintStep(this, text, m_footprint, changed));
    setChosen(chosen);
}

void FootprintEditor::applyStep(const Footprint &footprint)
{
    m_footprint = footprint;
    m_dragging = false;
    m_chosen.clear();
    emit changed();
}

Footprint FootprintEditor::shownFootprint() const
{
    if (m_dragging && m_dragDelta != Point{})
        return withPadsMoved(m_footprint, m_chosen, m_dragDelta);
    return m_footprint;
}

QList<Pad> FootprintEditor::floatingPads() const
{
    if (!m_cursorKnown)
        return {};
    if (m_mode == Mode::Pad) {
        Pad pad = m_nextPad;
        pad.at = m_cursor;
        pad.number = nextPadNumber(m_footprint);
        return {pad};
    }
    if (m_mode == Mode::Row)
        return padsMovedBy(m_rowPads, m_cursor);
    return {};
}

std::optional<Point> FootprintEditor::floatingOrigin() const
{
    if (m_mode == Mode::Origin && m_cursorKnown)
        return m_cursor;
    return std::nullopt;
}

QRectF FootprintEditor::frame() const
{
    return m_framing ? QRectF(m_frameFrom, m_frameTo).normalized() : QRectF();
}

void FootprintEditor::press(const QPointF &at, bool adding)
{
    m_numberEdit->close(true);
    m_pressAt = at;
    m_cursor = snapped(at);
    m_cursorKnown = true;
    switch (m_mode) {
    case Mode::Pad: {
        Pad pad = m_nextPad;
        pad.at = m_cursor;
        pad.number = nextPadNumber(m_footprint);
        const int index = int(m_footprint.pads.size());
        change(tr("Place pad"), withPadsAdded(m_footprint, {pad}), {index});
        return;
    }
    case Mode::Row: {
        const int first = int(m_footprint.pads.size());
        QSet<int> placed;
        for (int k = 0; k < m_rowPads.size(); ++k)
            placed.insert(first + k);
        const Footprint changed = withPadsAdded(m_footprint, padsMovedBy(m_rowPads, m_cursor));
        m_mode = Mode::Select;
        updateModeButtons();
        change(tr("Place row of pads"), changed, placed);
        return;
    }
    case Mode::Origin: {
        const Point origin = m_cursor;
        m_mode = Mode::Select;
        updateModeButtons();
        change(tr("Move origin"), withOriginAt(m_footprint, origin), m_chosen);
        // The drawing stays where it was on screen.
        m_canvas->setView(m_canvas->viewCenter() - toMm(origin), m_canvas->pixelsPerMm());
        m_cursor = m_cursor - origin;
        return;
    }
    case Mode::Select:
        break;
    }

    const int hit = padAtPoint(m_footprint, at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
    m_pressPad = -1;
    if (hit < 0) {
        m_framing = true;
        m_frameAdding = adding;
        m_frameFrom = at;
        m_frameTo = at;
        if (!adding)
            setChosen({});
        emit changed();
        return;
    }
    if (adding) {
        QSet<int> chosen = m_chosen;
        if (chosen.contains(hit))
            chosen.remove(hit);
        else
            chosen.insert(hit);
        setChosen(chosen);
        return;
    }
    if (m_chosen.contains(hit))
        m_pressPad = hit;
    else
        setChosen({hit});
    m_dragging = true;
    m_dragAnchor = m_footprint.pads.at(hit).at;
    m_dragDelta = Point{};
}

void FootprintEditor::move(const QPointF &at)
{
    hover(at);
    if (m_framing) {
        m_frameTo = at;
        emit changed();
        return;
    }
    if (!m_dragging)
        return;
    if (m_dragDelta == Point{}
        && QLineF(at, m_pressAt).length() < m_canvas->pixelsToMm(DRAG_PIXELS))
        return;
    const Point delta = snapped(toMm(m_dragAnchor) + (at - m_pressAt)) - m_dragAnchor;
    if (delta != m_dragDelta) {
        m_dragDelta = delta;
        emit changed();
    }
}

void FootprintEditor::release(const QPointF &at)
{
    if (m_framing) {
        m_framing = false;
        m_frameTo = at;
        QSet<int> chosen = m_frameAdding ? m_chosen : QSet<int>();
        chosen.unite(padsInside(m_footprint, QRectF(m_frameFrom, m_frameTo).normalized()));
        setChosen(chosen);
        emit changed();
        return;
    }
    if (!m_dragging)
        return;
    m_dragging = false;
    const Point delta = m_dragDelta;
    m_dragDelta = Point{};
    if (delta != Point{}) {
        change(SheetEditor::actionName(SheetEditor::Action::Move),
               withPadsMoved(m_footprint, m_chosen, delta), m_chosen);
    } else if (m_pressPad >= 0) {
        // A click on one of several chosen chooses it alone.
        setChosen({m_pressPad});
    }
    m_pressPad = -1;
    emit changed();
}

void FootprintEditor::doubleClick(const QPointF &at)
{
    // The second of two quick clicks places again.
    if (m_mode != Mode::Select) {
        press(at, false);
        return;
    }
    const int hit = padAtPoint(m_footprint, at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
    if (hit >= 0)
        editNumber(hit);
}

void FootprintEditor::editNumber(int pad)
{
    setChosen({pad});
    m_dragging = false;
    // The edit is tied to this pad as the footprint stands now: a step made
    // meanwhile closes it first (press() does), so the index holds.
    disconnect(m_numberEdit, &InPlaceEdit::accepted, this, nullptr);
    connect(m_numberEdit, &InPlaceEdit::accepted, this, [this, pad](const QString &text) {
        if (pad >= m_footprint.pads.size())
            return;
        Pad changed = m_footprint.pads.at(pad);
        changed.number = text.trimmed();
        change(padPropertiesStep(), withPadsReplaced(m_footprint, {pad}, {changed}), {pad});
    });
    m_numberEdit->open(m_footprint.pads.at(pad).number, padRect(m_footprint.pads.at(pad)));
}

void FootprintEditor::hover(const QPointF &at)
{
    const Point cursor = snapped(at);
    if (m_cursorKnown && cursor == m_cursor)
        return;
    m_cursor = cursor;
    m_cursorKnown = true;
    if (m_mode != Mode::Select)
        emit changed();
}

void FootprintEditor::rotate()
{
    if (m_mode == Mode::Pad) {
        setNextPad(padsRotated({m_nextPad}, Point{}).first());
        return;
    }
    if (m_mode == Mode::Row) {
        m_rowPads = padsRotated(m_rowPads, Point{});
        emit changed();
        return;
    }
    if (m_chosen.isEmpty() || m_dragging)
        return;
    const Point center = snapped(padsBounds(m_footprint, m_chosen).center());
    change(SheetEditor::actionName(SheetEditor::Action::Rotate),
           withPadsRotated(m_footprint, m_chosen, center), m_chosen);
}

void FootprintEditor::deleteChosen()
{
    if (m_chosen.isEmpty() || m_dragging)
        return;
    change(SheetEditor::actionName(SheetEditor::Action::Delete),
           withoutPads(m_footprint, m_chosen), {});
}

void FootprintEditor::chooseAll()
{
    QSet<int> all;
    for (int i = 0; i < m_footprint.pads.size(); ++i)
        all.insert(i);
    setMode(Mode::Select);
    setChosen(all);
}

void FootprintEditor::escape()
{
    if (m_dragging || m_framing) {
        m_dragging = false;
        m_framing = false;
        m_dragDelta = Point{};
        emit changed();
    } else if (m_mode != Mode::Select) {
        setMode(Mode::Select);
    } else {
        setChosen({});
    }
    updateModeButtons();
}

void FootprintEditor::originAtMiddle()
{
    const Point origin = snapped(toMm(footprintMiddle(m_footprint)));
    if (origin == Point{})
        return;
    setMode(Mode::Select);
    change(tr("Move origin"), withOriginAt(m_footprint, origin), m_chosen);
    m_canvas->setView(m_canvas->viewCenter() - toMm(origin), m_canvas->pixelsPerMm());
}

void FootprintEditor::askRow()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Row of pads"));
    QFormLayout *form = new QFormLayout;
    QRadioButton *single = new QRadioButton(tr("One column"), &dialog);
    QRadioButton *twoColumns = new QRadioButton(tr("Two columns, numbered across"), &dialog);
    QRadioButton *dip = new QRadioButton(tr("DIP, numbered around"), &dialog);
    QButtonGroup kinds(&dialog);
    kinds.addButton(single, int(PadRowKind::Single));
    kinds.addButton(twoColumns, int(PadRowKind::Double));
    kinds.addButton(dip, int(PadRowKind::Dip));
    kinds.button(int(m_row.kind))->setChecked(true);
    QVBoxLayout *kindLayout = new QVBoxLayout;
    kindLayout->addWidget(single);
    kindLayout->addWidget(twoColumns);
    kindLayout->addWidget(dip);
    form->addRow(tr("Layout") + QLatin1Char(':'), kindLayout);
    QSpinBox *count = new QSpinBox(&dialog);
    count->setRange(1, MAX_ROW_PADS);
    count->setValue(m_row.count);
    form->addRow(tr("Pads") + QLatin1Char(':'), count);
    QDoubleSpinBox *pitch = mmSizeBox(m_row.pitch, 0.1, 100, 0.01, &dialog);
    form->addRow(tr("Pitch") + QLatin1Char(':'), pitch);
    QDoubleSpinBox *spacing = mmSizeBox(m_row.spacing, 0.1, 200, 0.01, &dialog);
    form->addRow(tr("Between columns") + QLatin1Char(':'), spacing);
    QSpinBox *first = new QSpinBox(&dialog);
    first->setRange(1, 9999);
    first->setValue(m_row.firstNumber);
    form->addRow(tr("First number") + QLatin1Char(':'), first);
    QCheckBox *square = new QCheckBox(tr("First pad square"), &dialog);
    square->setChecked(m_row.firstSquare);
    form->addRow(QString(), square);
    QLabel *problem = new QLabel(&dialog);
    problem->setStyleSheet(QStringLiteral("color: #c00000;"));
    form->addRow(problem);
    const auto update = [&] {
        const bool two = kinds.checkedId() != int(PadRowKind::Single);
        spacing->setEnabled(two);
        const bool odd = two && count->value() % 2 != 0;
        problem->setText(odd ? tr("Two columns need an even number of pads") : QString());
    };
    connect(&kinds, &QButtonGroup::idClicked, &dialog, update);
    connect(count, &QSpinBox::valueChanged, &dialog, update);
    update();

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (problem->text().isEmpty())
            dialog.accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addLayout(form);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) {
        updateModeButtons();
        return;
    }
    m_row.kind = PadRowKind(kinds.checkedId());
    m_row.count = count->value();
    m_row.pitch = mmSizeOf(pitch);
    m_row.spacing = mmSizeOf(spacing);
    m_row.firstNumber = first->value();
    m_row.firstSquare = square->isChecked();
    m_rowPads = padRow(m_row, m_nextPad);
    if (m_rowPads.isEmpty()) {
        updateModeButtons();
        return;
    }
    setMode(Mode::Row);
}

bool FootprintEditor::save()
{
    if (!m_name.isEmpty())
        return saveAs(m_name, true);
    bool ok = false;
    const QString name = QInputDialog::getText(this, editorTitle(), tr("Footprint name") + QLatin1Char(':'),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return false;
    if (!nameRule().match(name).hasMatch()) {
        showStatus(tr("A name may hold Latin letters, digits and these signs") + QStringLiteral(": . _ + -"));
        return false;
    }
    const bool taken = QFileInfo::exists(footprintPath(AppSettings::userLibraryFolder(), name))
                       || QFileInfo::exists(footprintPath(AppSettings::builtInLibraryFolder(), name));
    if (taken
        && QMessageBox::question(this, editorTitle(),
                                 tr("The library has a footprint %1 already. Replace it").arg(name)
                                     + QLatin1Char('?'))
               != QMessageBox::Yes)
        return false;
    return saveAs(name, true);
}

bool FootprintEditor::saveAs(const QString &name, bool replace)
{
    const QString path = footprintPath(AppSettings::userLibraryFolder(), name);
    if (!replace && QFileInfo::exists(path))
        return false;
    for (const Pad &pad : m_footprint.pads) {
        const QString problem = padProblem(pad);
        if (!problem.isEmpty()) {
            showStatus(capitalized(problem));
            return false;
        }
    }
    QString error;
    if (!AppSettings::makeUserLibraryFolders(error)) {
        showStatus(error);
        return false;
    }
    LibraryItem item;
    item.kind = LibraryKind::Footprint;
    item.footprint = m_footprint;
    if (!LibraryFile::write(path, item, error)) {
        showStatus(error);
        return false;
    }
    m_name = name;
    m_inUserLibrary = true;
    m_undoStack.setClean();
    updateTitle();
    showStatus(writtenMessage(path));
    emit saved(name);
    return true;
}

void FootprintEditor::closeEvent(QCloseEvent *event)
{
    if (!m_undoStack.isClean()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, editorTitle(),
            tr("The footprint has changes that are not saved. Save them") + QLatin1Char('?'),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (answer == QMessageBox::Cancel || (answer == QMessageBox::Save && !save())) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

// PadPanel

PadPanel::PadPanel(FootprintEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_editor(editor)
{
    m_heading = new QLabel(this);
    QFont bold = m_heading->font();
    bold.setBold(true);
    m_heading->setFont(bold);
    const auto lineEdit = [this](const char *name) {
        QLineEdit *edit = new QLineEdit(this);
        edit->setObjectName(QLatin1String(name));
        edit->installEventFilter(this);
        return edit;
    };
    m_number = lineEdit("padNumber");
    m_shape = new QComboBox(this);
    m_shape->setObjectName(QStringLiteral("padShape"));
    // In the order of PadShape.
    m_shape->addItems({tr("Round"), tr("Rectangle"), tr("Oval")});
    m_width = lineEdit("padWidth");
    m_height = lineEdit("padHeight");
    m_hole = lineEdit("padHole");
    m_layers = new QComboBox(this);
    m_layers->setObjectName(QStringLiteral("padLayers"));
    // In the order of PadLayers.
    m_layers->addItems({tr("Both sides"), tr("Side of the part"), tr("Other side")});
    m_x = lineEdit("padX");
    m_y = lineEdit("padY");

    QFormLayout *form = new QFormLayout;
    const QString unit = QStringLiteral(", ") + mmUnit() + QLatin1Char(':');
    form->addRow(tr("Number") + QLatin1Char(':'), m_number);
    form->addRow(tr("Shape") + QLatin1Char(':'), m_shape);
    form->addRow(tr("Width") + unit, m_width);
    form->addRow(tr("Height") + unit, m_height);
    form->addRow(tr("Hole") + unit, m_hole);
    form->addRow(tr("Copper") + QLatin1Char(':'), m_layers);
    form->addRow(QStringLiteral("X") + unit, m_x);
    form->addRow(QStringLiteral("Y") + unit, m_y);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_heading);
    layout->addLayout(form);
    layout->addStretch();

    connect(m_number, &QLineEdit::editingFinished, this, &PadPanel::commitNumber);
    for (QLineEdit *edit : {m_width, m_height, m_hole, m_x, m_y})
        connect(edit, &QLineEdit::editingFinished, this, [this, edit] { commitMm(edit); });
    connect(m_shape, &QComboBox::activated, this, &PadPanel::commitShape);
    connect(m_layers, &QComboBox::activated, this, &PadPanel::commitLayers);
    refresh(true);
}

QString PadPanel::padTitle()
{
    return tr("Pad");
}

bool PadPanel::eventFilter(QObject *watched, QEvent *event)
{
    const bool escape = (event->type() == QEvent::ShortcutOverride
                         || event->type() == QEvent::KeyPress)
                        && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape;
    if (!escape || qobject_cast<QLineEdit *>(watched) == nullptr)
        return QWidget::eventFilter(watched, event);
    // Esc is the field's, not the window's, which would drop the choice.
    event->accept();
    if (event->type() == QEvent::KeyPress) {
        refresh(true);
        static_cast<QWidget *>(watched)->clearFocus();
    }
    return true;
}

bool PadPanel::isTyping(const QWidget *field, bool force) const
{
    return !force && field->hasFocus();
}

void PadPanel::refresh(bool force)
{
    const QSet<int> &chosen = m_editor->chosen();
    if (chosen != m_shown)
        force = true;
    m_shown = chosen;
    QList<Pad> pads;
    QList<int> indices(chosen.cbegin(), chosen.cend());
    std::sort(indices.begin(), indices.end());
    for (const int i : indices)
        pads.append(m_editor->footprint().pads.at(i));
    const bool none = pads.isEmpty();
    if (none)
        pads.append(m_editor->nextPad());

    if (none)
        m_heading->setText(tr("New pads"));
    else if (pads.size() == 1)
        m_heading->setText(padTitle() + QLatin1Char(' ') + pads.first().number);
    else
        m_heading->setText(tr("Pads chosen") + QStringLiteral(": ") + QString::number(pads.size()));

    const QString different = PropertiesPanel::differentMark();
    const auto showText = [&](QLineEdit *edit, auto value, auto text) {
        const bool same = std::all_of(pads.cbegin(), pads.cend(), [&](const Pad &pad) {
            return value(pad) == value(pads.first());
        });
        edit->setPlaceholderText(same ? QString() : different);
        if (!isTyping(edit, force))
            edit->setText(same ? text(value(pads.first())) : QString());
    };
    const auto showChoice = [&](QComboBox *box, auto value) {
        const bool same = std::all_of(pads.cbegin(), pads.cend(), [&](const Pad &pad) {
            return value(pad) == value(pads.first());
        });
        box->setCurrentIndex(same ? int(value(pads.first())) : -1);
    };
    const auto mm = [](Coord nm) { return formatMmShort(nm); };
    m_number->setEnabled(pads.size() == 1 && !none);
    showText(m_number, [](const Pad &pad) { return pad.number; },
             [](const QString &number) { return number; });
    if (none && !isTyping(m_number, force))
        m_number->clear();
    showChoice(m_shape, [](const Pad &pad) { return pad.shape; });
    showText(m_width, [](const Pad &pad) { return pad.sizeX; }, mm);
    showText(m_height, [](const Pad &pad) { return pad.sizeY; }, mm);
    // A round pad has one size.
    m_height->setEnabled(std::none_of(pads.cbegin(), pads.cend(), [](const Pad &pad) {
        return pad.shape == PadShape::Circle;
    }));
    showText(m_hole, [](const Pad &pad) { return pad.hole; }, mm);
    showChoice(m_layers, [](const Pad &pad) { return pad.layers; });
    m_x->setEnabled(!none);
    m_y->setEnabled(!none);
    showText(m_x, [](const Pad &pad) { return pad.at.x; }, mm);
    showText(m_y, [](const Pad &pad) { return pad.at.y; }, mm);
    if (none && !isTyping(m_x, force))
        m_x->clear();
    if (none && !isTyping(m_y, force))
        m_y->clear();
}

template <typename Apply>
void PadPanel::commit(Apply apply)
{
    QList<int> indices(m_shown.cbegin(), m_shown.cend());
    std::sort(indices.begin(), indices.end());
    const Footprint &footprint = m_editor->footprint();
    QList<Pad> changed;
    if (indices.isEmpty())
        changed.append(m_editor->nextPad());
    for (const int i : indices)
        changed.append(footprint.pads.at(i));
    for (Pad &pad : changed) {
        if (!apply(pad)) {
            refresh(true);
            return;
        }
        // A round pad has one size, and one with no hole sits on one side.
        if (pad.shape == PadShape::Circle)
            pad.sizeY = pad.sizeX;
        if (pad.hole == 0 && pad.layers == PadLayers::Both)
            pad.layers = PadLayers::Front;
        const QString problem = padProblem(pad);
        if (!problem.isEmpty()) {
            m_editor->showStatus(capitalized(problem));
            refresh(true);
            return;
        }
    }
    // The next pads placed are like those set.
    m_editor->setNextPad(changed.first());
    if (!indices.isEmpty())
        m_editor->change(FootprintEditor::padPropertiesStep(),
                         withPadsReplaced(footprint, indices, changed), m_shown);
    refresh(true);
}

void PadPanel::commitNumber()
{
    if (m_shown.size() != 1)
        return;
    const QString number = m_number->text().trimmed();
    commit([&number](Pad &pad) {
        pad.number = number;
        return true;
    });
}

void PadPanel::commitShape(int index)
{
    if (index < 0)
        return;
    commit([index](Pad &pad) {
        pad.shape = PadShape(index);
        return true;
    });
}

void PadPanel::commitLayers(int index)
{
    if (index < 0)
        return;
    commit([index](Pad &pad) {
        pad.layers = PadLayers(index);
        return true;
    });
}

void PadPanel::commitMm(QLineEdit *edit)
{
    // An empty field of pads that differ sets nothing.
    if (edit->text().trimmed().isEmpty()) {
        refresh(true);
        return;
    }
    Coord nm = 0;
    if (!parseMm(edit->text(), nm)) {
        refresh(true);
        return;
    }
    commit([this, edit, nm](Pad &pad) {
        if (edit == m_width)
            pad.sizeX = nm;
        else if (edit == m_height)
            pad.sizeY = nm;
        else if (edit == m_hole)
            pad.hole = nm;
        else if (edit == m_x)
            pad.at.x = nm;
        else if (edit == m_y)
            pad.at.y = nm;
        return true;
    });
}

} // namespace ppcb
