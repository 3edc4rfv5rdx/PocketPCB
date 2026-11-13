#pragma once

#include "libraryfile.h"

#include <QGraphicsItem>

namespace ppcb {

// Shows one symbol or footprint of the library on the canvas, at the scene
// origin. A part has no drawing of its own and shows nothing.
class LibraryViewItem : public QGraphicsItem
{
public:
    explicit LibraryViewItem(const LibraryItem &content);

    const LibraryItem &content() const { return m_content; }
    void setContent(const LibraryItem &content);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    LibraryItem m_content;
};

} // namespace ppcb
