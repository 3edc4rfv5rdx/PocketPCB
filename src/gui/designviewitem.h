#pragma once

#include "document.h"

#include <QGraphicsItem>

namespace ppcb {

// Shows one page of the open project on the canvas. The pages are the
// sheets of the schematic in their order, then the board.
class BoardEditor;
class SheetEditor;

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
    // What a view fitted to the drawing should hold, at least
    // FIT_DRAWING_MIN_SIZE across: everything placed on a sheet, or its
    // paper when nothing is; what is laid on the board, without its
    // outline (layoutBounds()).
    QRectF drawingBounds() const;

    // Sheets are edited through 'editor': the item hands it the left mouse
    // button and the hovering mouse, and draws what it holds (the
    // selection, a drag, a rectangle, the net under the mouse).
    // The editor must outlive the item.
    void setEditor(SheetEditor *editor);
    // The board is edited through 'editor' the same way: the outline as it
    // is dragged or drawn, its corners and what is lit. It must outlive the
    // item.
    void setBoardEditor(BoardEditor *editor);
    // The layers of the board drawn; all at first.
    BoardLayerSet boardLayers() const { return m_boardLayers; }
    void setBoardLayers(const BoardLayerSet &layers);
    // Whether every line of the ratsnest is drawn bold, with a dot at each
    // end, so that what is still to be joined stands out.
    void setRatsnestBright(bool bright);
    // Whether the lines of the ratsnest are gentle arcs, all bent the same
    // way, so that lines from one pad part.
    void setRatsnestArcs(bool arcs);
    // Whether the coordinates of the corner being drawn on the board are
    // shown beside it.
    void setDrawingCoordinatesShown(bool shown);
    // Whether the problems of the rules check are ringed on the sheet: while
    // their panel is open.
    void setErcMarksShown(bool shown);

    QRectF boundingRect() const override { return m_area; }
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent *event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override;

private:
    // The editor, when it edits the page shown.
    SheetEditor *activeEditor() const;
    BoardEditor *activeBoardEditor() const;
    // The board as the board editor shows it, with what it draws over it.
    void paintBoardPage(QPainter &painter) const;
    // The net lit, under the mouse or chosen in the table of nets: a band
    // along its wires, its labels and power symbols filled in, a dot on its
    // pins.
    void paintLitNet(QPainter &painter, const Sheet &sheet) const;
    void paintSelection(QPainter &painter, const Sheet &sheet) const;

    const Document *m_document;
    SheetEditor *m_editor = nullptr;
    BoardEditor *m_boardEditor = nullptr;
    BoardLayerSet m_boardLayers = BoardLayerSet::all();
    bool m_ratsnestBright = false;
    bool m_ratsnestArcs = false;
    bool m_drawingCoordinatesShown = true;
    QRectF m_area;
    int m_page = 0;
    bool m_ercMarksShown = false;
};

} // namespace ppcb
