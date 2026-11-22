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
constexpr int TOOL_ICON_SIZE = 24;

} // namespace ppcb
