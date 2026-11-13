#include "libraryviewitem.h"

#include "paint.h"

namespace ppcb {

namespace {

// Room around the drawing, in millimetres, so nothing touches the edge.
const double MARGIN = 2.0;
// Half-size of the area kept for a part, which draws nothing.
const double EMPTY_HALF_SIZE = 5.0;

} // namespace

LibraryViewItem::LibraryViewItem(const LibraryItem &content)
    : m_content(content)
{
}

void LibraryViewItem::setContent(const LibraryItem &content)
{
    prepareGeometryChange();
    m_content = content;
    update();
}

QRectF LibraryViewItem::boundingRect() const
{
    QRectF bounds(-EMPTY_HALF_SIZE, -EMPTY_HALF_SIZE, 2 * EMPTY_HALF_SIZE, 2 * EMPTY_HALF_SIZE);
    if (m_content.kind == LibraryKind::Symbol)
        bounds = symbolBounds(m_content.symbol);
    else if (m_content.kind == LibraryKind::Footprint)
        bounds = footprintBounds(m_content.footprint);
    return bounds.adjusted(-MARGIN, -MARGIN, MARGIN, MARGIN);
}

void LibraryViewItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                            QWidget *widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);
    if (m_content.kind == LibraryKind::Symbol)
        paintSymbol(*painter, m_content.symbol);
    else if (m_content.kind == LibraryKind::Footprint)
        paintFootprint(*painter, m_content.footprint);
}

} // namespace ppcb
