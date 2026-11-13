#pragma once

#include "document.h"

#include <QGraphicsItem>

namespace ppcb {

// Shows one page of the open project on the canvas. The pages are the
// sheets of the schematic in their order, then the board.
class DesignViewItem : public QGraphicsItem
{
public:
    // The document must outlive the item. 'area' is the part of the scene
    // the item may draw on: a page has no edge of its own, so the item
    // takes all of it.
    DesignViewItem(const Document *document, const QRectF &area);

    int pageCount() const;
    int boardPage() const { return pageCount() - 1; }
    int page() const { return m_page; }
    // A page out of range is taken as the nearest one.
    void setPage(int page);
    // Call after the document's content has changed: the shown page may be
    // gone, and the drawing is stale.
    void contentChanged();

    // What a view fitted to the shown page should hold: the paper of a
    // sheet, or everything on the board.
    QRectF pageBounds() const;

    QRectF boundingRect() const override { return m_area; }
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    const Document *m_document;
    QRectF m_area;
    int m_page = 0;
};

} // namespace ppcb
