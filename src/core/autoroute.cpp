#include "autoroute.h"

#include "gridroute.h"
#include "paint.h"
#include "ratsnest.h"
#include "trackedit.h"

#include <QHash>
#include <QLineF>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace ppcb {

namespace {

// What a way costs: its steps, its bends, a straight piece across the grain
// of its side (upright on the top, level on the bottom), and a via, dear
// enough that a way on one side is taken while it is not much longer.
const int STRAIGHT_COST = 10;
const int DIAGONAL_COST = 14;
const int BEND45_COST = 2;
const int BEND90_COST = 6;
const int ACROSS_GRAIN_COST = 4;
const int VIA_COST = 60 * STRAIGHT_COST;
// A wire link, on a board with copper on one side: looked for only when a
// track finds no way, then as dear as this many steps; level or upright, its
// length a whole number of these steps between the least and the most, in
// nanometres.
const int LINK_COST = 40 * STRAIGHT_COST;
const Coord LINK_PITCH = 2540000;
const Coord LINK_SHORTEST = 2 * LINK_PITCH;
const Coord LINK_LONGEST = 12 * LINK_PITCH;
// Room kept from the copper of other nets beyond the clearance when there is
// room for it, in millimetres, and what a step into it adds: tracks keep to
// the middle of the room they run in, as the author moves them by hand.
const double SOFT_ROOM_MM = 1.0;
const int CROWDED_COST = 6;
// The room around what is on a board without an outline.
const Coord NO_OUTLINE_MARGIN = 5 * NM_PER_MM;

// What lies at a node of the grid: no copper's room, the room of the copper
// of one net (its index), or of more than one, or of what no net may touch.
constexpr int FREE = -1;
constexpr int BLOCKED = -2;

Coord floorTo(Coord value, Coord step)
{
    const Coord whole = value / step;
    return (value % step != 0 && value < 0 ? whole - 1 : whole) * step;
}

// The nodes the tracks may go through, on every layer, and what lies there.
class RouteGrid
{
public:
    RouteGrid(const Point &origin, Coord step, int columns, int rows, int layers)
        : m_origin(origin)
        , m_step(step)
        , m_columns(columns)
        , m_rows(rows)
        , m_layers(layers)
        , m_occupants(std::size_t(columns) * rows * layers, FREE)
        , m_near(std::size_t(columns) * rows * layers, FREE)
    {
    }

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }
    int layers() const { return m_layers; }

    bool contains(const GridNode &node) const
    {
        return node.column >= 0 && node.column < m_columns && node.row >= 0 && node.row < m_rows
               && node.layer >= 0 && node.layer < m_layers;
    }
    qint64 key(const GridNode &node) const
    {
        return (qint64(node.layer) * m_rows + node.row) * m_columns + node.column;
    }
    Point point(const GridNode &node) const
    {
        return m_origin + Point{node.column * m_step, node.row * m_step};
    }
    // The node nearest to 'at' on 'layer', on the grid or not.
    GridNode nearest(const Point &at, int layer) const
    {
        const auto along = [this](Coord from, Coord to) {
            return int(std::llround(double(to - from) / double(m_step)));
        };
        return GridNode{along(m_origin.x, at.x), along(m_origin.y, at.y), layer};
    }

    // A track of the net 'net' (or BLOCKED) may go through 'node'.
    bool usable(const GridNode &node, int net) const
    {
        if (!contains(node))
            return false;
        const int occupant = m_occupants[std::size_t(key(node))];
        return occupant == FREE || occupant == net;
    }

    // A track of the net 'net' through 'node' comes nearer to copper of
    // another net than it need, though it may.
    bool crowded(const GridNode &node, int net) const
    {
        const int near = m_near[std::size_t(key(node))];
        return near != FREE && near != net;
    }

    // The room of copper of 'net' (or of what no net may touch, BLOCKED) on
    // 'layer': the nodes nearer than 'reach' millimetres to the line 'a'-'b',
    // and those nearer than 'reach' and SOFT_ROOM_MM, which are crowded.
    void mark(int layer, const QPointF &a, const QPointF &b, double reach, int net)
    {
        const double soft = reach + SOFT_ROOM_MM;
        const double step = nmToMm(m_step);
        const QPointF origin = toMm(m_origin);
        // The nodes of the rectangle round the line, on the grid.
        const auto low = [step](double a, double b, double from, double around) {
            return std::max(0, int(std::floor((std::min(a, b) - around - from) / step)));
        };
        const auto high = [step](double a, double b, double from, int count, double around) {
            return std::min(count - 1, int(std::ceil((std::max(a, b) + around - from) / step)));
        };
        const int columnFrom = low(a.x(), b.x(), origin.x(), soft);
        const int columnTo = high(a.x(), b.x(), origin.x(), m_columns, soft);
        const int rowFrom = low(a.y(), b.y(), origin.y(), soft);
        const int rowTo = high(a.y(), b.y(), origin.y(), m_rows, soft);
        for (int row = rowFrom; row <= rowTo; ++row) {
            for (int column = columnFrom; column <= columnTo; ++column) {
                const QPointF at = origin + QPointF(column * step, row * step);
                const double apart = QLineF(nearestOnPiece(a, b, at), at).length();
                const std::size_t index = std::size_t(key(GridNode{column, row, layer}));
                const auto take = [net](int &occupant) {
                    occupant =
                        net == BLOCKED || (occupant != FREE && occupant != net) ? BLOCKED : net;
                };
                if (apart < soft)
                    take(m_near[index]);
                if (apart < reach)
                    take(m_occupants[index]);
            }
        }
    }
    void markAll(const GridNode &node)
    {
        for (int layer = 0; layer < m_layers; ++layer)
            m_occupants[std::size_t(key(GridNode{node.column, node.row, layer}))] = BLOCKED;
    }

private:
    Point m_origin;
    Coord m_step;
    int m_columns;
    int m_rows;
    int m_layers;
    std::vector<int> m_occupants;
    // The same for the soft room round copper.
    std::vector<int> m_near;
};

class Router
{
public:
    // With 'links' a board with copper on one side gets wire links where a
    // track cannot go.
    Router(const Design &design, bool links)
        : m_design(design)
        , m_board(design.board)
        , m_links(links)
    {
        const Rules &rules = design.project.rules;
        Coord width = rules.defaultTrackWidth;
        Coord clearance = rules.clearance;
        for (const NetClass &netClass : design.project.netClasses) {
            width = std::max(width, netClass.trackWidth);
            clearance = std::max(clearance, netClass.clearance);
        }
        // Room for the widest track of all, kept from everything.
        m_room = nmToMm(width) / 2 + nmToMm(clearance);
        m_edgeRoom = nmToMm(width) / 2 + nmToMm(rules.edgeClearance);
        m_outline = outlinePath(m_board.outline);
        buildGrid();
    }

    Board board() const { return m_board; }
    // The step of the grid tracks are laid on.
    Coord step() const { return m_step; }

    // The board's outline holds the point, or there is no outline.
    bool onBoard(const Point &at) const
    {
        return m_board.outline.isEmpty() || m_outline.contains(toMm(at));
    }

    // Lays a track for 'line'; false when none is found.
    bool route(const RatsnestLine &line);

private:
    // The layer of 'side', -1 when the board has no copper there.
    int layerOf(BoardSide side) const
    {
        if (m_grid->layers() == 2)
            return side == BoardSide::Top ? 0 : 1;
        return side == m_singleSide ? 0 : -1;
    }
    BoardSide sideOf(int layer) const
    {
        if (m_grid->layers() == 2)
            return layer == 0 ? BoardSide::Top : BoardSide::Bottom;
        return m_singleSide;
    }
    int netIndex(const QString &net)
    {
        if (net.isEmpty())
            return BLOCKED;
        const auto found = m_nets.constFind(net);
        if (found != m_nets.constEnd())
            return *found;
        const int index = int(m_nets.size());
        m_nets.insert(net, index);
        return index;
    }
    void markTrack(const Track &track)
    {
        const int layer = layerOf(track.side);
        if (layer >= 0)
            m_grid->mark(layer, toMm(track.from), toMm(track.to),
                         nmToMm(track.width) / 2 + m_room, netIndex(track.net));
    }
    void markVia(const Via &via)
    {
        for (int layer = 0; layer < m_grid->layers(); ++layer)
            m_grid->mark(layer, toMm(via.at), toMm(via.at), nmToMm(via.size) / 2 + m_room,
                         netIndex(via.net));
    }
    void markLink(const WireLink &link)
    {
        for (const Point &end : {link.from, link.to}) {
            for (int layer = 0; layer < m_grid->layers(); ++layer)
                m_grid->mark(layer, toMm(end), toMm(end), nmToMm(link.size) / 2 + m_room,
                             netIndex(link.net));
        }
    }
    void buildGrid();

    const Design &m_design;
    Board m_board;
    Coord m_step = 1;
    bool m_links = true;
    double m_room = 0;
    double m_edgeRoom = 0;
    QPainterPath m_outline;
    BoardSide m_singleSide = BoardSide::Top;
    std::optional<RouteGrid> m_grid;
    QHash<QString, int> m_nets;
};

void Router::buildGrid()
{
    const Library &library = m_design.library;
    const Coord step = std::max<Coord>(m_design.project.boardGridStep / 2, 1);
    m_step = step;
    QRectF bounds = m_board.outline.isEmpty() ? boardBounds(m_board, library)
                                              : m_outline.boundingRect();
    if (m_board.outline.isEmpty()) {
        const double margin = nmToMm(NO_OUTLINE_MARGIN);
        bounds.adjust(-margin, -margin, margin, margin);
    }
    const Point origin{floorTo(mmToNm(bounds.left()), step), floorTo(mmToNm(bounds.top()), step)};
    const int columns = int((mmToNm(bounds.right()) - origin.x) / step) + 2;
    const int rows = int((mmToNm(bounds.bottom()) - origin.y) / step) + 2;
    const int layers = m_board.copper == CopperSides::Both ? 2 : 1;
    m_singleSide = m_board.copper == CopperSides::Bottom ? BoardSide::Bottom : BoardSide::Top;
    m_grid.emplace(origin, step, columns, rows, layers);

    // Off the board, or too near its edge.
    if (!m_board.outline.isEmpty()) {
        const QPolygonF edge = m_outline.toFillPolygon();
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const GridNode node{column, row, 0};
                const QPointF at = toMm(m_grid->point(node));
                bool clear = m_outline.contains(at);
                for (int i = 0; clear && i + 1 < edge.size(); ++i)
                    clear = QLineF(nearestOnPiece(edge.at(i), edge.at(i + 1), at), at).length()
                            >= m_edgeRoom;
                if (!clear)
                    m_grid->markAll(node);
            }
        }
    }
    // The copper there is, and the holes.
    for (const PadPoint &pad : padPoints(m_board, library)) {
        for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
            const int layer = layerOf(side);
            if (layer >= 0 && pad.on(side))
                m_grid->mark(layer, toMm(pad.axisFrom), toMm(pad.axisTo),
                             nmToMm(pad.radius) + m_room, netIndex(pad.net));
        }
    }
    for (const MountingHole &hole : m_board.holes) {
        for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
            const int layer = layerOf(side);
            if (layer >= 0 && !(hole.ring > 0 && holePadOn(m_board, hole, side)))
                m_grid->mark(layer, toMm(hole.at), toMm(hole.at), nmToMm(hole.drill) / 2 + m_room,
                             BLOCKED);
        }
    }
    for (const Track &track : m_board.tracks)
        markTrack(track);
    for (const Via &via : m_board.vias)
        markVia(via);
}

bool Router::route(const RatsnestLine &line)
{
    const Library &library = m_design.library;
    const QList<NetPad> pads = netPads(m_board, library);
    const QList<int> groups = copperRoots(m_board, pads);
    const auto padAt = [&pads, &line](const Point &at) {
        for (int i = 0; i < pads.size(); ++i) {
            if (pads.at(i).at == at && pads.at(i).net == line.net)
                return i;
        }
        return -1;
    };
    const int from = padAt(line.from);
    const int to = padAt(line.to);
    if (from < 0 || to < 0)
        return false;

    // The copper of the two groups the line joins: the way may start from
    // and end on any of it, the pads, the vias and the tracks laid, whatever
    // lies round it, so a connection goes on from a track there instead of
    // running beside it. Each node is tied to the point of copper it stands
    // for, the middle of a pad, or itself on a track.
    QList<GridNode> starts;
    QList<GridNode> goals;
    QHash<qint64, Point> own;
    const auto take = [&](int group, const Point &at, int layer, const Point &copper) {
        if (layer < 0 || (group != groups.at(from) && group != groups.at(to)))
            return;
        const GridNode node = m_grid->nearest(at, layer);
        if (!m_grid->contains(node) || own.contains(m_grid->key(node)))
            return;
        (group == groups.at(from) ? starts : goals).append(node);
        own.insert(m_grid->key(node), copper);
    };
    for (int i = 0; i < pads.size(); ++i) {
        if (pads.at(i).top)
            take(groups.at(i), pads.at(i).at, layerOf(BoardSide::Top), pads.at(i).at);
        if (pads.at(i).bottom)
            take(groups.at(i), pads.at(i).at, layerOf(BoardSide::Bottom), pads.at(i).at);
    }
    const int trackCount = int(m_board.tracks.size());
    for (int t = 0; t < trackCount; ++t) {
        const Track &track = m_board.tracks.at(t);
        const int group = groups.at(pads.size() + t);
        const int layer = layerOf(track.side);
        // Every node along a track on the grid, else its ends.
        const Point way = track.to - track.from;
        const Coord length = std::max(std::llabs(way.x), std::llabs(way.y));
        const bool onGrid = (way.x == 0 || way.y == 0 || std::llabs(way.x) == std::llabs(way.y))
                            && length % m_step == 0
                            && m_grid->point(m_grid->nearest(track.from, 0)) == track.from;
        const Coord count = onGrid && length > 0 ? length / m_step : 1;
        for (Coord k = 0; k <= count; ++k) {
            const Point at{track.from.x + way.x / count * k, track.from.y + way.y / count * k};
            take(group, at, layer, onGrid ? m_grid->point(m_grid->nearest(at, layer)) : at);
        }
    }
    for (int v = 0; v < m_board.vias.size(); ++v) {
        const int group = groups.at(pads.size() + trackCount + v);
        for (int layer = 0; layer < m_grid->layers(); ++layer)
            take(group, m_board.vias.at(v).at, layer, m_board.vias.at(v).at);
    }

    const int net = netIndex(line.net);
    const RouteGrid &grid = *m_grid;
    const auto open = [&grid, &own, net](const GridNode &node) {
        return own.contains(grid.key(node)) || grid.usable(node, net);
    };
    GridRouteRules rules;
    rules.columns = grid.columns();
    rules.rows = grid.rows();
    rules.layers = grid.layers();
    rules.diagonal = true;
    rules.straightCost = STRAIGHT_COST;
    rules.diagonalCost = DIAGONAL_COST;
    rules.bend45Cost = BEND45_COST;
    rules.bend90Cost = BEND90_COST;
    rules.stepCost = [&open, &grid, net](const GridNode &a, const GridNode &b) {
        if (!open(b))
            return -1;
        const int crowded = grid.crowded(b, net) ? CROWDED_COST : 0;
        // A slanted step only across a cell clear on all its corners.
        if (a.column != b.column && a.row != b.row
            && (!open(GridNode{b.column, a.row, a.layer})
                || !open(GridNode{a.column, b.row, a.layer})))
            return -1;
        if (grid.layers() == 2
            && ((b.layer == 0 && a.column == b.column) || (b.layer == 1 && a.row == b.row)))
            return crowded + ACROSS_GRAIN_COST;
        return crowded;
    };
    if (grid.layers() == 2) {
        // A via needs the node and those round it clear on both sides.
        rules.layerCost = [&grid, net](const GridNode &at, int) {
            for (int row = at.row - 1; row <= at.row + 1; ++row) {
                for (int column = at.column - 1; column <= at.column + 1; ++column) {
                    for (int layer = 0; layer < grid.layers(); ++layer) {
                        if (!grid.usable(GridNode{column, row, layer}, net))
                            return -1;
                    }
                }
            }
            return VIA_COST;
        };
    }
    // On a board of one side a way may leap what is in it by a wire link,
    // level or upright, between two pads with room round them, outside the
    // parts. Its wire lies on the side of the parts, clear of every pad and
    // hole but its own two, and of the other links.
    const Rules &linkRules = m_design.project.rules;
    const double linkPad = nmToMm(linkRules.linkSize) / 2;
    const double wireHalf = nmToMm(WIRE_LINK_WIRE) / 2;
    const double gap = nmToMm(linkRules.clearance);
    QList<QRectF> bodies;
    for (const Placement &placement : m_board.placements)
        bodies.append(placementRect(placement, library));
    const QList<PadPoint> padsThere = padPoints(m_board, library);
    const auto linkClear = [&](const Point &a, const Point &b) {
        const QPointF from = toMm(a);
        const QPointF to = toMm(b);
        const auto apart = [&from, &to](const QPointF &p, const QPointF &q) {
            return piecesApart(from, to, p, q);
        };
        // Its pads not under a part; its wire may pass under one raised over
        // the board, but not over a pad or a hole.
        for (const QRectF &body : std::as_const(bodies)) {
            if (body.contains(from) || body.contains(to))
                return false;
        }
        for (const PadPoint &pad : padsThere) {
            const QPointF p = toMm(pad.axisFrom);
            const QPointF q = toMm(pad.axisTo);
            const double radius = nmToMm(pad.radius);
            // Its pads clear of the pad, and its wire.
            for (const QPointF &end : {from, to}) {
                if (piecesApart(end, end, p, q) < radius + linkPad + gap)
                    return false;
            }
            if (apart(p, q) < radius + wireHalf + gap)
                return false;
        }
        for (const MountingHole &hole : m_board.holes) {
            const QPointF at = toMm(hole.at);
            if (apart(at, at) < nmToMm(holeOuterDiameter(hole)) / 2 + linkPad + gap)
                return false;
        }
        for (const WireLink &link : m_board.links) {
            if (apart(toMm(link.from), toMm(link.to)) < 2 * linkPad + gap)
                return false;
        }
        return true;
    };
    const auto padFits = [&grid, &open](const GridNode &at) {
        for (int row = at.row - 1; row <= at.row + 1; ++row) {
            for (int column = at.column - 1; column <= at.column + 1; ++column) {
                if (!open(GridNode{column, row, at.layer}))
                    return false;
            }
        }
        return true;
    };
    QList<GridNode> path = gridRoute(rules, starts, goals);
    if (path.size() < 2 && grid.layers() == 1 && m_links) {
        // Two links of one way a track apart: their pads and the room
        // between them, in steps.
        rules.jumpSpacing = int(std::ceil((2 * linkPad + gap) / nmToMm(m_step)));
        rules.jumps = [this, &grid, &padFits, &linkClear](const GridNode &at) {
            QList<GridJump> jumps;
            if (!padFits(at))
                return jumps;
            const Point from = grid.point(at);
            for (Coord length = LINK_SHORTEST; length <= LINK_LONGEST; length += LINK_PITCH) {
                if (length % m_step != 0)
                    continue;
                const int cells = int(length / m_step);
                for (const auto &[columns, rows] :
                     {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                    const GridNode to{at.column + columns * cells, at.row + rows * cells, at.layer};
                    if (!grid.contains(to) || !padFits(to) || !linkClear(from, grid.point(to)))
                        continue;
                    jumps.append(GridJump{to, LINK_COST + cells * STRAIGHT_COST});
                }
            }
            return jumps;
        };
        path = gridRoute(rules, starts, goals);
    }
    if (path.size() < 2)
        return false;
    // Where the way leaps: from a node to the next one not beside it.
    QHash<qint64, GridNode> leaps;
    for (int i = 1; i < path.size(); ++i) {
        const GridNode &a = path.at(i - 1);
        const GridNode &b = path.at(i);
        if (a.layer == b.layer
            && std::max(std::abs(a.column - b.column), std::abs(a.row - b.row)) > 1)
            leaps.insert(grid.key(a), b);
    }

    // The way as tracks and vias of the class of the net, from the middle of
    // the pad to the middle of the pad.
    const NetClass *netClass = trackClass(m_design, line.net);
    const Rules &boardRules = m_design.project.rules;
    const Coord width = netClass != nullptr ? netClass->trackWidth : boardRules.defaultTrackWidth;
    const auto layTrack = [&](const Point &a, const Point &b, int layer) {
        if (a == b)
            return;
        Track track;
        track.id = newObjectId();
        track.side = sideOf(layer);
        track.net = line.net;
        track.width = width;
        track.from = a;
        track.to = b;
        track.autorouted = true;
        m_board.tracks.append(track);
        markTrack(track);
    };
    const QList<GridNode> corners = gridCorners(path);
    layTrack(own.value(grid.key(corners.first())), grid.point(corners.first()),
             corners.first().layer);
    for (int i = 1; i < corners.size(); ++i) {
        const GridNode &a = corners.at(i - 1);
        const GridNode &b = corners.at(i);
        if (leaps.contains(grid.key(a)) && leaps.value(grid.key(a)) == b) {
            WireLink link;
            link.id = newObjectId();
            link.from = grid.point(a);
            link.to = grid.point(b);
            link.net = line.net;
            link.drill = boardRules.linkDrill;
            link.size = boardRules.linkSize;
            link.autorouted = true;
            m_board.links.append(link);
            markLink(link);
            continue;
        }
        if (a.layer == b.layer) {
            layTrack(grid.point(a), grid.point(b), b.layer);
            continue;
        }
        Via via;
        via.id = newObjectId();
        via.at = grid.point(b);
        via.net = line.net;
        via.drill = netClass != nullptr ? netClass->viaDrill : boardRules.viaDrill;
        via.size = netClass != nullptr ? netClass->viaSize : boardRules.viaSize;
        via.autorouted = true;
        m_board.vias.append(via);
        markVia(via);
    }
    layTrack(grid.point(corners.last()), own.value(grid.key(corners.last())),
             corners.last().layer);
    return true;
}

} // namespace

namespace {

// How often one connection may be laid by taking others up, and how many
// times that may be done in a run for each connection there was at first.
const int RIP_UPS_PER_LINE = 2;
const int RIP_UPS_PER_CONNECTION = 3;

QString lineKey(const RatsnestLine &line)
{
    return QStringLiteral("%1 %2 %3 %4 %5")
        .arg(line.net)
        .arg(line.from.x)
        .arg(line.from.y)
        .arg(line.to.x)
        .arg(line.to.y);
}

// The tracks and the vias of 'board' the autorouter laid and no hand has
// touched since: those it may take up again.
QSet<ObjectId> autoroutedOf(const Board &board)
{
    QSet<ObjectId> laid;
    for (const Track &track : board.tracks) {
        if (track.autorouted)
            laid.insert(track.id);
    }
    for (const Via &via : board.vias) {
        if (via.autorouted)
            laid.insert(via.id);
    }
    for (const WireLink &link : board.links) {
        if (link.autorouted)
            laid.insert(link.id);
    }
    return laid;
}

// 'line' laid on 'working', its board as 'current' has it, by taking up the
// connections the autorouter laid (autoroutedOf()) that are in its way: it is
// laid on the board without them, and those its way comes too near are taken
// up whole, from pad to pad or fork, to be laid again. Tracks laid by hand
// are never taken up. The board, or nothing when the line has no way even so.
std::optional<Board> ripUp(const Design &working, const Board &current, const RatsnestLine &line,
                           bool links)
{
    const QSet<ObjectId> laid = autoroutedOf(current);
    Design bare = working;
    bare.board = withoutTracks(current, laid);
    Router probe(bare, links);
    if (!probe.route(line))
        return std::nullopt;
    // The way found, put on the board as it is.
    Board with = current;
    QSet<ObjectId> way;
    for (const Track &track : probe.board().tracks) {
        if (findById(bare.board.tracks, track.id) == nullptr) {
            with.tracks.append(track);
            way.insert(track.id);
        }
    }
    QList<Via> wayVias;
    for (const Via &via : probe.board().vias) {
        if (findById(bare.board.vias, via.id) == nullptr) {
            with.vias.append(via);
            wayVias.append(via);
        }
    }
    // What of other nets the autorouter laid comes too near it: tracks, and
    // vias near its tracks or its vias near their tracks.
    QSet<ObjectId> others;
    for (const Track &track : current.tracks) {
        if (laid.contains(track.id) && track.net != line.net)
            others.insert(track.id);
    }
    QSet<ObjectId> inTheWay = shortingTracks(with, working, others);
    const NetClass *netClass = trackClass(working, line.net);
    const double clearance =
        nmToMm(netClass != nullptr ? netClass->clearance : working.project.rules.clearance);
    for (const Via &via : current.vias) {
        if (!laid.contains(via.id) || via.net == line.net)
            continue;
        for (const Track &track : with.tracks) {
            if (!way.contains(track.id))
                continue;
            const QPointF at = toMm(via.at);
            if (QLineF(nearestOnPiece(toMm(track.from), toMm(track.to), at), at).length()
                < nmToMm(track.width) / 2 + nmToMm(via.size) / 2 + clearance)
                inTheWay.insert(via.id);
        }
    }
    for (const Via &via : wayVias) {
        for (const Track &track : current.tracks) {
            const QPointF at = toMm(via.at);
            if (others.contains(track.id)
                && QLineF(nearestOnPiece(toMm(track.from), toMm(track.to), at), at).length()
                       < nmToMm(track.width) / 2 + nmToMm(via.size) / 2 + clearance)
                inTheWay.insert(track.id);
        }
    }
    if (inTheWay.isEmpty())
        return std::nullopt;
    // Taken up whole: each track with its run, each via with the runs of the
    // tracks on it, of what the autorouter laid.
    QSet<ObjectId> gone;
    for (const ObjectId &id : std::as_const(inTheWay)) {
        if (const Via *via = findById(with.vias, id)) {
            gone.insert(id);
            for (const Track &track : with.tracks) {
                if (others.contains(track.id) && (track.from == via->at || track.to == via->at))
                    gone.unite(trackRun(with, working.library, track.id));
            }
        } else {
            gone.unite(trackRun(with, working.library, id));
        }
    }
    gone.intersect(laid);
    // A wire link of the autorouter's goes with a track taken up from it.
    for (const WireLink &link : with.links) {
        if (!link.autorouted)
            continue;
        for (const Track &track : with.tracks) {
            if (gone.contains(track.id)
                && (track.from == link.from || track.to == link.from || track.from == link.to
                    || track.to == link.to))
                gone.insert(link.id);
        }
    }
    const Board after = withoutTracks(with, gone);
    if (!shortingTracks(after, working, way).isEmpty())
        return std::nullopt;
    return after;
}

} // namespace

AutorouteResult autorouted(const Design &design, const QSet<QString> &nets,
                           const std::function<bool(int done, int total)> &progress, bool links)
{
    // The board as laid so far is the router's; 'working' has it after each
    // connection taken up, to lay the rest on.
    Design working = design;
    std::optional<Router> router;
    router.emplace(working, links);
    AutorouteResult result;
    QList<RatsnestLine> failed;
    const QList<RatsnestLine> none;
    // The connections still to be laid, but for those in 'skipped'.
    const auto waiting = [&](const QList<RatsnestLine> &skipped) {
        QList<RatsnestLine> lines;
        for (const RatsnestLine &line : ratsnest(router->board(), design.library)) {
            if ((nets.isEmpty() || nets.contains(line.net)) && !skipped.contains(line)
                && router->onBoard(line.from) && router->onBoard(line.to))
                lines.append(line);
        }
        return lines;
    };
    const int total = int(waiting(none).size());
    result.total = total;
    QHash<QString, int> rippedFor;
    int ripUps = 0;
    // The board with the fewest connections that found no way when all were
    // tried: taking others up may first leave more, then fewer.
    std::optional<Board> best;
    int bestFailed = 0;
    for (;;) {
        const QList<RatsnestLine> lines = waiting(failed);
        if (progress
            && !progress(std::max(0, total - int(lines.size()) - int(failed.size())), total)) {
            result.stopped = true;
            break;
        }
        if (lines.isEmpty()) {
            // All tried: the best so far is kept, and one that found no way
            // is laid by taking up others.
            if (!best || int(failed.size()) < bestFailed) {
                best = router->board();
                bestFailed = int(failed.size());
            }
            bool laid = false;
            for (const RatsnestLine &line : std::as_const(failed)) {
                if (rippedFor.value(lineKey(line)) >= RIP_UPS_PER_LINE
                    || ripUps >= RIP_UPS_PER_CONNECTION * total)
                    continue;
                ++rippedFor[lineKey(line)];
                ++ripUps;
                const std::optional<Board> board =
                    ripUp(working, router->board(), line, links);
                if (!board)
                    continue;
                working.board = *board;
                router.emplace(working, links);
                // What failed may find a way now.
                failed.clear();
                laid = true;
                break;
            }
            if (!laid)
                break;
            continue;
        }
        // The shortest first.
        const auto length = [](const RatsnestLine &line) {
            return QLineF(toMm(line.from), toMm(line.to)).length();
        };
        const RatsnestLine line = *std::min_element(
            lines.cbegin(), lines.cend(),
            [&length](const RatsnestLine &a, const RatsnestLine &b) { return length(a) < length(b); });
        if (!router->route(line))
            failed.append(line);
    }
    // The best board, should taking others up have left more at the end.
    if (best && !result.stopped && int(failed.size()) > bestFailed) {
        working.board = *best;
        router.emplace(working, links);
    }
    // The tracks laid put in the middle of the room they run in.
    QSet<ObjectId> laid;
    for (const Track &track : router->board().tracks) {
        if (findById(design.board.tracks, track.id) == nullptr)
            laid.insert(track.id);
    }
    result.board = withTracksSpread(router->board(), design, router->step(), laid);
    // What is left to lay, however it is drawn now, and what was laid of
    // what there was.
    result.unrouted = int(waiting(none).size());
    result.routed = std::max(0, total - result.unrouted);
    result.vias = int(result.board.vias.size() - design.board.vias.size());
    result.links = int(result.board.links.size() - design.board.links.size());
    return result;
}

} // namespace ppcb
