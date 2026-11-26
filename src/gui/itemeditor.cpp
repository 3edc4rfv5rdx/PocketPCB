#include "itemeditor.h"

#include "appsettings.h"
#include "canvasview.h"
#include "footprintedit.h"
#include "format.h"
#include "graphicedit.h"
#include "grid.h"
#include "inplaceedit.h"
#include "mainwindow.h"
#include "paint.h"
#include "itempanels.h"
#include "sheeteditor.h"
#include "symboledit.h"

#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QRegularExpression>
#include <QSplitter>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

// How far the mouse goes with the button down before a press is a drag, in
// screen pixels.
const double DRAG_PIXELS = 3.0;
// What follows the cursor, half seen.
const double FLOATING_OPACITY = 0.5;
// The frame round what is chosen, in screen pixels.
const double CHOSEN_PEN_PIXELS = 2.0;
// Half the side of the square on a corner of a chosen drawing, in pixels.
const double HANDLE_PIXELS = 3.0;
// The line a new text is typed into, its width in mm.
const double NEW_TEXT_WIDTH_MM = 12.0;
// The ends of a measurement, how far its text stands off its end, in
// screen pixels, and how much its white back hides.
const double MEASURE_END_PIXELS = 3.0;
const int MEASURE_TEXT_OFFSET = 10;
const int MEASURE_BACK_ALPHA = 220;
// The cross of the origin, its arms and its ring, in screen pixels.
const double ORIGIN_ARM_PIXELS = 14.0;
const double ORIGIN_RING_PIXELS = 5.0;
// The cross of the origin about to be placed, its half length in mm.
const double ORIGIN_CROSS_MM = 2.0;

// What an item's name may hold: it is the name of its file.
const QRegularExpression &nameRule()
{
    static const QRegularExpression rule(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]*$"));
    return rule;
}

// The file of the item 'name' of the kind kept in 'subfolder'.
QString itemPath(const QString &libraryFolder, const char *subfolder, const QString &name)
{
    return QDir(QDir(libraryFolder).filePath(QLatin1String(subfolder)))
        .filePath(name + QLatin1String(LIBRARY_FILE_EXT));
}

// The drawings of 'graphics' given the line of 'line'.
QList<Graphic> withLineOf(QList<Graphic> graphics, const Graphic &line)
{
    for (Graphic &graphic : graphics) {
        graphic.width = line.width;
        graphic.dashed = line.dashed;
    }
    return graphics;
}

bool isDrawingMode(ItemEditor::Mode mode)
{
    switch (mode) {
    case ItemEditor::Mode::Line:
    case ItemEditor::Mode::Rect:
    case ItemEditor::Mode::Circle:
    case ItemEditor::Mode::Arc:
    case ItemEditor::Mode::Polygon:
    case ItemEditor::Mode::Text:
        return true;
    case ItemEditor::Mode::Select:
    case ItemEditor::Mode::Place:
    case ItemEditor::Mode::Row:
    case ItemEditor::Mode::Origin:
    case ItemEditor::Mode::Measure:
        break;
    }
    return false;
}

QPen cosmeticPen(const QColor &color, Qt::PenStyle style = Qt::SolidLine)
{
    QPen pen(color, CHOSEN_PEN_PIXELS, style);
    pen.setCosmetic(true);
    return pen;
}

// The item on the canvas: it hands the mouse to the editor and draws what
// the editor shows.
class ItemCanvasItem : public QGraphicsItem
{
public:
    ItemCanvasItem(ItemEditor *editor, const QRectF &area)
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
        m_editor->paintEditor(*painter);
    }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }
        m_editor->press(event->scenePos(), event->modifiers().testFlag(Qt::ControlModifier),
                        event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
    }
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override
    {
        m_editor->move(event->scenePos(), event->modifiers().testFlag(Qt::ShiftModifier));
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
        m_editor->hover(event->scenePos(), event->modifiers().testFlag(Qt::ShiftModifier));
    }

private:
    ItemEditor *m_editor;
    QRectF m_area;
};

// One step of an item editor: the item before and after it.
template <class Item>
class ItemStep : public QUndoCommand
{
public:
    ItemStep(ItemEditorOf<Item> *editor, const QString &text, const Item &before,
             const Item &after)
        : QUndoCommand(text)
        , m_editor(editor)
        , m_before(before)
        , m_after(after)
    {
    }

    void undo() override { m_editor->applyStep(m_before); }
    void redo() override { m_editor->applyStep(m_after); }

private:
    ItemEditorOf<Item> *m_editor;
    Item m_before;
    Item m_after;
};

} // namespace

bool operator==(const ItemChoice &a, const ItemChoice &b)
{
    return a.terminals == b.terminals && a.graphics == b.graphics
           && a.designator == b.designator && a.value == b.value;
}

// ItemEditor

ItemEditor::ItemEditor(const QString &name, bool inUserLibrary, QWidget *parent)
    : QWidget(parent)
    , m_name(name)
    , m_inUserLibrary(inUserLibrary)
{
}

ItemEditor::~ItemEditor()
{
    // The stack going last says it is clean no more, or again: nobody is to
    // hear it, the tab is gone.
    m_undoStack.disconnect();
}

void ItemEditor::build(ItemPanel *terminalPanel, ItemPanel *itemPanel, Coord gridStep)
{
    m_canvas = new CanvasView(this);
    m_canvas->setObjectName(QStringLiteral("itemCanvas"));
    m_canvas->setGridStyle(AppSettings::gridStyle());
    m_canvas->setCursorStyle(AppSettings::cursorStyle());
    m_canvas->setZoomCenterAndWarp(AppSettings::zoomCenterAndWarp());
    m_canvas->setGridStep(gridStep);
    m_canvas->scene()->addItem(new ItemCanvasItem(this, m_canvas->scene()->sceneRect()));
    m_textEdit = new InPlaceEdit(m_canvas, this);
    m_terminalPanel = terminalPanel;
    m_graphicPanel = new GraphicPanel(this, this);
    m_itemPanel = itemPanel;
    m_panels = new QStackedWidget(this);
    m_panels->addWidget(m_terminalPanel);
    m_panels->addWidget(m_graphicPanel);
    // The panel of what is chosen, that of the item under it.
    QWidget *side = new QWidget(this);
    QVBoxLayout *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->addWidget(m_panels);
    sideLayout->addWidget(m_itemPanel);
    sideLayout->addStretch();

    // The strip over the canvas, the panels beside it.
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_canvas);
    splitter->addWidget(side);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setChildrenCollapsible(false);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createToolBar());
    layout->addWidget(splitter, 1);

    connect(&m_undoStack, &QUndoStack::cleanChanged, this, &ItemEditor::titleChanged);
    connect(this, &ItemEditor::changed, m_canvas->scene(), [this] { m_canvas->scene()->update(); });
    connect(this, &ItemEditor::changed, this, &ItemEditor::showPanel);
    // A short right click calls off what is being placed or drawn, as Esc.
    connect(m_canvas, &CanvasView::contextMenuRequested, this, [this] {
        if (m_mode != Mode::Select || m_dragging || m_framing)
            escape();
    });
    showPanel();
    // Once the tab is shown and the canvas knows its size.
    QTimer::singleShot(0, this, [this] { m_canvas->fitRect(shownBounds()); });
}

QString ItemEditor::drawingPropertiesStep()
{
    return tr("Drawing properties");
}

bool ItemEditor::userLibraryHas(const char *folder, const QString &name)
{
    return QFileInfo::exists(itemPath(AppSettings::userLibraryFolder(), folder, name));
}

QAction *ItemEditor::addTool(QToolBar *bar, const QString &text, const char *name,
                             const QKeySequence &key, std::optional<ToolIcon> icon,
                             const std::function<void()> &slot)
{
    QAction *result = new QAction(text, this);
    result->setObjectName(QLatin1String(name));
    QString tip = text;
    tip.remove(QLatin1Char('&'));
    if (!key.isEmpty()) {
        result->setShortcut(key);
        tip += QStringLiteral(" (") + key.toString(QKeySequence::NativeText) + QLatin1Char(')');
    }
    result->setToolTip(tip);
    // The keys of the tab shown only: the others are hidden.
    result->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(result, &QAction::triggered, this, slot);
    addAction(result);
    if (icon) {
        result->setIcon(toolIcon(*icon, bar->palette().color(QPalette::WindowText)));
        bar->addAction(result);
    }
    return result;
}

void ItemEditor::addMenuButton(QToolBar *bar, QAction *main, QMenu *menu, const char *name)
{
    QToolButton *button = new QToolButton(bar);
    button->setObjectName(QLatin1String(name));
    button->setDefaultAction(main);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    bar->addWidget(button);
}

QWidget *ItemEditor::createToolBar()
{
    QToolBar *bar = new QToolBar(this);
    bar->setObjectName(QStringLiteral("itemBar"));
    bar->setMovable(false);
    styleToolBar(bar);
    const QColor color = bar->palette().color(QPalette::WindowText);
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

    // Save, its arrow offering to save under a new name.
    QAction *saveAction = addTool(bar, MainWindow::saveTitle(), "actionSaveItem",
                                  QKeySequence::Save, std::nullopt, [this] { save(); });
    saveAction->setIcon(toolIcon(ToolIcon::Save, color));
    QAction *saveAs = addTool(bar, MainWindow::saveAsTitle() + QStringLiteral("…"),
                              "actionSaveItemAs", QKeySequence::SaveAs, std::nullopt,
                              [this] { saveUnderNewName(); });
    m_renameAction =
        addTool(bar, SheetEditor::actionName(SheetEditor::Action::Rename) + QStringLiteral("…"),
                "actionRenameItem", QKeySequence(), std::nullopt, [this] { renameItem(); });
    m_renameAction->setEnabled(canRename());
    connect(this, &ItemEditor::titleChanged, m_renameAction,
            [this] { m_renameAction->setEnabled(canRename()); });
    QMenu *saveMenu = new QMenu(this);
    saveMenu->addAction(saveAs);
    saveMenu->addAction(m_renameAction);
    addMenuButton(bar, saveAction, saveMenu, "saveItemButton");
    gap();
    QAction *undo = m_undoStack.createUndoAction(this, MainWindow::undoTitle());
    undo->setObjectName(QStringLiteral("actionUndoItem"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    undo->setIcon(toolIcon(ToolIcon::Undo, color));
    addAction(undo);
    bar->addAction(undo);
    QAction *redo = m_undoStack.createRedoAction(this, MainWindow::redoTitle());
    redo->setObjectName(QStringLiteral("actionRedoItem"));
    redo->setShortcut(QKeySequence::Redo);
    redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    redo->setIcon(toolIcon(ToolIcon::Redo, color));
    addAction(redo);
    bar->addAction(redo);
    gap();
    addTool(bar, MainWindow::zoomInTitle(), "actionZoomInItem", QKeySequence::ZoomIn,
            ToolIcon::ZoomIn, [this] {
                m_canvas->setView(m_canvas->viewCenter(),
                                  m_canvas->pixelsPerMm() * CanvasView::ZOOM_STEP);
            });
    addTool(bar, MainWindow::zoomOutTitle(), "actionZoomOutItem", QKeySequence::ZoomOut,
            ToolIcon::ZoomOut, [this] {
                m_canvas->setView(m_canvas->viewCenter(),
                                  m_canvas->pixelsPerMm() / CanvasView::ZOOM_STEP);
            });
    addTool(bar, MainWindow::fitDrawingTitle(), "actionFitItem", QKeySequence(Qt::Key_Home),
            ToolIcon::ZoomFitDrawing, [this] { m_canvas->fitRect(shownBounds()); });
    stretch();

    // The modes in the middle, the one going on pressed.
    QAction *select = addTool(bar, MainWindow::selectTitle(), "actionSelectItem",
                              QKeySequence(Qt::Key_Escape), ToolIcon::Select, [this] { escape(); });
    m_modeActions.insert(int(Mode::Select), select);
    addPlacingTools(bar);
    gap();
    struct DrawingMode
    {
        Mode mode;
        QString text;
        const char *name;
        QKeySequence key;
        ToolIcon icon;
    };
    const QList<DrawingMode> drawing = {
        {Mode::Line, tr("Line"), "actionDrawLine", QKeySequence(Qt::Key_L), ToolIcon::DrawLine},
        {Mode::Rect, tr("Rectangle, with Shift an oval"), "actionDrawRect", QKeySequence(),
         ToolIcon::DrawRect},
        {Mode::Circle, tr("Circle"), "actionDrawCircle", QKeySequence(), ToolIcon::DrawCircle},
        {Mode::Arc, tr("Arc"), "actionDrawArc", QKeySequence(), ToolIcon::DrawArc},
        {Mode::Polygon, tr("Polygon"), "actionDrawPolygon", QKeySequence(), ToolIcon::DrawPolygon},
        {Mode::Text, tr("Text"), "actionDrawText", QKeySequence(Qt::Key_T), ToolIcon::DrawText},
    };
    for (const DrawingMode &mode : drawing) {
        m_modeActions.insert(int(mode.mode), addTool(bar, mode.text, mode.name, mode.key, mode.icon,
                                                     [this, m = mode.mode] { setMode(m); }));
    }
    m_modeActions.insert(int(Mode::Measure),
                         addTool(bar, tr("Measure"), "actionMeasure", QKeySequence(Qt::Key_M),
                                 ToolIcon::Measure, [this] { setMode(Mode::Measure); }));
    gap();
    QAction *origin = addTool(bar, tr("Origin here"), "actionOriginHere", QKeySequence(Qt::Key_O),
                              std::nullopt, [this] { setMode(Mode::Origin); });
    origin->setIcon(toolIcon(ToolIcon::Origin, color));
    m_modeActions.insert(int(Mode::Origin), origin);
    QMenu *originMenu = new QMenu(this);
    QAction *middle = originMenu->addAction(tr("Origin in the middle"));
    middle->setObjectName(QStringLiteral("actionOriginMiddle"));
    connect(middle, &QAction::triggered, this, &ItemEditor::originAtMiddle);
    addMenuButton(bar, origin, originMenu, "originButton");
    for (QAction *mode : std::as_const(m_modeActions))
        mode->setCheckable(true);
    stretch();

    m_gridBox = new QComboBox(bar);
    m_gridBox->setObjectName(QStringLiteral("itemGridBox"));
    for (const Coord step : gridSteps())
        m_gridBox->addItem(MainWindow::gridStepText(step), QVariant::fromValue<qlonglong>(step));
    m_gridBox->setCurrentIndex(
        m_gridBox->findData(QVariant::fromValue<qlonglong>(m_canvas->gridStep())));
    connect(m_gridBox, &QComboBox::activated, this, [this](int index) {
        m_canvas->setGridStep(m_gridBox->itemData(index).toLongLong());
    });
    connect(m_canvas, &CanvasView::gridStepChanged, this, [this](Coord step) {
        m_gridBox->setCurrentIndex(m_gridBox->findData(QVariant::fromValue<qlonglong>(step)));
    });
    bar->addWidget(m_gridBox);

    // Keys with no button.
    addTool(bar, SheetEditor::actionName(SheetEditor::Action::Rotate), "actionRotateItem",
            QKeySequence(Qt::Key_R), std::nullopt, [this] { rotate(); });
    addTool(bar, SheetEditor::actionName(SheetEditor::Action::Delete), "actionDeleteItem",
            QKeySequence::Delete, std::nullopt, [this] { deleteChosen(); });
    addTool(bar, MainWindow::selectAllTitle(), "actionChooseAllItem", QKeySequence::SelectAll,
            std::nullopt, [this] { chooseAll(); });
    updateModeButtons();
    return bar;
}

QString ItemEditor::tabTitle() const
{
    const QString title = m_name.isEmpty() ? newItemTitle() : m_name;
    return m_undoStack.isClean() ? title : title + QLatin1Char('*');
}

QString ItemEditor::tabTip() const
{
    QString tip = editorTitle() + QStringLiteral(": ") + (m_name.isEmpty() ? newItemTitle() : m_name);
    if (!m_name.isEmpty() && !m_inUserLibrary)
        tip += QStringLiteral(" (") + tr("from the built-in library") + QLatin1Char(')');
    return tip;
}

void ItemEditor::updateModeButtons()
{
    for (auto it = m_modeActions.constBegin(); it != m_modeActions.constEnd(); ++it)
        it.value()->setChecked(it.key() == int(m_mode));
}

void ItemEditor::showPanel()
{
    const bool drawings = isDrawingMode(m_mode)
                          || (!m_chosen.graphics.isEmpty() && m_chosen.terminals.isEmpty());
    m_panels->setCurrentWidget(drawings ? static_cast<QWidget *>(m_graphicPanel) : m_terminalPanel);
    m_terminalPanel->refresh();
    m_graphicPanel->refresh();
    m_itemPanel->refresh();
}

void ItemEditor::paintHandle(QPainter &painter, const Point &at) const
{
    const double handle = m_canvas->pixelsToMm(HANDLE_PIXELS);
    painter.drawRect(QRectF(toMm(at) - QPointF(handle, handle), QSizeF(2 * handle, 2 * handle)));
}

void ItemEditor::showStatus(const QString &text)
{
    m_status = text;
    emit statusChanged(text);
}

void ItemEditor::alert(const Alert &alert)
{
    Alerts::show(this, alert, [this](const QString &text) { showStatus(text); });
}

Point ItemEditor::snapped(const QPointF &at) const
{
    return m_canvas->snapped(at);
}

Point ItemEditor::terminalSnapped(const QPointF &at) const
{
    const Coord step = terminalGrid();
    if (step <= 0)
        return snapped(at);
    return Point{snapToGrid(mmToNm(at.x()), step), snapToGrid(mmToNm(at.y()), step)};
}

void ItemEditor::setMode(Mode mode)
{
    m_mode = mode;
    m_dragging = false;
    m_framing = false;
    m_drawPoints.clear();
    m_measureFrom.reset();
    m_measureTo.reset();
    updateModeButtons();
    emit changed();
}

void ItemEditor::setChosen(const ItemChoice &chosen)
{
    ItemChoice valid;
    const int terminals = terminalCount();
    for (const int i : chosen.terminals) {
        if (i >= 0 && i < terminals)
            valid.terminals.insert(i);
    }
    const int drawings = int(graphics().size());
    for (const int i : chosen.graphics) {
        if (i >= 0 && i < drawings)
            valid.graphics.insert(i);
    }
    valid.designator = chosen.designator;
    valid.value = chosen.value;
    if (valid == m_chosen)
        return;
    m_chosen = valid;
    describeChoice();
    emit changed();
}

void ItemEditor::setNextGraphic(const Graphic &graphic)
{
    m_nextGraphic.width = graphic.width;
    m_nextGraphic.dashed = graphic.dashed;
    emit changed();
}

Point ItemEditor::drawPoint(const Point &cursor) const
{
    const bool straight = m_mode == Mode::Line || m_mode == Mode::Polygon;
    if (!straight || m_free || m_drawPoints.isEmpty())
        return cursor;
    return keptStraightOrDiagonal(m_drawPoints.last(), cursor);
}

QList<Graphic> ItemEditor::floatingGraphics() const
{
    if (!m_cursorKnown || m_drawPoints.isEmpty())
        return {};
    const Point at = drawPoint(m_cursor);
    const Point first = m_drawPoints.first();
    QList<Graphic> graphics;
    switch (m_mode) {
    case Mode::Line:
        graphics.append(lineBetween(m_drawPoints.last(), at));
        break;
    case Mode::Rect:
        graphics.append(m_free ? ovalBetween(first, at) : rectBetween(first, at));
        break;
    case Mode::Circle:
        if (at != first)
            graphics.append(circleThrough(first, at));
        break;
    case Mode::Arc:
        // From the start to the middle clicked, then the arc through both.
        if (m_drawPoints.size() == 1)
            graphics.append(lineBetween(first, at));
        else if (const std::optional<Graphic> arc = arcThrough(first, m_drawPoints.at(1), at))
            graphics.append(*arc);
        break;
    case Mode::Polygon:
        for (int i = 1; i < m_drawPoints.size(); ++i)
            graphics.append(lineBetween(m_drawPoints.at(i - 1), m_drawPoints.at(i)));
        graphics.append(lineBetween(m_drawPoints.last(), at));
        break;
    case Mode::Select:
    case Mode::Place:
    case Mode::Row:
    case Mode::Origin:
    case Mode::Text:
    case Mode::Measure:
        break;
    }
    return withLineOf(graphics, m_nextGraphic);
}

std::optional<Point> ItemEditor::floatingOrigin() const
{
    if (m_mode == Mode::Origin && m_cursorKnown)
        return terminalSnapped(m_hoverAt);
    return std::nullopt;
}

QRectF ItemEditor::frame() const
{
    return m_framing ? QRectF(m_frameFrom, m_frameTo).normalized() : QRectF();
}

std::optional<QLineF> ItemEditor::measurement() const
{
    if (m_mode != Mode::Measure || !m_measureFrom)
        return std::nullopt;
    const Point to = m_measureTo ? *m_measureTo : measurePoint(m_hoverAt);
    return QLineF(toMm(*m_measureFrom), toMm(to));
}

void ItemEditor::move(const QPointF &at, bool free)
{
    hover(at, free);
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
    Point delta;
    const Coord step = terminalGrid();
    if (step > 0 && m_dragGraphic < 0 && m_dragTerminal < 0 && !m_chosen.terminals.isEmpty()) {
        // Terminals go by whole steps of their grid, whatever is taken.
        const QPointF moved = at - m_pressAt;
        delta = Point{snapToGrid(mmToNm(moved.x()), step), snapToGrid(mmToNm(moved.y()), step)};
    } else {
        delta = snapped(toMm(m_dragAnchor) + (at - m_pressAt)) - m_dragAnchor;
    }
    if (delta != m_dragDelta) {
        m_dragDelta = delta;
        emit changed();
    }
}

void ItemEditor::hover(const QPointF &at, bool free)
{
    m_hoverAt = at;
    const Point cursor = snapped(at);
    if (m_cursorKnown && cursor == m_cursor && free == m_free) {
        // A measurement under way follows the mouse off the grid too.
        if (m_mode == Mode::Measure && m_measureFrom && !m_measureTo)
            emit changed();
        return;
    }
    m_cursor = cursor;
    m_cursorKnown = true;
    m_free = free;
    if (m_mode != Mode::Select)
        emit changed();
}

void ItemEditor::chooseAll()
{
    ItemChoice all;
    for (int i = 0; i < terminalCount(); ++i)
        all.terminals.insert(i);
    for (int i = 0; i < int(graphics().size()); ++i)
        all.graphics.insert(i);
    setMode(Mode::Select);
    setChosen(all);
}

void ItemEditor::escape()
{
    if (m_dragging || m_framing) {
        m_dragging = false;
        m_framing = false;
        m_dragDelta = Point{};
        m_dragGraphic = -1;
        m_dragTerminal = -1;
        emit changed();
    } else if (!m_drawPoints.isEmpty()) {
        // A line ends where it got to; a polygon not closed is dropped.
        m_drawPoints.clear();
        emit changed();
    } else if (m_measureFrom) {
        m_measureFrom.reset();
        m_measureTo.reset();
        emit changed();
    } else if (m_mode != Mode::Select) {
        setMode(Mode::Select);
    } else {
        setChosen({});
    }
    updateModeButtons();
}

bool ItemEditor::save()
{
    if (!m_name.isEmpty())
        return saveAs(m_name, true);
    const QString name = askName(MainWindow::saveTitle().remove(QLatin1Char('&')));
    return !name.isEmpty() && saveAs(name, true);
}

bool ItemEditor::saveUnderNewName()
{
    const QString name = askName(MainWindow::saveAsTitle().remove(QLatin1Char('&')));
    return !name.isEmpty() && saveAs(name, true);
}

QString ItemEditor::askName(const QString &title)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, title, nameLabel() + QLatin1Char(':'),
                                               QLineEdit::Normal, m_name, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return QString();
    if (!nameRule().match(name).hasMatch()) {
        alert(Alert::warning(tr("A name may hold Latin letters, digits and these signs")
                                 + QStringLiteral(": . _ + -"),
                             QStringLiteral("footprintName")));
        return QString();
    }
    // Its own name it keeps without asking.
    const char *folder = libraryFolder();
    const bool taken =
        name != m_name
        && (QFileInfo::exists(itemPath(AppSettings::userLibraryFolder(), folder, name))
            || QFileInfo::exists(itemPath(AppSettings::builtInLibraryFolder(), folder, name)));
    if (taken
        && QMessageBox::question(this, title, takenQuestion(name) + QLatin1Char('?'))
               != QMessageBox::Yes)
        return QString();
    return name;
}

bool ItemEditor::saveAs(const QString &name, bool replace)
{
    const QString path = itemPath(AppSettings::userLibraryFolder(), libraryFolder(), name);
    if (!replace && QFileInfo::exists(path))
        return false;
    const QString problem = saveProblem();
    if (!problem.isEmpty()) {
        alert(Alert::error(capitalized(problem)));
        return false;
    }
    QString error;
    if (!AppSettings::makeUserLibraryFolders(error)) {
        alert(Alert::error(error));
        return false;
    }
    if (!LibraryFile::write(path, libraryItem(), error)) {
        alert(Alert::error(error));
        return false;
    }
    m_name = name;
    m_inUserLibrary = true;
    m_undoStack.setClean();
    emit titleChanged();
    showStatus(writtenMessage(path));
    emit saved(name);
    return true;
}

bool ItemEditor::renameItem()
{
    if (!canRename())
        return false;
    const QString title = SheetEditor::actionName(SheetEditor::Action::Rename);
    const QString name = askName(title);
    if (name.isEmpty() || name == m_name)
        return false;
    // Built-in parts listing it, not replaced by parts of the user's own,
    // keep the old name.
    QStringList problems;
    const Library builtIn = LibraryFolder::read(AppSettings::builtInLibraryFolder(), problems);
    const Library user = LibraryFolder::read(AppSettings::userLibraryFolder(), problems);
    QStringList stranded;
    for (const QString &part : partsUsing(builtIn, m_name)) {
        if (!user.parts.contains(part))
            stranded.append(part);
    }
    if (!stranded.isEmpty()
        && QMessageBox::question(this, title,
                                 strandedQuestion(m_name, stranded) + QLatin1Char('?'))
               != QMessageBox::Yes)
        return false;
    return renameTo(name);
}

bool ItemEditor::renameTo(const QString &name)
{
    if (!canRename() || name.isEmpty() || name == m_name)
        return false;
    const QString old = m_name;
    const QString userFolder = AppSettings::userLibraryFolder();
    QStringList problems;
    const Library user = LibraryFolder::read(userFolder, problems);
    if (!saveAs(name, true))
        return false;
    // The user's parts follow it.
    for (const QString &partName : partsUsing(user, old)) {
        LibraryItem item;
        item.kind = LibraryKind::Part;
        item.part = partRenamed(user.parts.value(partName), old, name);
        QString error;
        if (!LibraryFile::write(itemPath(userFolder, LIBRARY_PARTS_FOLDER, partName), item, error)) {
            alert(Alert::error(error));
            emit saved(name);
            return false;
        }
    }
    QFile::remove(itemPath(userFolder, libraryFolder(), old));
    showStatus(renamedMessage(old, name));
    emit saved(name);
    return true;
}

bool ItemEditor::confirmClose()
{
    if (m_undoStack.isClean())
        return true;
    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, editorTitle(), unsavedQuestion() + QLatin1Char('?'),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save());
}

// ItemEditorOf

template <class Item>
ItemEditorOf<Item>::ItemEditorOf(const QString &name, const Item &item, bool inUserLibrary,
                                 QWidget *parent)
    : ItemEditor(name, inUserLibrary, parent)
    , m_item(item)
{
}

template <class Item>
void ItemEditorOf<Item>::change(const QString &text, const Item &changed, ItemChoice chosen)
{
    if (changed == m_item) {
        setChosen(chosen);
        return;
    }
    m_undoStack.push(new ItemStep<Item>(this, text, m_item, changed));
    setChosen(chosen);
    // The same terminals chosen, moved: told again.
    describeChoice();
}

template <class Item>
void ItemEditorOf<Item>::applyStep(const Item &item)
{
    m_item = item;
    m_dragging = false;
    m_chosen = ItemChoice();
    itemReplaced();
    emit changed();
}

template <class Item>
void ItemEditorOf<Item>::changeGraphics(const QString &text, const QList<Graphic> &changed)
{
    Item result = m_item;
    result.graphics = changed;
    change(text, result, m_chosen);
}

template <class Item>
Item ItemEditorOf<Item>::withChoiceMoved(const Item &item, const Point &delta) const
{
    Item result = withTerminalsMoved(item, m_chosen.terminals, delta);
    result.graphics = withGraphicsMoved(result.graphics, m_chosen.graphics, delta);
    if (m_chosen.designator)
        result.refAt = result.refAt + delta;
    if (m_chosen.value)
        result.valueAt = result.valueAt + delta;
    return result;
}

template <class Item>
Item ItemEditorOf<Item>::shownItem() const
{
    if (!m_dragging || m_dragDelta == Point{})
        return m_item;
    if (m_dragTerminal >= 0)
        return withTerminalEnd(m_item, m_dragTerminal, m_dragAnchor + m_dragDelta);
    if (m_dragGraphic >= 0) {
        Item result = m_item;
        result.graphics[m_dragGraphic] = withCornerMoved(m_item.graphics.at(m_dragGraphic),
                                                         m_dragCorner, m_dragAnchor + m_dragDelta);
        return result;
    }
    return withChoiceMoved(m_item, m_dragDelta);
}

template <class Item>
QRectF ItemEditorOf<Item>::choiceBounds() const
{
    QList<QRectF> rects;
    if (!m_chosen.terminals.isEmpty())
        rects.append(terminalsBounds(m_item, m_chosen.terminals));
    if (!m_chosen.graphics.isEmpty())
        rects.append(graphicsBounds(m_item.graphics, m_chosen.graphics));
    if (m_chosen.designator)
        rects.append(placeholderRect(m_item.refAt, ComponentText::Designator));
    if (m_chosen.value)
        rects.append(placeholderRect(m_item.valueAt, ComponentText::Value));
    QRectF bounds;
    for (int i = 0; i < rects.size(); ++i) {
        const QRectF &rect = rects.at(i);
        bounds = i == 0 ? rect
                        : QRectF(QPointF(std::min(bounds.left(), rect.left()),
                                         std::min(bounds.top(), rect.top())),
                                 QPointF(std::max(bounds.right(), rect.right()),
                                         std::max(bounds.bottom(), rect.bottom())));
    }
    return bounds;
}

template <class Item>
Point ItemEditorOf<Item>::measurePoint(const QPointF &at) const
{
    const int terminal = terminalAt(at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
    if (terminal >= 0)
        return terminalPoint(terminal);
    const std::optional<Point> drawn =
        graphicPointNear(m_item.graphics, at, m_canvas->pixelsToMm(CanvasView::END_PIXELS));
    if (drawn)
        return *drawn;
    // With Shift the very point, off the grid.
    return m_free ? Point{mmToNm(at.x()), mmToNm(at.y())} : snapped(at);
}

template <class Item>
void ItemEditorOf<Item>::addGraphic(const QString &text, Graphic graphic)
{
    if (graphic.kind != GraphicKind::Text) {
        graphic.width = m_nextGraphic.width;
        graphic.dashed = m_nextGraphic.dashed;
    }
    Item changed = m_item;
    changed.graphics.append(graphic);
    ItemChoice chosen;
    chosen.graphics = {int(changed.graphics.size()) - 1};
    change(text, changed, chosen);
}

template <class Item>
void ItemEditorOf<Item>::drawClick(const Point &cursor)
{
    const Point at = drawPoint(cursor);
    const QString step = tr("Draw");
    switch (m_mode) {
    case Mode::Line:
        if (!m_drawPoints.isEmpty() && at != m_drawPoints.last())
            addGraphic(step, lineBetween(m_drawPoints.last(), at));
        // Each line goes on from the end of the last.
        m_drawPoints = {at};
        break;
    case Mode::Rect:
        if (m_drawPoints.isEmpty()) {
            m_drawPoints = {at};
        } else if (at.x != m_drawPoints.first().x && at.y != m_drawPoints.first().y) {
            // With Shift an oval in it.
            addGraphic(step, m_free ? ovalBetween(m_drawPoints.first(), at)
                                    : rectBetween(m_drawPoints.first(), at));
            m_drawPoints.clear();
        }
        break;
    case Mode::Circle:
        if (m_drawPoints.isEmpty()) {
            m_drawPoints = {at};
        } else if (at != m_drawPoints.first()) {
            addGraphic(step, circleThrough(m_drawPoints.first(), at));
            m_drawPoints.clear();
        }
        break;
    case Mode::Arc:
        // The start, a point in the middle, the end; a point again, or one
        // on the line of the others, is passed over.
        if (m_drawPoints.isEmpty()) {
            m_drawPoints = {at};
        } else if (m_drawPoints.contains(at)) {
        } else if (m_drawPoints.size() == 1) {
            m_drawPoints.append(at);
        } else if (const std::optional<Graphic> arc =
                       arcThrough(m_drawPoints.first(), m_drawPoints.at(1), at)) {
            addGraphic(step, *arc);
            m_drawPoints.clear();
        }
        break;
    case Mode::Polygon:
        if (m_drawPoints.size() >= 3 && at == m_drawPoints.first())
            finishDrawing();
        else if (m_drawPoints.isEmpty() || at != m_drawPoints.last())
            m_drawPoints.append(at);
        break;
    case Mode::Text:
        editText(at, -1);
        break;
    case Mode::Select:
    case Mode::Place:
    case Mode::Row:
    case Mode::Origin:
    case Mode::Measure:
        break;
    }
    emit changed();
}

template <class Item>
void ItemEditorOf<Item>::finishDrawing()
{
    if (m_mode == Mode::Polygon && m_drawPoints.size() >= 3)
        addGraphic(tr("Draw"), polygonOf(m_drawPoints));
    m_drawPoints.clear();
    emit changed();
}

template <class Item>
void ItemEditorOf<Item>::press(const QPointF &at, bool adding, bool free)
{
    m_textEdit->close(true);
    m_pressAt = at;
    m_free = free;
    m_cursor = snapped(at);
    m_cursorKnown = true;
    switch (m_mode) {
    case Mode::Place:
    case Mode::Row:
        placeAt(at);
        return;
    case Mode::Origin: {
        const Point origin = terminalSnapped(at);
        m_mode = Mode::Select;
        updateModeButtons();
        change(tr("Move origin"), withOriginAt(m_item, origin), m_chosen);
        // The drawing stays where it was on screen.
        m_canvas->setView(m_canvas->viewCenter() - toMm(origin), m_canvas->pixelsPerMm());
        m_cursor = m_cursor - origin;
        return;
    }
    case Mode::Line:
    case Mode::Rect:
    case Mode::Circle:
    case Mode::Arc:
    case Mode::Polygon:
    case Mode::Text:
        drawClick(m_cursor);
        return;
    case Mode::Measure: {
        const Point point = measurePoint(at);
        // A new measurement after one is done.
        if (!m_measureFrom || m_measureTo) {
            m_measureFrom = point;
            m_measureTo.reset();
        } else {
            m_measureTo = point;
        }
        emit changed();
        return;
    }
    case Mode::Select:
        break;
    }

    const double reach = m_canvas->pixelsToMm(CanvasView::HIT_PIXELS);
    m_pressAlone.reset();
    m_dragGraphic = -1;
    m_dragCorner = -1;
    m_dragTerminal = -1;
    // An end of a chosen terminal, then a corner of a drawing, then an end
    // of a terminal not chosen, is dragged alone.
    const double cornerReach = m_canvas->pixelsToMm(CanvasView::END_PIXELS);
    if (!adding) {
        if (const int terminal = terminalEndNear(at, cornerReach, true); terminal >= 0) {
            dragTerminalEnd(terminal);
            return;
        }
        for (int i = int(m_item.graphics.size()) - 1; i >= 0; --i) {
            const int corner = cornerAt(m_item.graphics.at(i), at, cornerReach);
            if (corner >= 0) {
                dragCorner(i, corner);
                return;
            }
        }
        if (const int terminal = terminalEndNear(at, cornerReach, false); terminal >= 0) {
            dragTerminalEnd(terminal);
            return;
        }
    }
    // What is under the press: the drawings over the terminals, then the
    // terminals, then the places of the texts, which stand on the origin at
    // first, then a circle round it.
    ItemChoice hit;
    Point anchor;
    const int graphic = graphicAt(m_item.graphics, at, reach);
    const int terminal = terminalAt(at, reach);
    if (graphic >= 0) {
        hit.graphics = {graphic};
        const Graphic &g = m_item.graphics.at(graphic);
        anchor = g.kind == GraphicKind::Polygon && !g.points.isEmpty() ? g.points.first() : g.a;
    } else if (terminal >= 0) {
        hit.terminals = {terminal};
        anchor = terminalPoint(terminal);
    } else if (placeholderRect(m_item.refAt, ComponentText::Designator).contains(at)) {
        hit.designator = true;
        anchor = m_item.refAt;
    } else if (placeholderRect(m_item.valueAt, ComponentText::Value).contains(at)) {
        hit.value = true;
        anchor = m_item.valueAt;
    } else if (const int around = circleAround(m_item.graphics, at); around >= 0) {
        // Inside a circle with nothing else there: the circle.
        hit.graphics = {around};
        anchor = m_item.graphics.at(around).a;
    }
    if (hit.isEmpty()) {
        m_framing = true;
        m_frameAdding = adding;
        m_frameFrom = at;
        m_frameTo = at;
        if (!adding)
            setChosen({});
        emit changed();
        return;
    }
    const bool already = m_chosen.terminals.contains(hit.terminals)
                         && m_chosen.graphics.contains(hit.graphics)
                         && (!hit.designator || m_chosen.designator)
                         && (!hit.value || m_chosen.value);
    if (adding) {
        ItemChoice chosen = m_chosen;
        if (already) {
            chosen.terminals.subtract(hit.terminals);
            chosen.graphics.subtract(hit.graphics);
            chosen.designator = chosen.designator && !hit.designator;
            chosen.value = chosen.value && !hit.value;
        } else {
            chosen.terminals.unite(hit.terminals);
            chosen.graphics.unite(hit.graphics);
            chosen.designator = chosen.designator || hit.designator;
            chosen.value = chosen.value || hit.value;
        }
        setChosen(chosen);
        return;
    }
    if (already)
        m_pressAlone = hit;
    else
        setChosen(hit);
    m_dragging = true;
    m_dragAnchor = anchor;
    m_dragDelta = Point{};
}

template <class Item>
int ItemEditorOf<Item>::terminalEndNear(const QPointF &at, double reach, bool chosenOnly) const
{
    for (int i = terminalCount() - 1; i >= 0; --i) {
        if (chosenOnly && !m_chosen.terminals.contains(i))
            continue;
        const std::optional<Point> end = terminalEnd(i);
        if (!end)
            continue;
        // Its third nearest the end at most, zoomed out as well: the rest of
        // it takes the whole terminal.
        const double length = QLineF(toMm(*end), toMm(terminalPoint(i))).length();
        if (QLineF(toMm(*end), at).length() <= std::min(reach, length / 3))
            return i;
    }
    return -1;
}

template <class Item>
void ItemEditorOf<Item>::dragCorner(int graphic, int corner)
{
    ItemChoice alone;
    alone.graphics = {graphic};
    setChosen(alone);
    m_dragging = true;
    m_dragGraphic = graphic;
    m_dragCorner = corner;
    m_dragAnchor = graphicCorners(m_item.graphics.at(graphic)).at(corner);
    m_dragDelta = Point{};
}

template <class Item>
void ItemEditorOf<Item>::dragTerminalEnd(int terminal)
{
    ItemChoice alone;
    alone.terminals = {terminal};
    setChosen(alone);
    m_dragging = true;
    m_dragTerminal = terminal;
    m_dragAnchor = *terminalEnd(terminal);
    m_dragDelta = Point{};
}

template <class Item>
void ItemEditorOf<Item>::release(const QPointF &at)
{
    if (m_framing) {
        m_framing = false;
        m_frameTo = at;
        const QRectF rect = QRectF(m_frameFrom, m_frameTo).normalized();
        ItemChoice chosen = m_frameAdding ? m_chosen : ItemChoice();
        chosen.terminals.unite(terminalsInside(rect));
        chosen.graphics.unite(graphicsInside(m_item.graphics, rect));
        chosen.designator =
            chosen.designator
            || rect.contains(placeholderRect(m_item.refAt, ComponentText::Designator));
        chosen.value =
            chosen.value || rect.contains(placeholderRect(m_item.valueAt, ComponentText::Value));
        setChosen(chosen);
        emit changed();
        return;
    }
    if (!m_dragging)
        return;
    const Item moved = shownItem();
    const bool dragged = m_dragDelta != Point{};
    m_dragging = false;
    m_dragDelta = Point{};
    m_dragGraphic = -1;
    m_dragCorner = -1;
    m_dragTerminal = -1;
    if (dragged) {
        change(SheetEditor::actionName(SheetEditor::Action::Move), moved, m_chosen);
    } else if (m_pressAlone) {
        // A click on one of several chosen chooses it alone.
        setChosen(*m_pressAlone);
    }
    m_pressAlone.reset();
    emit changed();
}

template <class Item>
void ItemEditorOf<Item>::doubleClick(const QPointF &at)
{
    switch (m_mode) {
    case Mode::Line:
    case Mode::Polygon:
        // The first click of the two put its point down already.
        finishDrawing();
        return;
    case Mode::Select:
        break;
    case Mode::Place:
    case Mode::Row:
    case Mode::Origin:
    case Mode::Rect:
    case Mode::Circle:
    case Mode::Arc:
    case Mode::Text:
    case Mode::Measure:
        // The second of two quick clicks is a click of its own.
        press(at, false, m_free);
        return;
    }
    const double reach = m_canvas->pixelsToMm(CanvasView::HIT_PIXELS);
    const int graphic = graphicAt(m_item.graphics, at, reach);
    if (graphic >= 0 && m_item.graphics.at(graphic).kind == GraphicKind::Text) {
        editText(m_item.graphics.at(graphic).a, graphic);
        return;
    }
    const int terminal = terminalAt(at, reach);
    if (terminal >= 0) {
        ItemChoice alone;
        alone.terminals = {terminal};
        setChosen(alone);
        m_dragging = false;
        editTerminal(terminal);
    }
}

template <class Item>
void ItemEditorOf<Item>::editText(const Point &at, int graphic)
{
    m_dragging = false;
    disconnect(m_textEdit, &InPlaceEdit::accepted, this, nullptr);
    connect(m_textEdit, &InPlaceEdit::accepted, this, [this, at, graphic](const QString &typed) {
        const QString text = typed.trimmed();
        // An empty text is none: a text given none keeps its own.
        if (text.isEmpty())
            return;
        if (graphic < 0) {
            addGraphic(tr("Draw"), textAt(at, text));
            return;
        }
        if (graphic >= m_item.graphics.size())
            return;
        Item changed = m_item;
        changed.graphics[graphic].text = text;
        ItemChoice chosen;
        chosen.graphics = {graphic};
        change(drawingPropertiesStep(), changed, chosen);
    });
    const QString text = graphic >= 0 ? m_item.graphics.at(graphic).text : QString();
    const QRectF rect =
        graphic >= 0 ? graphicRect(m_item.graphics.at(graphic))
                     : QRectF(toMm(at) - QPointF(NEW_TEXT_WIDTH_MM / 2, nmToMm(DEFAULT_TEXT_HEIGHT)),
                              QSizeF(NEW_TEXT_WIDTH_MM, 2 * nmToMm(DEFAULT_TEXT_HEIGHT)));
    m_textEdit->open(text, rect);
}

template <class Item>
void ItemEditorOf<Item>::rotate()
{
    if (m_mode == Mode::Place || m_mode == Mode::Row) {
        rotatePlacing();
        return;
    }
    if (m_chosen.isEmpty() || m_dragging)
        return;
    // About a point of the terminals' grid, so that they stay on it.
    const QPointF middle = choiceBounds().center();
    const Point center = m_chosen.terminals.isEmpty() ? snapped(middle) : terminalSnapped(middle);
    Item changed = withTerminalsRotated(m_item, m_chosen.terminals, center);
    changed.graphics = withGraphicsRotated(changed.graphics, m_chosen.graphics, center);
    if (m_chosen.designator)
        changed.refAt = turnedQuarter(changed.refAt, center);
    if (m_chosen.value)
        changed.valueAt = turnedQuarter(changed.valueAt, center);
    change(SheetEditor::actionName(SheetEditor::Action::Rotate), changed, m_chosen);
}

template <class Item>
void ItemEditorOf<Item>::deleteChosen()
{
    if ((m_chosen.terminals.isEmpty() && m_chosen.graphics.isEmpty()) || m_dragging)
        return;
    Item changed = withoutTerminals(m_item, m_chosen.terminals);
    changed.graphics = withoutGraphics(changed.graphics, m_chosen.graphics);
    change(SheetEditor::actionName(SheetEditor::Action::Delete), changed, {});
}

template <class Item>
void ItemEditorOf<Item>::originAtMiddle()
{
    const Point origin = terminalSnapped(toMm(itemMiddle()));
    if (origin == Point{})
        return;
    setMode(Mode::Select);
    change(tr("Move origin"), withOriginAt(m_item, origin), m_chosen);
    m_canvas->setView(m_canvas->viewCenter() - toMm(origin), m_canvas->pixelsPerMm());
}

template <class Item>
void ItemEditorOf<Item>::paintEditor(QPainter &painter) const
{
    const Item shown = shownItem();
    paintShown(painter, shown);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(cosmeticPen(paintcolor::selection()));
    paintChosenTerminals(painter, shown);
    for (const int i : m_chosen.graphics) {
        if (i < 0 || i >= shown.graphics.size())
            continue;
        const Graphic &graphic = shown.graphics.at(i);
        const QPolygonF outline = graphicOutline(graphic);
        if (outline.isEmpty())
            painter.drawRect(graphicRect(graphic));
        else
            painter.drawPolyline(outline);
        // The corners that can be dragged.
        for (const Point &corner : graphicCorners(graphic))
            paintHandle(painter, corner);
    }
    if (m_chosen.designator)
        painter.drawRect(placeholderRect(shown.refAt, ComponentText::Designator));
    if (m_chosen.value)
        painter.drawRect(placeholderRect(shown.valueAt, ComponentText::Value));

    painter.setOpacity(FLOATING_OPACITY);
    paintFloating(painter, floatingGraphics());
    painter.setOpacity(1.0);
    if (const std::optional<Point> origin = floatingOrigin()) {
        painter.setPen(cosmeticPen(paintcolor::footprintOrigin()));
        const QPointF at = toMm(*origin);
        painter.drawLine(at - QPointF(ORIGIN_CROSS_MM, 0), at + QPointF(ORIGIN_CROSS_MM, 0));
        painter.drawLine(at - QPointF(0, ORIGIN_CROSS_MM), at + QPointF(0, ORIGIN_CROSS_MM));
    }
    if (const std::optional<QLineF> measured = measurement()) {
        const QLineF line = *measured;
        const double pixel = m_canvas->pixelsToMm(1);
        painter.setPen(cosmeticPen(paintcolor::selection(), Qt::DashLine));
        painter.drawLine(line);
        painter.setPen(cosmeticPen(paintcolor::selection()));
        for (const QPointF &end : {line.p1(), line.p2()})
            painter.drawEllipse(end, MEASURE_END_PIXELS * pixel, MEASURE_END_PIXELS * pixel);
        const QString text = QStringLiteral("ΔX %1   ΔY %2   %3")
                                 .arg(formatMmShort(mmToNm(line.dx())),
                                      formatMmShort(mmToNm(line.dy())),
                                      formatMmShortUnit(mmToNm(line.length())));
        // Beside the far end, the same size at any zoom, on white.
        painter.save();
        painter.translate(line.p2());
        painter.scale(pixel, pixel);
        const QRectF box = painter.fontMetrics()
                               .boundingRect(text)
                               .translated(MEASURE_TEXT_OFFSET, -MEASURE_TEXT_OFFSET)
                               .adjusted(-3, -2, 3, 2);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(255, 255, 255, MEASURE_BACK_ALPHA));
        painter.drawRect(box);
        painter.setPen(paintcolor::selection());
        painter.drawText(QPointF(MEASURE_TEXT_OFFSET, -MEASURE_TEXT_OFFSET), text);
        painter.restore();
    }
    // The origin, the point the item is held by and turns about, over all,
    // the same size on screen at any zoom.
    if (!floatingOrigin()) {
        const double pixel = m_canvas->pixelsToMm(1);
        const double arm = ORIGIN_ARM_PIXELS * pixel;
        painter.setPen(cosmeticPen(paintcolor::footprintOrigin()));
        painter.drawLine(QPointF(-arm, 0), QPointF(arm, 0));
        painter.drawLine(QPointF(0, -arm), QPointF(0, arm));
        painter.drawEllipse(QPointF(0, 0), ORIGIN_RING_PIXELS * pixel, ORIGIN_RING_PIXELS * pixel);
    }
    const QRectF rect = frame();
    if (!rect.isNull()) {
        QPen framePen = cosmeticPen(paintcolor::selection(), Qt::DashLine);
        framePen.setWidthF(1);
        painter.setPen(framePen);
        painter.drawRect(rect);
    }
    painter.restore();
}

template class ItemEditorOf<Footprint>;
template class ItemEditorOf<Symbol>;

} // namespace ppcb
