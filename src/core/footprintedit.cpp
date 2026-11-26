#include "footprintedit.h"

#include "jsonio.h"
#include "paint.h"
#include "trackedit.h"

#include <algorithm>

namespace ppcb {

namespace {

Point turnedQuarter(const Point &point, const Point &center)
{
    const Point offset = point - center;
    return center + Point{-offset.y, offset.x};
}

Pad padTurned(Pad pad, const Point &center)
{
    pad.at = turnedQuarter(pad.at, center);
    std::swap(pad.sizeX, pad.sizeY);
    return pad;
}

Graphic graphicMoved(Graphic graphic, const Point &delta)
{
    graphic.a = graphic.a + delta;
    graphic.b = graphic.b + delta;
    for (Point &point : graphic.points)
        point = point + delta;
    return graphic;
}

} // namespace

QString nextPadNumber(const Footprint &footprint)
{
    QSet<int> taken;
    for (const Pad &pad : footprint.pads) {
        bool ok = false;
        const int number = pad.number.toInt(&ok);
        if (ok && number > 0 && pad.number == QString::number(number))
            taken.insert(number);
    }
    int number = 1;
    while (taken.contains(number))
        ++number;
    return QString::number(number);
}

int padAtPoint(const Footprint &footprint, const QPointF &at, double reach)
{
    for (int i = int(footprint.pads.size()) - 1; i >= 0; --i) {
        if (padRect(footprint.pads.at(i)).adjusted(-reach, -reach, reach, reach).contains(at))
            return i;
    }
    return -1;
}

QSet<int> padsInside(const Footprint &footprint, const QRectF &rect)
{
    QSet<int> inside;
    for (int i = 0; i < footprint.pads.size(); ++i) {
        if (rect.contains(padRect(footprint.pads.at(i))))
            inside.insert(i);
    }
    return inside;
}

QRectF padsBounds(const Footprint &footprint, const QSet<int> &pads)
{
    QRectF bounds;
    for (int i = 0; i < footprint.pads.size(); ++i) {
        if (pads.contains(i))
            bounds = bounds.isNull() ? padRect(footprint.pads.at(i))
                                     : bounds.united(padRect(footprint.pads.at(i)));
    }
    return bounds;
}

Footprint withPadsAdded(const Footprint &footprint, const QList<Pad> &pads)
{
    Footprint result = footprint;
    result.pads.append(pads);
    return result;
}

Footprint withPadsMoved(const Footprint &footprint, const QSet<int> &pads, const Point &delta)
{
    Footprint result = footprint;
    for (int i = 0; i < result.pads.size(); ++i) {
        if (pads.contains(i))
            result.pads[i].at = result.pads.at(i).at + delta;
    }
    return result;
}

Footprint withPadsRotated(const Footprint &footprint, const QSet<int> &pads, const Point &center)
{
    Footprint result = footprint;
    for (int i = 0; i < result.pads.size(); ++i) {
        if (pads.contains(i))
            result.pads[i] = padTurned(result.pads.at(i), center);
    }
    return result;
}

QList<Pad> padsRotated(const QList<Pad> &pads, const Point &center)
{
    QList<Pad> result;
    for (const Pad &pad : pads)
        result.append(padTurned(pad, center));
    return result;
}

Footprint withoutPads(const Footprint &footprint, const QSet<int> &pads)
{
    Footprint result = footprint;
    result.pads.clear();
    for (int i = 0; i < footprint.pads.size(); ++i) {
        if (!pads.contains(i))
            result.pads.append(footprint.pads.at(i));
    }
    return result;
}

Footprint withPadsReplaced(const Footprint &footprint, const QList<int> &pads,
                           const QList<Pad> &changed)
{
    Footprint result = footprint;
    for (int k = 0; k < pads.size() && k < changed.size(); ++k) {
        const int i = pads.at(k);
        if (i >= 0 && i < result.pads.size())
            result.pads[i] = changed.at(k);
    }
    return result;
}

Footprint withOriginAt(const Footprint &footprint, const Point &at)
{
    const Point delta = Point{} - at;
    Footprint result = footprint;
    for (Pad &pad : result.pads)
        pad.at = pad.at + delta;
    for (Graphic &graphic : result.graphics)
        graphic = graphicMoved(graphic, delta);
    result.refAt = result.refAt + delta;
    result.valueAt = result.valueAt + delta;
    return result;
}

Point footprintMiddle(const Footprint &footprint)
{
    QRectF bounds = footprintBodyBounds(footprint);
    if (bounds.isNull()) {
        QSet<int> all;
        for (int i = 0; i < footprint.pads.size(); ++i)
            all.insert(i);
        bounds = padsBounds(footprint, all);
    }
    if (bounds.isNull())
        return Point{};
    return Point{mmToNm(bounds.center().x()), mmToNm(bounds.center().y())};
}

PadStroke padStroke(const Pad &pad)
{
    const Coord narrow = std::min(pad.sizeX, pad.sizeY);
    const Point half = pad.sizeX > pad.sizeY ? Point{(pad.sizeX - narrow) / 2, 0}
                                             : Point{0, (pad.sizeY - narrow) / 2};
    return PadStroke{pad.at - half, pad.at + half, narrow / 2};
}

Coord padsApart(const Pad &a, const Pad &b)
{
    const PadStroke sa = padStroke(a);
    const PadStroke sb = padStroke(b);
    const double apart = piecesApart(toMm(sa.from), toMm(sa.to), toMm(sb.from), toMm(sb.to));
    return std::max<Coord>(0, mmToNm(apart) - sa.radius - sb.radius);
}

bool operator==(const PadGap &a, const PadGap &b)
{
    return a.first == b.first && a.second == b.second && a.gap == b.gap && a.at == b.at;
}

QList<PadGap> padsTooNear(const Footprint &footprint, Coord clearance)
{
    // A side of the part, or the other one, each pad is on.
    const auto front = [](const Pad &pad) { return pad.layers != PadLayers::Back; };
    const auto back = [](const Pad &pad) { return pad.layers != PadLayers::Front; };
    QList<PadGap> gaps;
    for (int i = 0; i < footprint.pads.size(); ++i) {
        const Pad &a = footprint.pads.at(i);
        for (int j = i + 1; j < footprint.pads.size(); ++j) {
            const Pad &b = footprint.pads.at(j);
            if (!((front(a) && front(b)) || (back(a) && back(b))))
                continue;
            if (!a.number.isEmpty() && a.number == b.number)
                continue;
            const Coord gap = padsApart(a, b);
            if (gap >= clearance)
                continue;
            const Point at{(a.at.x + b.at.x) / 2, (a.at.y + b.at.y) / 2};
            gaps.append(PadGap{i, j, gap, at});
        }
    }
    return gaps;
}

QString padProblem(const Pad &pad)
{
    // The messages of the file reader, so that the two say it alike.
    if (pad.sizeX <= 0 || pad.sizeY <= 0)
        return JsonReader::tr("a size greater than zero is expected");
    if (pad.shape == PadShape::Circle && pad.sizeX != pad.sizeY)
        return JsonReader::tr("a round pad must have equal width and height");
    if (pad.hole < 0 || pad.hole >= std::min(pad.sizeX, pad.sizeY))
        return JsonReader::tr("the hole must be smaller than the pad");
    if (pad.hole == 0 && pad.layers == PadLayers::Both)
        return JsonReader::tr("a pad without a hole must be on one layer");
    return QString();
}

QList<Pad> padRow(const PadRow &row, const Pad &like)
{
    const bool twoColumns = row.kind != PadRowKind::Single;
    if (row.count < 1 || (twoColumns && row.count % 2 != 0))
        return {};
    const int down = twoColumns ? row.count / 2 : row.count;
    QList<Pad> pads;
    for (int k = 0; k < row.count; ++k) {
        // Which column and how far down it.
        int column = 0;
        int step = k;
        if (row.kind == PadRowKind::Double) {
            column = k % 2;
            step = k / 2;
        } else if (row.kind == PadRowKind::Dip && k >= down) {
            column = 1;
            step = row.count - 1 - k;
        }
        Pad pad = like;
        pad.number = QString::number(row.firstNumber + k);
        pad.at = Point{column * row.spacing, step * row.pitch};
        if (k == 0 && row.firstSquare)
            pad.shape = PadShape::Rect;
        pads.append(pad);
    }
    return pads;
}

} // namespace ppcb
