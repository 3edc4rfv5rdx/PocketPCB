#include "designviewitem.h"

#include "paint.h"
#include "sheeteditor.h"
#include "wiring.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>

namespace ppcb {

namespace {

// The band drawn along a selected wire or bus, in millimetres.
const double SELECTED_LINE_WIDTH = 0.8;
// How opaque the net under the mouse is drawn over the sheet, of 255.
const int HOVERED_NET_ALPHA = 110;
// The radius of the dot on a pin of the net under the mouse, in millimetres.
const double HOVERED_PIN_RADIUS = 0.6;

} // namespace

DesignViewItem::DesignViewItem(const Document *document, const QRectF &area)
    : m_document(document)
    , m_area(area)
{
}

int DesignViewItem::pageCount() const
{
    return int(m_document->design().schematic.sheets.size()) + 1;
}

void DesignViewItem::setPage(int page)
{
    m_page = qBound(0, page, pageCount() - 1);
    update();
}

void DesignViewItem::contentChanged()
{
    setPage(m_page);
}

QRectF DesignViewItem::pageBounds() const
{
    const Design &design = m_document->design();
    if (m_page == boardPage())
        return boardBounds(design.board, design.library);
    return paperRect(design.schematic.sheets.at(m_page).paper);
}

void DesignViewItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                           QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    const Design &design = m_document->design();
    if (m_page == boardPage()) {
        paintBoard(*painter, design.board, design.library);
        return;
    }
    const SheetEditor *editor = activeEditor();
    const Sheet *preview = editor == nullptr ? nullptr : editor->preview();
    if (preview == nullptr) {
        paintSheet(*painter, design.schematic, design.library, m_page, design.project.titleBlock);
        paintHoveredNet(*painter, design.schematic.sheets.at(m_page));
        paintSelection(*painter, design.schematic.sheets.at(m_page));
        return;
    }
    // A drag or a placing in progress: the sheet as it would be.
    Schematic schematic = design.schematic;
    schematic.sheets[m_page] = *preview;
    const Library *library = editor->previewLibrary();
    paintSheet(*painter, schematic, library != nullptr ? *library : design.library, m_page,
               design.project.titleBlock);
    paintSelection(*painter, *preview);
}

void DesignViewItem::setEditor(SheetEditor *editor)
{
    m_editor = editor;
    setAcceptedMouseButtons(editor == nullptr ? Qt::NoButton : Qt::LeftButton);
    setAcceptHoverEvents(editor != nullptr);
}

SheetEditor *DesignViewItem::activeEditor() const
{
    if (m_editor == nullptr || m_editor->sheet() != m_page || m_page == boardPage())
        return nullptr;
    return m_editor;
}

void DesignViewItem::paintHoveredNet(QPainter &painter, const Sheet &sheet) const
{
    const SheetEditor *editor = activeEditor();
    const Net *net = editor == nullptr ? nullptr : editor->hoveredNet();
    if (net == nullptr)
        return;
    const Library &library = m_document->design().library;
    const QSet<ObjectId> drawn = net->objects.value(m_page);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor color = paintcolor::hoveredNet();
    color.setAlpha(HOVERED_NET_ALPHA);
    const QPen band(color, SELECTED_LINE_WIDTH, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    for (const SheetObjectShape &shape : sheetObjectShapes(sheet, library)) {
        if (!drawn.contains(shape.id))
            continue;
        if (shape.polyline.isEmpty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawRect(shape.rect);
        } else {
            painter.setPen(band);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(shape.polyline);
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    for (const SheetPin &pin : sheetPins(sheet, library)) {
        if (netHasPin(*net, pin.owner, pin.number))
            painter.drawEllipse(toMm(pin.at), HOVERED_PIN_RADIUS, HOVERED_PIN_RADIUS);
    }
    painter.restore();
}

void DesignViewItem::paintSelection(QPainter &painter, const Sheet &sheet) const
{
    const SheetEditor *editor = activeEditor();
    if (editor == nullptr)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor fill = paintcolor::selection();
    fill.setAlpha(40);
    // A line one pixel wide at any zoom.
    QPen pen(paintcolor::selection(), 0);
    // A wire or a bus, or a segment of a wire: a wide band along it.
    QColor bandColor = paintcolor::selection();
    bandColor.setAlpha(110);
    const QPen band(bandColor, SELECTED_LINE_WIDTH, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    const QList<SheetObjectShape> shapes = sheetObjectShapes(sheet, m_document->design().library);
    for (const SheetObjectShape &shape : shapes) {
        if (!editor->selection().contains(shape.id))
            continue;
        if (shape.polyline.isEmpty()) {
            painter.setPen(pen);
            painter.setBrush(fill);
            painter.drawRect(shape.rect);
        } else {
            painter.setPen(band);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(shape.polyline);
        }
    }
    const QLineF segment = editor->chosenSegment();
    if (!segment.isNull()) {
        painter.setPen(band);
        painter.drawLine(segment);
    }
    const QRectF text = editor->chosenText();
    if (!text.isEmpty()) {
        painter.setPen(pen);
        painter.setBrush(fill);
        painter.drawRect(text);
    }
    const QLineF guide = editor->textGuide();
    if (!guide.isNull()) {
        painter.setPen(pen);
        painter.drawLine(guide);
    }
    const QRectF rubber = editor->rubberBand();
    if (!rubber.isEmpty()) {
        painter.setPen(QPen(paintcolor::selection(), 0, Qt::DashLine));
        painter.setBrush(fill);
        painter.drawRect(rubber);
    }
    painter.restore();
}

void DesignViewItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    SheetEditor *editor = activeEditor();
    if (editor == nullptr || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    editor->press(event->scenePos(), event->modifiers().testFlag(Qt::ControlModifier),
                  event->modifiers().testFlag(Qt::ShiftModifier));
    event->accept();
}

void DesignViewItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event)
{
    SheetEditor *editor = activeEditor();
    if (editor == nullptr || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    editor->doubleClick(event->scenePos(), event->modifiers().testFlag(Qt::ControlModifier));
    event->accept();
}

void DesignViewItem::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    hoverMoveEvent(event);
}

void DesignViewItem::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    if (SheetEditor *editor = activeEditor())
        editor->hoverNet(event->scenePos());
}

void DesignViewItem::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    Q_UNUSED(event);
    if (m_editor != nullptr)
        m_editor->clearHoveredNet();
}

void DesignViewItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (SheetEditor *editor = activeEditor())
        editor->move(event->scenePos(), event->modifiers().testFlag(Qt::ShiftModifier));
}

void DesignViewItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (SheetEditor *editor = activeEditor())
        editor->release(event->scenePos(), event->modifiers().testFlag(Qt::ShiftModifier));
}

} // namespace ppcb
