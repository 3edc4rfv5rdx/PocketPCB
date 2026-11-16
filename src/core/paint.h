#pragma once

#include "project.h"

#include <QColor>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>

class QPainter;

namespace ppcb {

// Drawing of library objects, of a schematic sheet and of the board. The
// painter works in millimetres, the unit of the canvas scene; the model's
// nanometres are converted here.

// Every colour of the drawing, in one place.
namespace paintcolor {
inline QColor background() { return QColor(255, 255, 255); }
inline QColor symbol() { return QColor(132, 0, 0); }
inline QColor pinText() { return QColor(0, 100, 100); }
// "REF" and "VALUE" marks and the origin cross.
inline QColor placeholder() { return QColor(140, 140, 140); }
inline QColor silkscreen() { return QColor(0, 132, 132); }
inline QColor padBoth() { return QColor(190, 150, 0); }
inline QColor padFront() { return QColor(200, 50, 50); }
inline QColor padBack() { return QColor(50, 90, 200); }
inline QColor padText() { return QColor(255, 255, 255); }
// The schematic.
inline QColor wire() { return QColor(0, 132, 0); }
inline QColor bus() { return QColor(0, 0, 160); }
inline QColor label() { return QColor(0, 100, 100); }
// The plate under the text of a label that lies on no wire and no pin, and
// so joins nothing; the text on it has the background's colour.
inline QColor looseLabel() { return QColor(220, 0, 0); }
inline QColor noConnect() { return QColor(0, 0, 160); }
// A no-connect mark on no pin of a part.
inline QColor looseNoConnect() { return QColor(220, 0, 0); }
inline QColor sheetGraphic() { return QColor(70, 70, 70); }
inline QColor sheetFrame() { return QColor(90, 90, 90); }
// The board. Copper has the colours of the pads: padFront() on the top
// side, padBack() on the bottom, padBoth() for what goes through.
inline QColor silkscreenBottom() { return QColor(132, 0, 132); }
inline QColor boardOutline() { return QColor(60, 60, 60); }
inline QColor bareHole() { return QColor(120, 120, 120); }
// What stands in for a symbol or a footprint the project library lacks.
inline QColor missing() { return QColor(220, 0, 0); }
// The selection and the rectangle drawn to choose with.
inline QColor selection() { return QColor(0, 120, 215); }
} // namespace paintcolor

QPointF toMm(const Point &point);

// The end of a pin's line that touches the symbol body.
Point pinBodyEnd(const Pin &pin);

// Rectangles that hold everything paintSymbol() / paintFootprint() draw.
// Never empty, so a view can always be fitted to them.
QRectF symbolBounds(const Symbol &symbol);
// The rectangle the drawings of 'symbol' take, its body: pins and the
// texts of the program left out. Null when the symbol has no drawings.
QRectF symbolBodyBounds(const Symbol &symbol);
QRectF footprintBounds(const Footprint &footprint);

void paintSymbol(QPainter &painter, const Symbol &symbol);
void paintFootprint(QPainter &painter, const Footprint &footprint);

// The paper of a sheet: its top left corner is the origin, the long side
// lies along X.
QRectF paperRect(PaperSize paper);

// Sheet 'index' of the schematic: the frame with the title block, then
// everything placed on the sheet. Texts stay readable whatever way a symbol
// is turned or mirrored. An index out of range draws nothing. Symbols come
// from 'library'; one it lacks is drawn as a red square with a question mark.
void paintSheet(QPainter &painter, const Schematic &schematic, const Library &library,
                int index, const TitleBlock &titleBlock);

// Where an object of a sheet is for the mouse, in millimetres. What is a
// line — a wire, a bus, a drawing that is not filled in — is found by its
// line; anything else by a rectangle around what is drawn of it (a
// component's texts included). 'rect' holds the whole object either way.
struct SheetObjectShape
{
    ObjectId id;
    QRectF rect;
    // The line, for what is found by it; empty for the rest.
    QPolygonF polyline;
};

// The shapes of everything placed on 'sheet', in the order it is drawn: the
// last is on top.
QList<SheetObjectShape> sheetObjectShapes(const Sheet &sheet, const Library &library);

// The texts a component shows beside its symbol.
enum class ComponentText { Designator, Value };
// What is drawn for the text: the designator, or the value; a grey "VALUE"
// mark stands where the value is empty, to show where it goes and to be
// taken there, unless the component's part in 'library' needs no value.
QString componentTextShown(const Component &component, ComponentText text,
                           const Library &library);
// Where the text stands on the sheet: the component's point and the text's offset.
Point componentTextPoint(const Component &component, ComponentText text);
// A rectangle surely holding the text as it is drawn, in millimetres.
QRectF componentTextRect(const Component &component, ComponentText text,
                         const Library &library);

// A rectangle surely holding the name of a bus, in millimetres: above its
// first point.
QRectF busNameRect(const Bus &bus);

// Where the text of a label stands: its point and the text's offset.
Point labelTextPoint(const Label &label);
// A rectangle surely holding the text of a label, in millimetres. The
// label as a whole is also found by its point (see sheetObjectShapes()).
QRectF labelTextRect(const Label &label);

// A rectangle surely holding the net name of a power symbol, in
// millimetres, where it is drawn or would be if its symbol did not hide it;
// a square around its point when 'library' lacks the symbol.
QRectF powerNameRect(const PowerSymbol &power, const Library &library);

// A rectangle that holds everything paintBoard() draws; never empty.
QRectF boardBounds(const Board &board, const Library &library);
// The board as seen from the top: bottom copper under top copper, the
// silkscreen and the outline over them. What lies on the bottom side is
// mirrored, texts included, as it is on the real board. Footprints come
// from 'library'; one it lacks is drawn as a red square with a question mark.
void paintBoard(QPainter &painter, const Board &board, const Library &library);

} // namespace ppcb
