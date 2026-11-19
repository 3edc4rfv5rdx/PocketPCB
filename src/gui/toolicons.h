#pragma once

#include <QColor>
#include <QIcon>

namespace ppcb {

// The pictures of the buttons on the strip of tools left of the canvas.
enum class ToolIcon {
    Select,
    Wire,
    Bus,
    Label,
    Power,
    NoConnect,
    Rotate,
    Mirror,
    Delete,
    ZoomIn,
    ZoomOut,
    ZoomFit,
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
