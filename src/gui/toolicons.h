#pragma once

#include <QColor>
#include <QIcon>

class QToolBar;

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
    Autoroute,
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
    BrightGrid,
    Library,
    Properties,
    Nets,
    Erc,
    Drc,
    Bom,
    Versions,
    FootprintEditor,
    Pad,
    PadRow,
    Origin,
    DrawLine,
    DrawRect,
    DrawCircle,
    DrawArc,
    DrawPolygon,
    DrawText,
    Measure,
    Pin,
};

// 'icon' drawn in lines of 'color' on a square of TOOL_ICON_SIZE pixels,
// sharp on screens of twice the pixels too.
QIcon toolIcon(ToolIcon icon, const QColor &color);
// The layer button: two sheets, 'front' over 'back', framed in 'color'.
QIcon layerIcon(const QColor &front, const QColor &back, const QColor &color);
constexpr int TOOL_ICON_SIZE = 24;
// Dresses 'bar' as a strip of tools over a canvas: pictures alone, of
// TOOL_ICON_SIZE, the buttons lit under the mouse and framed while pressed.
void styleToolBar(QToolBar *bar);
// The style sheet that gives the buttons inside widgets of the class
// 'scope' ("QDockWidget"), or all buttons when it is empty, the one grey
// blue of the panels, to stand out from the lists round them.
QString panelButtonStyle(const QString &scope);
// A bold question mark of 'color' for a question box, 'size' pixels high,
// sharp on a screen of 'ratio' pixels to one.
QPixmap questionPixmap(const QColor &color, int size, qreal ratio);

} // namespace ppcb
