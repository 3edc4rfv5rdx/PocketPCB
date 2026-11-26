#include "drc.h"

#include "footprintedit.h"
#include "paint.h"
#include "ratsnest.h"
#include "trackedit.h"

#include <QHash>
#include <QLineF>
#include <QMap>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace ppcb {

namespace {

// Copper of the board, or a hole without a pad, as a line with a round end
// of 'radius' on each (millimetres): the long axis of a pill or a
// rectangle, a track, or the middle alone of a round pad, a via or a hole.
struct Shape
{
    DrcItem item;
    // The placement a pad is of; null for others.
    ObjectId placement;
    bool top = false;
    bool bottom = false;
    QPointF from;
    QPointF to;
    double radius = 0;
    // A hole without copper: kept clear of copper, never shorting it.
    bool hole = false;
};

// The nearest points of the pieces 'a'-'b' and 'c'-'d'; where they cross,
// that point twice.
QLineF nearestPoints(const QPointF &a, const QPointF &b, const QPointF &c, const QPointF &d)
{
    if (piecesApart(a, b, c, d) == 0) {
        // Crossing: where the lines meet.
        QPointF at;
        if (QLineF(a, b).intersects(QLineF(c, d), &at) == QLineF::BoundedIntersection)
            return QLineF(at, at);
    }
    const QList<QLineF> candidates = {QLineF(nearestOnPiece(a, b, c), c),
                                      QLineF(nearestOnPiece(a, b, d), d),
                                      QLineF(a, nearestOnPiece(c, d, a)),
                                      QLineF(b, nearestOnPiece(c, d, b))};
    return *std::min_element(candidates.cbegin(), candidates.cend(),
                             [](const QLineF &x, const QLineF &y) { return x.length() < y.length(); });
}

Point toNm(const QPointF &mm)
{
    return Point{mmToNm(mm.x()), mmToNm(mm.y())};
}

// The pad's name: the placement's designator and the pad's number.
QString padName(const Placement &placement, const Pad &pad)
{
    return placement.designator + QLatin1Char('.') + pad.number;
}


class Checker
{
public:
    explicit Checker(const Design &design)
        : m_design(design)
        , m_board(design.board)
        , m_rules(design.project.rules)
        , m_classes(design)
    {
        const QPainterPath outline = outlinePath(m_board.outline);
        if (!outline.isEmpty())
            m_outline = outline.toFillPolygon();
    }

    QList<DrcProblem> problems()
    {
        collectShapes();
        checkPairs();
        checkEdge();
        checkRings();
        checkWidths();
        checkDrills();
        checkUnrouted();
        std::stable_sort(m_problems.begin(), m_problems.end(),
                         [](const DrcProblem &a, const DrcProblem &b) {
                             if (a.error != b.error)
                                 return a.error;
                             if (a.kind != b.kind)
                                 return int(a.kind) < int(b.kind);
                             if (a.at.y != b.at.y)
                                 return a.at.y < b.at.y;
                             return a.at.x < b.at.x;
                         });
        return m_problems;
    }

private:
    // The clearance the class of 'net' asks for; the rules' one without
    // classes.
    Coord netClearance(const QString &net) const
    {
        const NetClass *netClass = m_classes.of(net);
        return netClass != nullptr ? netClass->clearance : m_rules.clearance;
    }

    // A problem added at 'level': none when the check is off.
    DrcProblem *add(DrcKind kind, CheckLevel level, const Point &at)
    {
        if (level == CheckLevel::Off)
            return nullptr;
        DrcProblem problem;
        problem.kind = kind;
        problem.error = level == CheckLevel::Error;
        problem.at = at;
        m_problems.append(problem);
        return &m_problems.last();
    }

    bool onBoard(const QPointF &at) const
    {
        return m_outline.isEmpty() || m_outline.containsPoint(at, Qt::OddEvenFill);
    }

    void collectShapes()
    {
        const bool top = copperOn(m_board.copper, BoardSide::Top);
        const bool bottom = copperOn(m_board.copper, BoardSide::Bottom);
        for (const Placement &placement : m_board.placements) {
            const Footprint *footprint = m_design.library.findFootprint(placement.footprint);
            if (footprint == nullptr || footprint->pads.isEmpty())
                continue;
            const bool off = std::none_of(
                footprint->pads.cbegin(), footprint->pads.cend(), [&](const Pad &pad) {
                    return onBoard(toMm(placedPadPoint(placement, pad.at)));
                });
            if (off && !m_outline.isEmpty()) {
                if (DrcProblem *problem =
                        add(DrcKind::OffBoard, CheckLevel::Warning, placement.at))
                    problem->name = placement.designator;
                continue;
            }
            for (const Pad &pad : footprint->pads) {
                const CopperSides sides = padCopperSides(pad.layers, placement.side);
                const PadStroke stroke = padStroke(pad);
                Shape shape;
                shape.item = DrcItem{DrcItem::Kind::Pad, placement.id, padName(placement, pad),
                                     placement.padNets.value(pad.number)};
                shape.placement = placement.id;
                shape.top = top && copperOn(sides, BoardSide::Top);
                shape.bottom = bottom && copperOn(sides, BoardSide::Bottom);
                shape.from = toMm(placedPadPoint(placement, stroke.from));
                shape.to = toMm(placedPadPoint(placement, stroke.to));
                shape.radius = nmToMm(stroke.radius);
                m_shapes.append(shape);
                if (pad.hole > 0)
                    m_padHoles.append({placement, pad});
            }
        }
        for (const Track &track : m_board.tracks) {
            Shape shape;
            shape.item = DrcItem{DrcItem::Kind::Track, track.id, QString(), track.net};
            shape.top = track.side == BoardSide::Top;
            shape.bottom = track.side == BoardSide::Bottom;
            shape.from = toMm(track.from);
            shape.to = toMm(track.to);
            shape.radius = nmToMm(track.width) / 2;
            m_shapes.append(shape);
        }
        for (const Via &via : m_board.vias) {
            Shape shape;
            shape.item = DrcItem{DrcItem::Kind::Via, via.id, QString(), via.net};
            shape.top = top;
            shape.bottom = bottom;
            shape.from = shape.to = toMm(via.at);
            shape.radius = nmToMm(via.size) / 2;
            m_shapes.append(shape);
        }
        for (const WireLink &link : m_board.links) {
            for (const Point &end : {link.from, link.to}) {
                Shape shape;
                shape.item = DrcItem{DrcItem::Kind::LinkPad, link.id, link.designator, link.net};
                shape.top = top;
                shape.bottom = bottom;
                shape.from = shape.to = toMm(end);
                shape.radius = nmToMm(link.size) / 2;
                m_shapes.append(shape);
            }
        }
        for (const MountingHole &hole : m_board.holes) {
            Shape shape;
            shape.from = shape.to = toMm(hole.at);
            shape.radius = nmToMm(holeOuterDiameter(hole)) / 2;
            if (hole.ring > 0) {
                shape.item = DrcItem{DrcItem::Kind::HolePad, hole.id, QString(), hole.net};
                shape.top = holePadOn(m_board, hole, BoardSide::Top);
                shape.bottom = holePadOn(m_board, hole, BoardSide::Bottom);
            } else {
                shape.item = DrcItem{DrcItem::Kind::Hole, hole.id, QString(), QString()};
                shape.top = shape.bottom = true;
                shape.hole = true;
            }
            m_shapes.append(shape);
        }
    }

    // Copper of two nets, and copper and holes, on a side both are on.
    void checkPairs()
    {
        for (int i = 0; i < m_shapes.size(); ++i) {
            const Shape &a = m_shapes.at(i);
            for (int j = i + 1; j < m_shapes.size(); ++j) {
                const Shape &b = m_shapes.at(j);
                if (!((a.top && b.top) || (a.bottom && b.bottom)) || (a.hole && b.hole))
                    continue;
                // The pads of one part keep apart as its footprint has them.
                if (!a.placement.isNull() && a.placement == b.placement)
                    continue;
                const bool hole = a.hole || b.hole;
                // Copper of one net; copper of no net is a net of its own.
                if (!hole && a.item.net == b.item.net && !a.item.net.isEmpty())
                    continue;
                // The two pads of one wire link.
                if (a.item.id == b.item.id)
                    continue;
                const QLineF nearest = nearestPoints(a.from, a.to, b.from, b.to);
                const double gap = nearest.length() - a.radius - b.radius;
                const Coord clearance =
                    hole ? m_rules.clearance
                         : std::max(netClearance(a.item.net), netClearance(b.item.net));
                const Point at = toNm((nearest.p1() + nearest.p2()) / 2);
                DrcProblem *problem = nullptr;
                if (!hole && gap <= 0)
                    problem = add(DrcKind::Short, CheckLevel::Error, at);
                else if (gap < nmToMm(clearance))
                    problem = add(DrcKind::Clearance, m_rules.clearanceLevel, at);
                if (problem == nullptr)
                    continue;
                problem->items = {a.item, b.item};
                problem->actual = std::max<Coord>(0, mmToNm(gap));
                problem->required = clearance;
            }
        }
    }

    // Copper too near the outline, or out of it.
    void checkEdge()
    {
        if (m_outline.isEmpty())
            return;
        const double wanted = nmToMm(m_rules.edgeClearance);
        for (const Shape &shape : std::as_const(m_shapes)) {
            if (shape.hole)
                continue;
            QLineF nearest;
            double apart = std::numeric_limits<double>::max();
            for (int i = 0; i + 1 < m_outline.size(); ++i) {
                const QLineF line =
                    nearestPoints(shape.from, shape.to, m_outline.at(i), m_outline.at(i + 1));
                if (line.length() < apart) {
                    apart = line.length();
                    nearest = line;
                }
            }
            const bool inside = onBoard(shape.from) && onBoard(shape.to);
            const double gap = inside ? apart - shape.radius : 0;
            if (gap >= wanted)
                continue;
            if (DrcProblem *problem =
                    add(DrcKind::EdgeClearance, m_rules.edgeClearanceLevel, toNm(nearest.p1()))) {
                problem->items = {shape.item};
                problem->actual = std::max<Coord>(0, mmToNm(gap));
                problem->required = m_rules.edgeClearance;
            }
        }
    }

    // The copper round each hole.
    void checkRings()
    {
        const auto check = [this](const DrcItem &item, const Point &at, Coord ring) {
            if (ring >= m_rules.annularRing)
                return;
            if (DrcProblem *problem = add(DrcKind::AnnularRing, m_rules.annularRingLevel, at)) {
                problem->items = {item};
                problem->actual = ring;
                problem->required = m_rules.annularRing;
            }
        };
        for (const auto &[placement, pad] : std::as_const(m_padHoles)) {
            check(DrcItem{DrcItem::Kind::Pad, placement.id, padName(placement, pad),
                          placement.padNets.value(pad.number)},
                  placedPadPoint(placement, pad.at),
                  (std::min(pad.sizeX, pad.sizeY) - pad.hole) / 2);
        }
        for (const Via &via : m_board.vias)
            check(DrcItem{DrcItem::Kind::Via, via.id, QString(), via.net}, via.at,
                  (via.size - via.drill) / 2);
        for (const WireLink &link : m_board.links) {
            check(DrcItem{DrcItem::Kind::LinkPad, link.id, link.designator, link.net}, link.from,
                  (link.size - link.drill) / 2);
        }
        for (const MountingHole &hole : m_board.holes) {
            if (hole.ring > 0)
                check(DrcItem{DrcItem::Kind::HolePad, hole.id, QString(), hole.net}, hole.at,
                      hole.ring);
        }
    }

    // Tracks narrower than the rules or their class allow.
    void checkWidths()
    {
        const Coord narrowest = m_rules.trackWidths.isEmpty()
                                    ? 0
                                    : *std::min_element(m_rules.trackWidths.cbegin(),
                                                        m_rules.trackWidths.cend());
        for (const Track &track : m_board.tracks) {
            const Point at = toNm((toMm(track.from) + toMm(track.to)) / 2);
            const NetClass *netClass = m_classes.of(track.net);
            DrcProblem *problem = nullptr;
            Coord required = 0;
            if (track.width < narrowest) {
                required = narrowest;
                problem = add(DrcKind::MinimumWidth, m_rules.minimumWidthLevel, at);
            } else if (netClass != nullptr && track.width < netClass->trackWidth) {
                required = netClass->trackWidth;
                problem = add(DrcKind::ClassWidth, m_rules.classWidthLevel, at);
            }
            if (problem == nullptr)
                continue;
            problem->items = {DrcItem{DrcItem::Kind::Track, track.id, QString(), track.net}};
            problem->actual = track.width;
            problem->required = required;
        }
    }

    // Diameters not in the drill set, when only those may be used.
    void checkDrills()
    {
        if (!m_rules.drillsOnly)
            return;
        // Each diameter, how many holes have it and where the first is.
        QMap<Coord, QPair<int, Point>> found;
        const auto take = [&found](Coord drill, const Point &at) {
            auto it = found.find(drill);
            if (it == found.end())
                found.insert(drill, qMakePair(1, at));
            else
                ++it->first;
        };
        for (const auto &[placement, pad] : std::as_const(m_padHoles))
            take(pad.hole, placedPadPoint(placement, pad.at));
        for (const Via &via : m_board.vias)
            take(via.drill, via.at);
        for (const WireLink &link : m_board.links) {
            take(link.drill, link.from);
            take(link.drill, link.to);
        }
        for (const MountingHole &hole : m_board.holes)
            take(hole.drill, hole.at);
        for (auto it = found.cbegin(); it != found.cend(); ++it) {
            if (m_rules.drills.contains(it.key()))
                continue;
            if (DrcProblem *problem =
                    add(DrcKind::DrillNotInSet, CheckLevel::Warning, it.value().second)) {
                problem->actual = it.key();
                problem->count = it.value().first;
            }
        }
    }

    void checkUnrouted()
    {
        for (const RatsnestLine &line : ratsnest(m_board, m_design.library)) {
            if (DrcProblem *problem =
                    add(DrcKind::Unrouted, CheckLevel::Warning,
                        toNm((toMm(line.from) + toMm(line.to)) / 2)))
                problem->name = line.net;
        }
    }

    const Design &m_design;
    const Board &m_board;
    const Rules &m_rules;
    const TrackClasses m_classes;
    QPolygonF m_outline;
    QList<Shape> m_shapes;
    // The pads with holes of the placements on the board.
    QList<QPair<Placement, Pad>> m_padHoles;
    QList<DrcProblem> m_problems;
};

} // namespace

QList<DrcProblem> drcProblems(const Design &design)
{
    return Checker(design).problems();
}

} // namespace ppcb
