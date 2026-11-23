#pragma once

#include <QColor>
#include <QIcon>

namespace ppcb {

// The pictures of the buttons on the strip of tools over the canvas, and of
// the menu items they stand for.
enum class ToolIcon {
    Menu,
    Recent,
    ProjectProperties,
    Save,
    Print,
    Pdf,
    Undo,
    Redo,
    Select,
    Wire,
    Bus,
    Label,
    Power,
    NoConnect,
    Outline,
    CutOut,
    RectangleBoard,
    Hole,
    Track,
    UpdateBoard,
    Lock,
    Rotate,
    Mirror,
    Delete,
    ZoomIn,
    ZoomOut,
    ZoomFit,
    ZoomFitDrawing,
    ActualSize,
    Library,
    Properties,
    Nets,
    Erc,
    Bom,
};

// 'icon' drawn in lines of 'color' on a square of TOOL_ICON_SIZE pixels,
// sharp on screens of twice the pixels too.
QIcon toolIcon(ToolIcon icon, const QColor &color);
// The layer button: two sheets, 'front' over 'back', framed in 'color'.
QIcon layerIcon(const QColor &front, const QColor &back, const QColor &color);
constexpr int TOOL_ICON_SIZE = 24;
// A bold question mark of 'color' for a question box, 'size' pixels high,
// sharp on a screen of 'ratio' pixels to one.
QPixmap questionPixmap(const QColor &color, int size, qreal ratio);

} // namespace ppcb
