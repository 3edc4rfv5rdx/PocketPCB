#include "footprinteditor.h"

#include "appsettings.h"
#include "canvasview.h"
#include "format.h"
#include "graphicedit.h"
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
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

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

// The drawings of 'graphics' given the line of 'line'.
QList<Graphic> withLineOf(QList<Graphic> graphics, const Graphic &line)
{
    for (Graphic &graphic : graphics) {
        graphic.width = line.width;
        graphic.dashed = line.dashed;
    }
    return graphics;
}

bool isDrawingMode(FootprintEditor::Mode mode)
{
    switch (mode) {
    case FootprintEditor::Mode::Line:
    case FootprintEditor::Mode::Rect:
    case FootprintEditor::Mode::Circle:
    case FootprintEditor::Mode::Arc:
    case FootprintEditor::Mode::Polygon:
    case FootprintEditor::Mode::Text:
        return true;
    case FootprintEditor::Mode::Select:
    case FootprintEditor::Mode::Pad:
    case FootprintEditor::Mode::Row:
    case FootprintEditor::Mode::Origin:
    case FootprintEditor::Mode::Measure:
        break;
    }
    return false;
}

// The rectangle round what is chosen; null when nothing is.
QRectF choiceBounds(const Footprint &footprint, const FootprintChoice &chosen)
{
    QList<QRectF> rects;
    if (!chosen.pads.isEmpty())
        rects.append(padsBounds(footprint, chosen.pads));
    if (!chosen.graphics.isEmpty())
        rects.append(graphicsBounds(footprint.graphics, chosen.graphics));
    if (chosen.designator)
        rects.append(placeholderRect(footprint.refAt, ComponentText::Designator));
    if (chosen.value)
        rects.append(placeholderRect(footprint.valueAt, ComponentText::Value));
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

// 'footprint' with what is chosen moved by 'delta'.
Footprint withChoiceMoved(const Footprint &footprint, const FootprintChoice &chosen,
                          const Point &delta)
{
    Footprint result = withPadsMoved(footprint, chosen.pads, delta);
    result.graphics = withGraphicsMoved(result.graphics, chosen.graphics, delta);
    if (chosen.designator)
        result.refAt = result.refAt + delta;
    if (chosen.value)
        result.valueAt = result.valueAt + delta;
    return result;
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
        const auto cosmetic = [](const QColor &color, Qt::PenStyle style = Qt::SolidLine) {
            QPen pen(color, CHOSEN_PEN_PIXELS, style);
            pen.setCosmetic(true);
            return pen;
        };
        painter->setBrush(Qt::NoBrush);
        // Pads too near each other, in red.
        painter->setPen(cosmetic(paintcolor::ercMark()));
        for (const PadGap &gap : m_editor->padGaps()) {
            for (const int i : {gap.first, gap.second}) {
                if (i < shown.pads.size())
                    painter->drawRect(padRect(shown.pads.at(i)));
            }
        }
        const FootprintChoice &chosen = m_editor->chosen();
        painter->setPen(cosmetic(paintcolor::selection()));
        for (const int i : chosen.pads) {
            if (i >= 0 && i < shown.pads.size())
                painter->drawRect(padRect(shown.pads.at(i)));
        }
        const double handle = m_editor->canvas()->pixelsToMm(HANDLE_PIXELS);
        for (const int i : chosen.graphics) {
            if (i < 0 || i >= shown.graphics.size())
                continue;
            const Graphic &graphic = shown.graphics.at(i);
            const QPolygonF outline = graphicOutline(graphic);
            if (outline.isEmpty())
                painter->drawRect(graphicRect(graphic));
            else
                painter->drawPolyline(outline);
            // The corners that can be dragged.
            for (const Point &corner : graphicCorners(graphic))
                painter->drawRect(QRectF(toMm(corner) - QPointF(handle, handle),
                                         QSizeF(2 * handle, 2 * handle)));
        }
        if (chosen.designator)
            painter->drawRect(placeholderRect(shown.refAt, ComponentText::Designator));
        if (chosen.value)
            painter->drawRect(placeholderRect(shown.valueAt, ComponentText::Value));

        const QList<Pad> floatingPads = m_editor->floatingPads();
        const QList<Graphic> floatingGraphics = m_editor->floatingGraphics();
        if (!floatingPads.isEmpty() || !floatingGraphics.isEmpty()) {
            painter->setOpacity(FLOATING_OPACITY);
            paintFootprintPads(*painter, floatingPads);
            paintFootprintGraphics(*painter, floatingGraphics);
            painter->setOpacity(1.0);
        }
        if (const std::optional<Point> origin = m_editor->floatingOrigin()) {
            painter->setPen(cosmetic(paintcolor::footprintOrigin()));
            const QPointF at = toMm(*origin);
            painter->drawLine(at - QPointF(ORIGIN_CROSS_MM, 0), at + QPointF(ORIGIN_CROSS_MM, 0));
            painter->drawLine(at - QPointF(0, ORIGIN_CROSS_MM), at + QPointF(0, ORIGIN_CROSS_MM));
        }
        if (const std::optional<QLineF> measured = m_editor->measurement()) {
            const QLineF line = *measured;
            const double pixel = m_editor->canvas()->pixelsToMm(1);
            painter->setPen(cosmetic(paintcolor::selection(), Qt::DashLine));
            painter->drawLine(line);
            painter->setPen(cosmetic(paintcolor::selection()));
            for (const QPointF &end : {line.p1(), line.p2()})
                painter->drawEllipse(end, MEASURE_END_PIXELS * pixel, MEASURE_END_PIXELS * pixel);
            const QString text =
                QStringLiteral("ΔX %1   ΔY %2   %3")
                    .arg(formatMmShort(mmToNm(line.dx())), formatMmShort(mmToNm(line.dy())),
                         formatMmShortUnit(mmToNm(line.length())));
            // Beside the far end, the same size at any zoom, on white.
            painter->save();
            painter->translate(line.p2());
            painter->scale(pixel, pixel);
            const QRectF box = painter->fontMetrics()
                                   .boundingRect(text)
                                   .translated(MEASURE_TEXT_OFFSET, -MEASURE_TEXT_OFFSET)
                                   .adjusted(-3, -2, 3, 2);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(255, 255, 255, MEASURE_BACK_ALPHA));
            painter->drawRect(box);
            painter->setPen(paintcolor::selection());
            painter->drawText(QPointF(MEASURE_TEXT_OFFSET, -MEASURE_TEXT_OFFSET), text);
            painter->restore();
        }
        // The origin, the point the part is held by and turns about, over
        // all, the same size on screen at any zoom.
        if (!m_editor->floatingOrigin()) {
            const double pixel = m_editor->canvas()->pixelsToMm(1);
            const double arm = ORIGIN_ARM_PIXELS * pixel;
            painter->setPen(cosmetic(paintcolor::footprintOrigin()));
            painter->drawLine(QPointF(-arm, 0), QPointF(arm, 0));
            painter->drawLine(QPointF(0, -arm), QPointF(0, arm));
            painter->drawEllipse(QPointF(0, 0), ORIGIN_RING_PIXELS * pixel,
                                 ORIGIN_RING_PIXELS * pixel);
        }
        const QRectF frame = m_editor->frame();
        if (!frame.isNull()) {
            QPen framePen = cosmetic(paintcolor::selection(), Qt::DashLine);
            framePen.setWidthF(1);
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
    FootprintEditor *m_editor;
    QRectF m_area;
};

} // namespace

bool operator==(const FootprintChoice &a, const FootprintChoice &b)
{
    return a.pads == b.pads && a.graphics == b.graphics && a.designator == b.designator
           && a.value == b.value;
}

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
    : QWidget(parent)
    , m_name(name)
    , m_inUserLibrary(inUserLibrary)
    , m_footprint(footprint)
    , m_nextPad(firstPad())
{
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
    m_numberEdit = new InPlaceEdit(m_canvas, this);
    m_padPanel = new PadPanel(this, this);
    m_graphicPanel = new GraphicPanel(this, this);
    m_panels = new QStackedWidget(this);
    m_panels->addWidget(m_padPanel);
    m_panels->addWidget(m_graphicPanel);

    // The strip over the canvas, the panel beside it.
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_canvas);
    splitter->addWidget(m_panels);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setChildrenCollapsible(false);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(createToolBar());
    layout->addWidget(splitter, 1);

    connect(&m_undoStack, &QUndoStack::cleanChanged, this, &FootprintEditor::titleChanged);
    connect(this, &FootprintEditor::changed, m_canvas->scene(), [this] { m_canvas->scene()->update(); });
    connect(this, &FootprintEditor::changed, this, &FootprintEditor::showPanel);
    // A short right click calls off what is being placed or drawn, as Esc.
    connect(m_canvas, &CanvasView::contextMenuRequested, this, [this] {
        if (m_mode != Mode::Select || m_dragging || m_framing)
            escape();
    });
    showPanel();
    // Once the tab is shown and the canvas knows its size.
    QTimer::singleShot(0, this, [this] { m_canvas->fitRect(footprintBounds(m_footprint)); });
}

FootprintEditor::~FootprintEditor()
{
    // The stack going last says it is clean no more, or again: nobody is to
    // hear it, the tab is gone.
    m_undoStack.disconnect();
}

QString FootprintEditor::editorTitle()
{
    return tr("Footprint editor");
}

QString FootprintEditor::padPropertiesStep()
{
    return tr("Pad properties");
}

QString FootprintEditor::drawingPropertiesStep()
{
    return tr("Drawing properties");
}

QString FootprintEditor::newFootprintTitle()
{
    return tr("New footprint");
}

bool FootprintEditor::isInUserLibrary(const QString &name)
{
    return QFileInfo::exists(footprintPath(AppSettings::userLibraryFolder(), name));
}

QWidget *FootprintEditor::createToolBar()
{
    QToolBar *bar = new QToolBar(this);
    bar->setObjectName(QStringLiteral("footprintBar"));
    bar->setMovable(false);
    styleToolBar(bar);
    const QColor color = bar->palette().color(QPalette::WindowText);

    // An action of the tab with its picture and its key in its tip; on the
    // strip when it has a picture.
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
        // The keys of the tab shown only: the others are hidden.
        result->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(result, &QAction::triggered, this, slot);
        addAction(result);
        if (icon) {
            result->setIcon(toolIcon(*icon, color));
            bar->addAction(result);
        }
        return result;
    };
    // A button with the action 'main' and the menu 'menu' under its arrow.
    const auto menuButton = [bar](QAction *main, QMenu *menu, const char *name) {
        QToolButton *button = new QToolButton(bar);
        button->setObjectName(QLatin1String(name));
        button->setDefaultAction(main);
        button->setMenu(menu);
        button->setPopupMode(QToolButton::MenuButtonPopup);
        bar->addWidget(button);
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

    // Save, its arrow offering to save under a new name.
    QAction *saveAction = action(MainWindow::saveTitle(), "actionSaveFootprint", QKeySequence::Save,
                                 std::nullopt, [this] { save(); });
    saveAction->setIcon(toolIcon(ToolIcon::Save, color));
    QAction *saveAs = action(MainWindow::saveAsTitle() + QStringLiteral("…"), "actionSaveFootprintAs",
                             QKeySequence::SaveAs, std::nullopt, [this] { saveUnderNewName(); });
    QMenu *saveMenu = new QMenu(this);
    saveMenu->addAction(saveAs);
    menuButton(saveAction, saveMenu, "saveFootprintButton");
    gap();
    QAction *undo = m_undoStack.createUndoAction(this, MainWindow::undoTitle());
    undo->setObjectName(QStringLiteral("actionUndoFootprint"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    undo->setIcon(toolIcon(ToolIcon::Undo, color));
    addAction(undo);
    bar->addAction(undo);
    QAction *redo = m_undoStack.createRedoAction(this, MainWindow::redoTitle());
    redo->setObjectName(QStringLiteral("actionRedoFootprint"));
    redo->setShortcut(QKeySequence::Redo);
    redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
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
    QAction *pad = action(PadPanel::padTitle(), "actionPadTool", QKeySequence(Qt::Key_P),
                          std::nullopt, [this] { setMode(Mode::Pad); });
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
    menuButton(pad, padMenu, "padButton");
    QAction *row = action(tr("Row of pads"), "actionPadRow", QKeySequence(), ToolIcon::PadRow,
                          [this] { askRow(); });
    m_modeActions.insert(int(Mode::Row), row);
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
        {Mode::Rect, tr("Rectangle"), "actionDrawRect", QKeySequence(), ToolIcon::DrawRect},
        {Mode::Circle, tr("Circle"), "actionDrawCircle", QKeySequence(), ToolIcon::DrawCircle},
        {Mode::Arc, tr("Arc"), "actionDrawArc", QKeySequence(), ToolIcon::DrawArc},
        {Mode::Polygon, tr("Polygon"), "actionDrawPolygon", QKeySequence(), ToolIcon::DrawPolygon},
        {Mode::Text, tr("Text"), "actionDrawText", QKeySequence(Qt::Key_T), ToolIcon::DrawText},
    };
    for (const DrawingMode &mode : drawing) {
        m_modeActions.insert(int(mode.mode),
                             action(mode.text, mode.name, mode.key, mode.icon,
                                    [this, m = mode.mode] { setMode(m); }));
    }
    m_modeActions.insert(int(Mode::Measure),
                         action(tr("Measure"), "actionMeasure", QKeySequence(Qt::Key_M),
                                ToolIcon::Measure, [this] { setMode(Mode::Measure); }));
    gap();
    QAction *origin = action(tr("Origin here"), "actionOriginHere", QKeySequence(Qt::Key_O),
                             std::nullopt, [this] { setMode(Mode::Origin); });
    origin->setIcon(toolIcon(ToolIcon::Origin, color));
    m_modeActions.insert(int(Mode::Origin), origin);
    QMenu *originMenu = new QMenu(this);
    QAction *middle = originMenu->addAction(tr("Origin in the middle"));
    middle->setObjectName(QStringLiteral("actionOriginMiddle"));
    connect(middle, &QAction::triggered, this, &FootprintEditor::originAtMiddle);
    menuButton(origin, originMenu, "originButton");
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
    action(SheetEditor::actionName(SheetEditor::Action::Rotate), "actionRotateFootprint",
           QKeySequence(Qt::Key_R), std::nullopt, [this] { rotate(); });
    action(SheetEditor::actionName(SheetEditor::Action::Delete), "actionDeleteFootprint",
           QKeySequence::Delete, std::nullopt, [this] { deleteChosen(); });
    action(MainWindow::selectAllTitle(), "actionChooseAllFootprint", QKeySequence::SelectAll,
           std::nullopt, [this] { chooseAll(); });
    updateModeButtons();
    return bar;
}

QString FootprintEditor::tabTitle() const
{
    const QString title = m_name.isEmpty() ? newFootprintTitle() : m_name;
    return m_undoStack.isClean() ? title : title + QLatin1Char('*');
}

QString FootprintEditor::tabTip() const
{
    QString tip = editorTitle() + QStringLiteral(": ")
                  + (m_name.isEmpty() ? newFootprintTitle() : m_name);
    if (!m_name.isEmpty() && !m_inUserLibrary)
        tip += QStringLiteral(" (") + tr("built-in") + QLatin1Char(')');
    return tip;
}

void FootprintEditor::updateModeButtons()
{
    for (auto it = m_modeActions.constBegin(); it != m_modeActions.constEnd(); ++it)
        it.value()->setChecked(it.key() == int(m_mode));
}

void FootprintEditor::showPanel()
{
    const bool drawings = isDrawingMode(m_mode)
                          || (!m_chosen.graphics.isEmpty() && m_chosen.pads.isEmpty());
    m_panels->setCurrentWidget(drawings ? static_cast<QWidget *>(m_graphicPanel) : m_padPanel);
    m_padPanel->refresh();
    m_graphicPanel->refresh();
}

void FootprintEditor::showStatus(const QString &text)
{
    m_status = text;
    emit statusChanged(text);
}

Point FootprintEditor::snapped(const QPointF &at) const
{
    return m_canvas->snapped(at);
}

void FootprintEditor::setClearance(Coord clearance)
{
    if (clearance == m_clearance)
        return;
    m_clearance = clearance;
    checkGaps();
    emit changed();
}

void FootprintEditor::checkGaps()
{
    const QList<PadGap> gaps =
        m_clearance > 0 ? padsTooNear(m_footprint, m_clearance) : QList<PadGap>();
    const bool news = gaps != m_gaps;
    m_gaps = gaps;
    if (!news || gaps.isEmpty())
        return;
    const PadGap &first = gaps.first();
    const QString number = m_footprint.pads.at(first.first).number;
    const QString other = m_footprint.pads.at(first.second).number;
    showStatus(tr("Pads %1 and %2 are %3 apart, less than the clearance %4")
                   .arg(number, other, formatMmShortUnit(first.gap),
                        formatMmShortUnit(m_clearance)));
}

void FootprintEditor::setMode(Mode mode)
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

void FootprintEditor::setChosen(const FootprintChoice &chosen)
{
    FootprintChoice valid;
    for (const int i : chosen.pads) {
        if (i >= 0 && i < m_footprint.pads.size())
            valid.pads.insert(i);
    }
    for (const int i : chosen.graphics) {
        if (i >= 0 && i < m_footprint.graphics.size())
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

void FootprintEditor::setNextPad(const Pad &pad)
{
    m_nextPad = pad;
    m_nextPad.number.clear();
    m_nextPad.at = Point{};
    emit changed();
}

void FootprintEditor::setNextGraphic(const Graphic &graphic)
{
    m_nextGraphic.width = graphic.width;
    m_nextGraphic.dashed = graphic.dashed;
    emit changed();
}

void FootprintEditor::change(const QString &text, const Footprint &changed,
                             const FootprintChoice &chosen)
{
    if (changed == m_footprint) {
        setChosen(chosen);
        return;
    }
    m_undoStack.push(new FootprintStep(this, text, m_footprint, changed));
    setChosen(chosen);
    // The same pads chosen, moved: told again.
    describeChoice();
}

void FootprintEditor::applyStep(const Footprint &footprint)
{
    m_footprint = footprint;
    m_dragging = false;
    m_chosen = FootprintChoice();
    checkGaps();
    emit changed();
}

Footprint FootprintEditor::shownFootprint() const
{
    if (!m_dragging || m_dragDelta == Point{})
        return m_footprint;
    if (m_dragGraphic >= 0) {
        Footprint result = m_footprint;
        result.graphics[m_dragGraphic] = withCornerMoved(
            m_footprint.graphics.at(m_dragGraphic), m_dragCorner, m_dragAnchor + m_dragDelta);
        return result;
    }
    return withChoiceMoved(m_footprint, m_chosen, m_dragDelta);
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

Point FootprintEditor::drawPoint(const Point &cursor) const
{
    const bool straight = m_mode == Mode::Line || m_mode == Mode::Polygon;
    if (!straight || m_free || m_drawPoints.isEmpty())
        return cursor;
    return keptStraightOrDiagonal(m_drawPoints.last(), cursor);
}

QList<Graphic> FootprintEditor::floatingGraphics() const
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
        graphics.append(rectBetween(first, at));
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
    case Mode::Pad:
    case Mode::Row:
    case Mode::Origin:
    case Mode::Text:
    case Mode::Measure:
        break;
    }
    return withLineOf(graphics, m_nextGraphic);
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

Point FootprintEditor::measurePoint(const QPointF &at) const
{
    const int pad = padAtPoint(m_footprint, at, m_canvas->pixelsToMm(CanvasView::HIT_PIXELS));
    return pad >= 0 ? m_footprint.pads.at(pad).at : snapped(at);
}

std::optional<QLineF> FootprintEditor::measurement() const
{
    if (m_mode != Mode::Measure || !m_measureFrom)
        return std::nullopt;
    const Point to = m_measureTo ? *m_measureTo : measurePoint(m_hoverAt);
    return QLineF(toMm(*m_measureFrom), toMm(to));
}

void FootprintEditor::describeChoice()
{
    if (m_chosen.pads.size() != 2 || !m_chosen.graphics.isEmpty())
        return;
    QList<int> two(m_chosen.pads.cbegin(), m_chosen.pads.cend());
    std::sort(two.begin(), two.end());
    const Pad &a = m_footprint.pads.at(two.first());
    const Pad &b = m_footprint.pads.at(two.last());
    const Coord centres = mmToNm(QLineF(toMm(a.at), toMm(b.at)).length());
    showStatus(tr("Pads %1 and %2: %3 between the copper, %4 between the centres")
                   .arg(a.number, b.number, formatMmShortUnit(padsApart(a, b)),
                        formatMmShortUnit(centres)));
}

void FootprintEditor::addGraphic(const QString &text, Graphic graphic)
{
    if (graphic.kind != GraphicKind::Text) {
        graphic.width = m_nextGraphic.width;
        graphic.dashed = m_nextGraphic.dashed;
    }
    Footprint changed = m_footprint;
    changed.graphics.append(graphic);
    FootprintChoice chosen;
    chosen.graphics = {int(changed.graphics.size()) - 1};
    change(text, changed, chosen);
}

void FootprintEditor::drawClick(const Point &cursor)
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
            addGraphic(step, rectBetween(m_drawPoints.first(), at));
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
    case Mode::Pad:
    case Mode::Row:
    case Mode::Origin:
    case Mode::Measure:
        break;
    }
    emit changed();
}

void FootprintEditor::finishDrawing()
{
    if (m_mode == Mode::Polygon && m_drawPoints.size() >= 3)
        addGraphic(tr("Draw"), polygonOf(m_drawPoints));
    m_drawPoints.clear();
    emit changed();
}

void FootprintEditor::press(const QPointF &at, bool adding, bool free)
{
    m_numberEdit->close(true);
    m_pressAt = at;
    m_free = free;
    m_cursor = snapped(at);
    m_cursorKnown = true;
    switch (m_mode) {
    case Mode::Pad: {
        Pad pad = m_nextPad;
        pad.at = m_cursor;
        pad.number = nextPadNumber(m_footprint);
        FootprintChoice placed;
        placed.pads = {int(m_footprint.pads.size())};
        change(tr("Place pad"), withPadsAdded(m_footprint, {pad}), placed);
        return;
    }
    case Mode::Row: {
        const int first = int(m_footprint.pads.size());
        FootprintChoice placed;
        for (int k = 0; k < m_rowPads.size(); ++k)
            placed.pads.insert(first + k);
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
    // A corner of a drawing is dragged alone.
    const double cornerReach = m_canvas->pixelsToMm(CanvasView::END_PIXELS);
    for (int i = int(m_footprint.graphics.size()) - 1; i >= 0 && !adding; --i) {
        const int corner = cornerAt(m_footprint.graphics.at(i), at, cornerReach);
        if (corner < 0)
            continue;
        FootprintChoice alone;
        alone.graphics = {i};
        setChosen(alone);
        m_dragging = true;
        m_dragGraphic = i;
        m_dragCorner = corner;
        m_dragAnchor = graphicCorners(m_footprint.graphics.at(i)).at(corner);
        m_dragDelta = Point{};
        return;
    }
    // What is under the press: the drawings over the pads, then the pads,
    // then the places of the texts, which stand on the origin at first.
    FootprintChoice hit;
    Point anchor;
    const int graphic = graphicAt(m_footprint.graphics, at, reach);
    const int pad = padAtPoint(m_footprint, at, reach);
    if (graphic >= 0) {
        hit.graphics = {graphic};
        const Graphic &g = m_footprint.graphics.at(graphic);
        anchor = g.kind == GraphicKind::Polygon && !g.points.isEmpty() ? g.points.first() : g.a;
    } else if (pad >= 0) {
        hit.pads = {pad};
        anchor = m_footprint.pads.at(pad).at;
    } else if (placeholderRect(m_footprint.refAt, ComponentText::Designator).contains(at)) {
        hit.designator = true;
        anchor = m_footprint.refAt;
    } else if (placeholderRect(m_footprint.valueAt, ComponentText::Value).contains(at)) {
        hit.value = true;
        anchor = m_footprint.valueAt;
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
    const bool already = m_chosen.pads.contains(hit.pads) && m_chosen.graphics.contains(hit.graphics)
                         && (!hit.designator || m_chosen.designator)
                         && (!hit.value || m_chosen.value);
    if (adding) {
        FootprintChoice chosen = m_chosen;
        if (already) {
            chosen.pads.subtract(hit.pads);
            chosen.graphics.subtract(hit.graphics);
            chosen.designator = chosen.designator && !hit.designator;
            chosen.value = chosen.value && !hit.value;
        } else {
            chosen.pads.unite(hit.pads);
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

void FootprintEditor::move(const QPointF &at, bool free)
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
        const QRectF rect = QRectF(m_frameFrom, m_frameTo).normalized();
        FootprintChoice chosen = m_frameAdding ? m_chosen : FootprintChoice();
        chosen.pads.unite(padsInside(m_footprint, rect));
        chosen.graphics.unite(graphicsInside(m_footprint.graphics, rect));
        chosen.designator = chosen.designator
                            || rect.contains(placeholderRect(m_footprint.refAt,
                                                             ComponentText::Designator));
        chosen.value = chosen.value
                       || rect.contains(placeholderRect(m_footprint.valueAt, ComponentText::Value));
        setChosen(chosen);
        emit changed();
        return;
    }
    if (!m_dragging)
        return;
    const Footprint moved = shownFootprint();
    const bool dragged = m_dragDelta != Point{};
    m_dragging = false;
    m_dragDelta = Point{};
    m_dragGraphic = -1;
    m_dragCorner = -1;
    if (dragged) {
        change(SheetEditor::actionName(SheetEditor::Action::Move), moved, m_chosen);
    } else if (m_pressAlone) {
        // A click on one of several chosen chooses it alone.
        setChosen(*m_pressAlone);
    }
    m_pressAlone.reset();
    emit changed();
}

void FootprintEditor::doubleClick(const QPointF &at)
{
    switch (m_mode) {
    case Mode::Line:
    case Mode::Polygon:
        // The first click of the two put its point down already.
        finishDrawing();
        return;
    case Mode::Select:
        break;
    case Mode::Pad:
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
    const int graphic = graphicAt(m_footprint.graphics, at, reach);
    if (graphic >= 0 && m_footprint.graphics.at(graphic).kind == GraphicKind::Text) {
        editText(m_footprint.graphics.at(graphic).a, graphic);
        return;
    }
    const int pad = padAtPoint(m_footprint, at, reach);
    if (pad >= 0)
        editNumber(pad);
}

void FootprintEditor::editNumber(int pad)
{
    FootprintChoice alone;
    alone.pads = {pad};
    setChosen(alone);
    m_dragging = false;
    // The edit is tied to this pad as the footprint stands now: a step made
    // meanwhile closes it first (press() does), so the index holds.
    disconnect(m_numberEdit, &InPlaceEdit::accepted, this, nullptr);
    connect(m_numberEdit, &InPlaceEdit::accepted, this, [this, pad](const QString &text) {
        if (pad >= m_footprint.pads.size())
            return;
        Pad changed = m_footprint.pads.at(pad);
        changed.number = text.trimmed();
        FootprintChoice chosen;
        chosen.pads = {pad};
        change(padPropertiesStep(), withPadsReplaced(m_footprint, {pad}, {changed}), chosen);
    });
    m_numberEdit->open(m_footprint.pads.at(pad).number, padRect(m_footprint.pads.at(pad)));
}

void FootprintEditor::editText(const Point &at, int graphic)
{
    m_dragging = false;
    disconnect(m_numberEdit, &InPlaceEdit::accepted, this, nullptr);
    connect(m_numberEdit, &InPlaceEdit::accepted, this, [this, at, graphic](const QString &typed) {
        const QString text = typed.trimmed();
        // An empty text is none: a text given none keeps its own.
        if (text.isEmpty())
            return;
        if (graphic < 0) {
            addGraphic(tr("Draw"), textAt(at, text));
            return;
        }
        if (graphic >= m_footprint.graphics.size())
            return;
        Footprint changed = m_footprint;
        changed.graphics[graphic].text = text;
        FootprintChoice chosen;
        chosen.graphics = {graphic};
        change(drawingPropertiesStep(), changed, chosen);
    });
    const QString text = graphic >= 0 ? m_footprint.graphics.at(graphic).text : QString();
    const QRectF rect = graphic >= 0
                            ? graphicRect(m_footprint.graphics.at(graphic))
                            : QRectF(toMm(at) - QPointF(NEW_TEXT_WIDTH_MM / 2, nmToMm(DEFAULT_TEXT_HEIGHT)),
                                     QSizeF(NEW_TEXT_WIDTH_MM, 2 * nmToMm(DEFAULT_TEXT_HEIGHT)));
    m_numberEdit->open(text, rect);
}

void FootprintEditor::hover(const QPointF &at, bool free)
{
    m_hoverAt = at;
    const Point cursor = snapped(at);
    if (m_cursorKnown && cursor == m_cursor && free == m_free)
        return;
    m_cursor = cursor;
    m_cursorKnown = true;
    m_free = free;
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
    const Point center = snapped(choiceBounds(m_footprint, m_chosen).center());
    Footprint changed = withPadsRotated(m_footprint, m_chosen.pads, center);
    changed.graphics = withGraphicsRotated(changed.graphics, m_chosen.graphics, center);
    const auto turned = [&center](const Point &point) {
        const Point offset = point - center;
        return center + Point{-offset.y, offset.x};
    };
    if (m_chosen.designator)
        changed.refAt = turned(changed.refAt);
    if (m_chosen.value)
        changed.valueAt = turned(changed.valueAt);
    change(SheetEditor::actionName(SheetEditor::Action::Rotate), changed, m_chosen);
}

void FootprintEditor::deleteChosen()
{
    if ((m_chosen.pads.isEmpty() && m_chosen.graphics.isEmpty()) || m_dragging)
        return;
    Footprint changed = withoutPads(m_footprint, m_chosen.pads);
    changed.graphics = withoutGraphics(changed.graphics, m_chosen.graphics);
    change(SheetEditor::actionName(SheetEditor::Action::Delete), changed, {});
}

void FootprintEditor::chooseAll()
{
    FootprintChoice all;
    for (int i = 0; i < m_footprint.pads.size(); ++i)
        all.pads.insert(i);
    for (int i = 0; i < m_footprint.graphics.size(); ++i)
        all.graphics.insert(i);
    setMode(Mode::Select);
    setChosen(all);
}

void FootprintEditor::escape()
{
    if (m_dragging || m_framing) {
        m_dragging = false;
        m_framing = false;
        m_dragDelta = Point{};
        m_dragGraphic = -1;
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
    const QString name = askName(MainWindow::saveTitle().remove(QLatin1Char('&')));
    return !name.isEmpty() && saveAs(name, true);
}

bool FootprintEditor::saveUnderNewName()
{
    const QString name = askName(MainWindow::saveAsTitle().remove(QLatin1Char('&')));
    return !name.isEmpty() && saveAs(name, true);
}

QString FootprintEditor::askName(const QString &title)
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, title, tr("Footprint name") + QLatin1Char(':'),
                              QLineEdit::Normal, m_name, &ok)
            .trimmed();
    if (!ok || name.isEmpty())
        return QString();
    if (!nameRule().match(name).hasMatch()) {
        showStatus(tr("A name may hold Latin letters, digits and these signs")
                   + QStringLiteral(": . _ + -"));
        return QString();
    }
    // Its own name it keeps without asking.
    const bool taken = name != m_name
                       && (QFileInfo::exists(footprintPath(AppSettings::userLibraryFolder(), name))
                           || QFileInfo::exists(
                               footprintPath(AppSettings::builtInLibraryFolder(), name)));
    if (taken
        && QMessageBox::question(this, title,
                                 tr("The library has a footprint %1 already. Replace it").arg(name)
                                     + QLatin1Char('?'))
               != QMessageBox::Yes)
        return QString();
    return name;
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
    emit titleChanged();
    showStatus(writtenMessage(path));
    emit saved(name);
    return true;
}

bool FootprintEditor::confirmClose()
{
    if (m_undoStack.isClean())
        return true;
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, editorTitle(),
        tr("The footprint has changes that are not saved. Save them") + QLatin1Char('?'),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save());
}

} // namespace ppcb
