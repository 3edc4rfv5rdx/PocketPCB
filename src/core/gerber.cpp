#include "gerber.h"

#include "boardprint.h"
#include "paint.h"
#include "version.h"

#include <QLineF>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

const double NM_PER_MM_F = 1e6;
// The width of the line of the board's outline, in mm.
const double PROFILE_LINE = 0.1;
// Apertures are numbered from 10 up; lower codes are commands.
const int FIRST_APERTURE = 10;
const int APERTURE_DECIMALS = 6;
const int DRILL_DECIMALS = 3;

QString number(double mm, int decimals)
{
    return QString::number(mm, 'f', decimals);
}

QString apertureSize(double mm)
{
    return number(mm, APERTURE_DECIMALS);
}

// The pieces a dashed line through 'points' is drawn as: 'pattern' gives
// the lengths of dash and gap in turn.
QList<QPolygonF> dashes(const QPolygonF &points, const QList<double> &pattern)
{
    QList<QPolygonF> pieces;
    double total = 0;
    for (const double length : pattern)
        total += length;
    if (points.size() < 2 || pattern.size() < 2 || total <= 0)
        return {points};
    int index = 0;
    double left = pattern.at(0);
    bool drawn = true;
    QPolygonF piece{points.at(0)};
    for (int i = 1; i < points.size(); ++i) {
        QPointF from = points.at(i - 1);
        const QPointF to = points.at(i);
        double length = QLineF(from, to).length();
        while (length > left) {
            const QPointF at = from + (to - from) * (left / length);
            if (drawn) {
                piece.append(at);
                pieces.append(piece);
            }
            piece = QPolygonF{at};
            length -= left;
            from = at;
            drawn = !drawn;
            index = (index + 1) % int(pattern.size());
            left = pattern.at(index);
        }
        left -= length;
        piece.append(to);
    }
    if (drawn && piece.size() > 1)
        pieces.append(piece);
    return pieces;
}

// A paint engine that writes what is painted into a Gerber file: lines with
// the pen's width, filled shapes as regions. The silkscreen is painted
// through it, so that the file holds what the board shows.
class GerberEngine : public QPaintEngine
{
public:
    explicit GerberEngine(GerberWriter &writer)
        : QPaintEngine(QPaintEngine::AllFeatures), m_writer(writer)
    {
    }

    bool begin(QPaintDevice *) override { return true; }
    bool end() override { return true; }
    Type type() const override { return QPaintEngine::User; }

    void updateState(const QPaintEngineState &state) override
    {
        const DirtyFlags flags = state.state();
        if (flags & DirtyTransform)
            m_transform = state.transform();
        if (flags & DirtyPen)
            m_pen = state.pen();
        if (flags & DirtyBrush)
            m_brush = state.brush();
    }

    void drawPixmap(const QRectF &, const QPixmap &, const QRectF &) override {}

    void drawLines(const QLineF *lines, int count) override
    {
        for (int i = 0; i < count; ++i)
            stroke(QPolygonF{lines[i].p1(), lines[i].p2()});
    }

    void drawLines(const QLine *lines, int count) override
    {
        for (int i = 0; i < count; ++i)
            stroke(QPolygonF{QPointF(lines[i].p1()), QPointF(lines[i].p2())});
    }

    void drawPolygon(const QPointF *points, int count, PolygonDrawMode mode) override
    {
        QPolygonF polygon;
        for (int i = 0; i < count; ++i)
            polygon.append(points[i]);
        if (mode == PolylineMode) {
            stroke(polygon);
            return;
        }
        QPainterPath path;
        path.addPolygon(polygon);
        path.closeSubpath();
        drawPath(path);
    }

    void drawPolygon(const QPoint *points, int count, PolygonDrawMode mode) override
    {
        QList<QPointF> converted;
        for (int i = 0; i < count; ++i)
            converted.append(QPointF(points[i]));
        drawPolygon(converted.constData(), count, mode);
    }

    void drawPath(const QPainterPath &path) override
    {
        if (m_brush.style() != Qt::NoBrush) {
            for (const QPolygonF &polygon : path.toFillPolygons())
                m_writer.region(m_transform.map(polygon));
        }
        for (const QPolygonF &polygon : path.toSubpathPolygons())
            stroke(polygon);
    }

    void drawEllipse(const QRectF &rect) override
    {
        QPainterPath path;
        path.addEllipse(rect);
        drawPath(path);
    }

    void drawRects(const QRectF *rects, int count) override
    {
        QPainterPath path;
        for (int i = 0; i < count; ++i)
            path.addRect(rects[i]);
        drawPath(path);
    }

private:
    // A line through 'points' with the pen, dashed as it is.
    void stroke(const QPolygonF &points)
    {
        if (m_pen.style() == Qt::NoPen || points.size() < 2)
            return;
        const double width = m_pen.widthF();
        QList<double> pattern;
        if (m_pen.style() != Qt::SolidLine) {
            for (const qreal length : m_pen.dashPattern())
                pattern.append(length * width);
        }
        // The pen's width grows with the painter's scale.
        const double scale = std::sqrt(std::abs(m_transform.determinant()));
        for (const QPolygonF &piece : dashes(points, pattern))
            m_writer.polyline(m_transform.map(piece), width * scale);
    }

    GerberWriter &m_writer;
    QTransform m_transform;
    QPen m_pen;
    QBrush m_brush;
};

// A device painted on in the board's millimetres, into a Gerber file.
class GerberDevice : public QPaintDevice
{
public:
    explicit GerberDevice(GerberWriter &writer) : m_engine(writer) {}
    QPaintEngine *paintEngine() const override { return &m_engine; }

protected:
    int metric(PaintDeviceMetric metric) const override
    {
        // A large page at a millimetre a dot: nothing of the board is cut off.
        const int size = 100000;
        const int dpi = 25;
        switch (metric) {
        case PdmWidth:
        case PdmHeight:
        case PdmWidthMM:
        case PdmHeightMM:
            return size;
        case PdmNumColors:
            return 2;
        case PdmDepth:
            return 1;
        case PdmDpiX:
        case PdmDpiY:
        case PdmPhysicalDpiX:
        case PdmPhysicalDpiY:
            return dpi;
        default:
            return QPaintDevice::metric(metric);
        }
    }

private:
    mutable GerberEngine m_engine;
};

QString sideFunction(BoardSide side)
{
    return side == BoardSide::Top ? QStringLiteral("Top") : QStringLiteral("Bot");
}

QByteArray copperFile(const Board &board, const Library &library, BoardSide side, int layer)
{
    GerberWriter writer(QStringLiteral("Copper,L%1,%2").arg(layer).arg(sideFunction(side)));
    for (const Track &track : board.tracks) {
        if (track.side == side)
            writer.line(toMm(track.from), toMm(track.to), nmToMm(track.width));
    }
    for (const CopperPad &pad : copperPads(board, library, side))
        writer.flash(pad.shape, nmToMm(pad.sizeX), nmToMm(pad.sizeY), toMm(pad.at));
    return writer.data();
}

// The openings of the solder mask: over each pad, larger by the rules'
// expansion all round, over vias only when they are not covered, and over
// each hole without a pad.
QByteArray maskFile(const Board &board, const Library &library, const Rules &rules,
                    BoardSide side)
{
    GerberWriter writer(QStringLiteral("Soldermask,%1").arg(sideFunction(side)), true);
    const double grow = 2 * nmToMm(rules.maskExpansion);
    for (const CopperPad &pad : copperPads(board, library, side)) {
        if (pad.kind == CopperPad::Kind::Via && rules.viasCovered)
            continue;
        writer.flash(pad.shape, nmToMm(pad.sizeX) + grow, nmToMm(pad.sizeY) + grow,
                     toMm(pad.at));
    }
    for (const MountingHole &hole : board.holes) {
        if (hole.ring == 0)
            writer.flash(PadShape::Circle, nmToMm(hole.drill) + grow,
                         nmToMm(hole.drill) + grow, toMm(hole.at));
    }
    return writer.data();
}

QByteArray silkscreenFile(const Board &board, const Library &library, BoardSide side)
{
    GerberWriter writer(QStringLiteral("Legend,%1").arg(sideFunction(side)));
    GerberDevice device(writer);
    QPainter painter(&device);
    paintSilkscreenLayer(painter, board, library, side);
    painter.end();
    return writer.data();
}

QByteArray profileFile(const Board &board)
{
    GerberWriter writer(QStringLiteral("Profile,NP"));
    for (const QPolygonF &polygon : outlinePath(board.outline).toSubpathPolygons())
        writer.polyline(polygon, PROFILE_LINE);
    return writer.data();
}

} // namespace

GerberWriter::GerberWriter(const QString &fileFunction, bool negative)
    : m_function(fileFunction), m_negative(negative)
{
}

void GerberWriter::line(const QPointF &from, const QPointF &to, double width)
{
    if (width <= 0)
        return;
    select(aperture(QStringLiteral("C,") + apertureSize(width)));
    moveTo(from);
    add(gerberPoint(to) + QStringLiteral("D01*"));
    m_at = to;
}

void GerberWriter::polyline(const QPolygonF &points, double width)
{
    for (int i = 1; i < points.size(); ++i)
        line(points.at(i - 1), points.at(i), width);
}

void GerberWriter::flash(PadShape shape, double sizeX, double sizeY, const QPointF &at)
{
    QString definition;
    if (shape == PadShape::Circle || (shape == PadShape::Oval && sizeX == sizeY))
        definition = QStringLiteral("C,") + apertureSize(std::max(sizeX, sizeY));
    else
        definition = (shape == PadShape::Rect ? QStringLiteral("R,") : QStringLiteral("O,"))
                     + apertureSize(sizeX) + QLatin1Char('X') + apertureSize(sizeY);
    select(aperture(definition));
    add(gerberPoint(at) + QStringLiteral("D03*"));
    m_at = at;
}

void GerberWriter::region(const QPolygonF &points)
{
    if (points.size() < 3)
        return;
    add(QStringLiteral("G36*"));
    add(gerberPoint(points.first()) + QStringLiteral("D02*"));
    for (int i = 1; i < points.size(); ++i)
        add(gerberPoint(points.at(i)) + QStringLiteral("D01*"));
    // A region is closed.
    if (points.last() != points.first())
        add(gerberPoint(points.first()) + QStringLiteral("D01*"));
    add(QStringLiteral("G37*"));
    m_at.reset();
}

QByteArray GerberWriter::data() const
{
    QStringList lines;
    lines << QStringLiteral("%TF.GenerationSoftware,PocketPCB,PocketPCB,%1*%")
                 .arg(QLatin1String(appVersion()))
          << QStringLiteral("%TF.FileFunction,%1*%").arg(m_function)
          << QStringLiteral("%TF.FilePolarity,%1*%")
                 .arg(m_negative ? QStringLiteral("Negative") : QStringLiteral("Positive"))
          << QStringLiteral("%FSLAX46Y46*%") << QStringLiteral("%MOMM*%")
          << QStringLiteral("%LPD*%") << QStringLiteral("G01*");
    for (int i = 0; i < m_definitions.size(); ++i)
        lines << QStringLiteral("%ADD%1%2*%").arg(FIRST_APERTURE + i).arg(m_definitions.at(i));
    lines << m_body << QStringLiteral("M02*");
    return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toLatin1();
}

int GerberWriter::aperture(const QString &definition)
{
    auto it = m_apertures.find(definition);
    if (it == m_apertures.end()) {
        it = m_apertures.insert(definition, FIRST_APERTURE + int(m_definitions.size()));
        m_definitions.append(definition);
    }
    return it.value();
}

void GerberWriter::select(int code)
{
    if (code == m_current)
        return;
    add(QStringLiteral("D%1*").arg(code));
    m_current = code;
}

void GerberWriter::moveTo(const QPointF &at)
{
    if (m_at && gerberPoint(*m_at) == gerberPoint(at))
        return;
    add(gerberPoint(at) + QStringLiteral("D02*"));
    m_at = at;
}

void GerberWriter::add(const QString &line)
{
    m_body.append(line);
}

QString gerberPoint(const QPointF &at)
{
    return QStringLiteral("X%1Y%2")
        .arg(qRound64(at.x() * NM_PER_MM_F))
        .arg(-qRound64(at.y() * NM_PER_MM_F));
}

QByteArray excellonFile(const QList<DrillHole> &holes, bool plated)
{
    // The tools in ascending diameter, each with its holes.
    QMap<Coord, QList<Point>> tools;
    for (const DrillHole &hole : holes) {
        if (hole.plated == plated)
            tools[hole.diameter].append(hole.at);
    }
    QStringList lines;
    // Altium's viewer puts every hole at the origin without the FORMAT line.
    lines << QStringLiteral("M48")
          << QStringLiteral("; DRILL file {PocketPCB %1}").arg(QLatin1String(appVersion()))
          << QStringLiteral("; FORMAT={-:-/ absolute / metric / decimal}")
          << QStringLiteral("; #@! TF.GenerationSoftware,PocketPCB,PocketPCB,%1")
                 .arg(QLatin1String(appVersion()))
          << QStringLiteral("; #@! TF.FileFunction,%1,1,2,%2")
                 .arg(plated ? QStringLiteral("Plated") : QStringLiteral("NonPlated"))
                 .arg(plated ? QStringLiteral("PTH") : QStringLiteral("NPTH"))
          << QStringLiteral("FMAT,2") << QStringLiteral("METRIC");
    int tool = 1;
    for (auto it = tools.cbegin(); it != tools.cend(); ++it, ++tool)
        lines << QStringLiteral("T%1C%2").arg(tool).arg(number(nmToMm(it.key()), DRILL_DECIMALS));
    lines << QStringLiteral("%") << QStringLiteral("G90") << QStringLiteral("G05");
    tool = 1;
    for (auto it = tools.cbegin(); it != tools.cend(); ++it, ++tool) {
        lines << QStringLiteral("T%1").arg(tool);
        for (const Point &at : it.value())
            lines << QStringLiteral("X%1Y%2")
                         .arg(number(nmToMm(at.x), DRILL_DECIMALS))
                         .arg(number(-nmToMm(at.y), DRILL_DECIMALS));
    }
    lines << QStringLiteral("T0") << QStringLiteral("M30");
    return (lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toLatin1();
}

QList<OutputFile> gerberFiles(const Design &design, const QString &baseName)
{
    const Board &board = design.board;
    const Library &library = design.library;
    const Rules &rules = design.project.rules;
    QList<OutputFile> files;
    const auto file = [&files, &baseName](const QString &ending, const QByteArray &data) {
        files.append(OutputFile{baseName + ending, data});
    };
    int layer = 1;
    for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
        if (!copperOn(board.copper, side))
            continue;
        const bool top = side == BoardSide::Top;
        file(top ? QStringLiteral(".GTL") : QStringLiteral(".GBL"),
             copperFile(board, library, side, layer++));
        file(top ? QStringLiteral(".GTS") : QStringLiteral(".GBS"),
             maskFile(board, library, rules, side));
    }
    for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
        if (silkscreenUsed(board, side))
            file(side == BoardSide::Top ? QStringLiteral(".GTO") : QStringLiteral(".GBO"),
                 silkscreenFile(board, library, side));
    }
    file(QStringLiteral(".GKO"), profileFile(board));
    const QList<DrillHole> holes = drillHoles(board, library, rules);
    for (const bool plated : {true, false}) {
        const bool any = std::any_of(holes.cbegin(), holes.cend(), [plated](const DrillHole &hole) {
            return hole.plated == plated;
        });
        if (any)
            file(plated ? QStringLiteral("-PTH.DRL") : QStringLiteral("-NPTH.DRL"),
                 excellonFile(holes, plated));
    }
    return files;
}

} // namespace ppcb
