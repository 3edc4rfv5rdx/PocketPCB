#pragma once

#include "boardcopper.h"
#include "footprint.h"
#include "project.h"
#include "zipfile.h"

#include <QMap>
#include <QPointF>
#include <QPolygonF>
#include <QStringList>

#include <optional>

namespace ppcb {

// The files a factory makes the board from: Gerber RS-274X with X2
// attributes, a file for each layer, and Excellon drill files, plated and
// not plated holes apart. Everything as seen from the top; the board's Y
// grows down, the files' up.

// Writes one Gerber file. Lengths and points in the board's millimetres.
class GerberWriter
{
public:
    // 'fileFunction' is the X2 file function ("Copper,L1,Top"); a
    // 'negative' file draws what is left out, as a solder mask's openings.
    GerberWriter(const QString &fileFunction, bool negative = false);

    // A line drawn with a round aperture 'width' across; nothing for no width.
    void line(const QPointF &from, const QPointF &to, double width);
    // Lines through 'points', one after another.
    void polyline(const QPolygonF &points, double width);
    // A pad: the aperture of 'shape', 'sizeX' by 'sizeY', flashed at 'at'.
    void flash(PadShape shape, double sizeX, double sizeY, const QPointF &at);
    // A filled region with the corners 'points'.
    void region(const QPolygonF &points);

    // The whole file.
    QByteArray data() const;

private:
    // The D code of the aperture 'definition' ("C,0.250000"), defined the
    // first time.
    int aperture(const QString &definition);
    void select(int code);
    void moveTo(const QPointF &at);
    void add(const QString &line);

    QString m_function;
    bool m_negative = false;
    QMap<QString, int> m_apertures;
    QStringList m_definitions;
    QStringList m_body;
    int m_current = -1;
    std::optional<QPointF> m_at;
};

// "X12700000Y-5080000": a point in the files' units (nanometres, Y up).
QString gerberPoint(const QPointF &at);

// The Excellon file of 'holes', in millimetres with a decimal point: the
// plated ones or the others ('plated'), through the board. Its header is
// KiCad's, which viewers that ignore the decimal point read too.
QByteArray excellonFile(const QList<DrillHole> &holes, bool plated);

// Every file of the board of 'design', named after 'baseName': the copper
// (.GTL, .GBL) and solder mask (.GTS, .GBS) of each side with copper, the
// silkscreen (.GTO, .GBO) of each side with something on it, the outline
// (.GKO), and the drill files (-PTH.DRL, -NPTH.DRL) when there are such
// holes.
QList<OutputFile> gerberFiles(const Design &design, const QString &baseName);

} // namespace ppcb
