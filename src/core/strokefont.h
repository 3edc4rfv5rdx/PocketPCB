#pragma once

#include <QList>
#include <QPolygonF>
#include <QString>

namespace ppcb {

// The built-in stroke font, Newstroke (see strokefont_data.cpp): letters
// made of lines, the same on screen, in print and on the board, on any
// computer. It has Latin, Greek and Cyrillic letters.

// A text laid out in the font, in units of the height of capitals: x from
// 0 at the left edge of the first letter, the baseline at y = 0 and y
// growing down, so that capitals reach from y = 0 up to y = -1.
struct StrokeText
{
    // Each a line through its points.
    QList<QPolygonF> strokes;
    // How far the text goes, the room after its last letter included.
    double width = 0;
};

// 'text' laid out in the font. A letter the font lacks is drawn as '?'; the
// ohm sign is drawn as the Greek omega.
StrokeText strokeText(const QString &text);

// The glyphs, from U+0020 on, one for each code; see strokefont_data.cpp.
extern const char *const STROKE_GLYPHS[];
extern const int STROKE_GLYPH_COUNT;

} // namespace ppcb
