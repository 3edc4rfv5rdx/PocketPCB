#include "strokefont.h"

#include <cstring>

namespace ppcb {

namespace {

// The code of the first glyph.
const char32_t FIRST_CODE = 0x20;
const char32_t QUESTION_MARK = '?';
const char32_t OHM_SIGN = 0x2126;
const char32_t GREEK_CAPITAL_OMEGA = 0x3A9;
// A coordinate is its letter less this one.
const char COORDINATE_ORIGIN = 'R';
// Two letters that lift the pen.
const char PEN_UP[] = " R";
// The capitals of a glyph stand from y = -12 to the baseline at y = 9.
const double CAP_HEIGHT_UNITS = 21;
const double BASELINE_UNITS = 9;

struct Glyph
{
    QList<QPolygonF> strokes;
    double width = 0;
};

Glyph parsedGlyph(const char *shape)
{
    Glyph glyph;
    const size_t length = std::strlen(shape);
    if (length < 2)
        return glyph;
    const double left = shape[0] - COORDINATE_ORIGIN;
    const double right = shape[1] - COORDINATE_ORIGIN;
    glyph.width = (right - left) / CAP_HEIGHT_UNITS;
    QPolygonF line;
    const auto endLine = [&glyph, &line] {
        // A single point is a dot: a line of no length, drawn by its round cap.
        if (line.size() == 1)
            line.append(line.first());
        if (!line.isEmpty())
            glyph.strokes.append(line);
        line.clear();
    };
    for (size_t i = 2; i + 1 < length; i += 2) {
        if (shape[i] == PEN_UP[0] && shape[i + 1] == PEN_UP[1]) {
            endLine();
            continue;
        }
        const double x = shape[i] - COORDINATE_ORIGIN - left;
        const double y = shape[i + 1] - COORDINATE_ORIGIN - BASELINE_UNITS;
        line.append(QPointF(x / CAP_HEIGHT_UNITS, y / CAP_HEIGHT_UNITS));
    }
    endLine();
    return glyph;
}

QList<Glyph> parsedGlyphs()
{
    QList<Glyph> glyphs;
    glyphs.reserve(STROKE_GLYPH_COUNT);
    for (int i = 0; i < STROKE_GLYPH_COUNT; ++i)
        glyphs.append(parsedGlyph(STROKE_GLYPHS[i]));
    return glyphs;
}

const Glyph &glyphOf(char32_t code)
{
    static const QList<Glyph> glyphs = parsedGlyphs();
    if (code == OHM_SIGN)
        code = GREEK_CAPITAL_OMEGA;
    if (code < FIRST_CODE || code - FIRST_CODE >= char32_t(glyphs.size()))
        code = QUESTION_MARK;
    return glyphs.at(int(code - FIRST_CODE));
}

} // namespace

StrokeText strokeText(const QString &text)
{
    StrokeText result;
    for (const char32_t code : text.toUcs4()) {
        const Glyph &glyph = glyphOf(code);
        for (const QPolygonF &stroke : glyph.strokes)
            result.strokes.append(stroke.translated(result.width, 0));
        result.width += glyph.width;
    }
    return result;
}

} // namespace ppcb
