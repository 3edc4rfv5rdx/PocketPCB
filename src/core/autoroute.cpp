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

    // The room of copper of 'net' (or of what no net may touch, BLOCKED) on
    // 'layer': the nodes nearer than 'reach' millimetres to the line 'a'-'b'.
    void mark(int layer, const QPointF &a, const QPointF &b, double reach, int net)
    {
        const double step = nmToMm(m_step);
        const QPointF origin = toMm(m_origin);
        // The nodes of the rectangle round the line, on the grid.
        const auto low = [step, reach](double a, double b, double from) {
            return std::max(0, int(std::floor((std::min(a, b) - reach - from) / step)));
        };
        const auto high = [step, reach](double a, double b, double from, int count) {
            return std::min(count - 1, int(std::ceil((std::max(a, b) + reach - from) / step)));
        };
        const int columnFrom = low(a.x(), b.x(), origin.x());
        const int columnTo = high(a.x(), b.x(), origin.x(), m_columns);
        const int rowFrom = low(a.y(), b.y(), origin.y());
        const int rowTo = high(a.y(), b.y(), origin.y(), m_rows);
        for (int row = rowFrom; row <= rowTo; ++row) {
            for (int column = columnFrom; column <= columnTo; ++column) {
                const QPointF at = origin + QPointF(column * step, row * step);
                if (QLineF(nearestOnPiece(a, b, at), at).length() >= reach)
                    continue;
                int &occupant = m_occupants[std::size_t(key(GridNode{column, row, layer}))];
                occupant = net == BLOCKED || (occupant != FREE && occupant != net) ? BLOCKED : net;
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
};

class Router
{
public:
    explicit Router(const Design &design) : m_design(design), m_board(design.board)
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
    void buildGrid();

    const Design &m_design;
    Board m_board;
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
    const QList<int> groups = copperGroups(m_board, pads);
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

    // The nodes of the pads of the two groups of copper, which the way may
    // start from and end on whatever lies round them, and the pads' middles.
    QList<GridNode> starts;
    QList<GridNode> goals;
    QHash<qint64, Point> own;
    for (int i = 0; i < pads.size(); ++i) {
        if (groups.at(i) != groups.at(from) && groups.at(i) != groups.at(to))
            continue;
        for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
            const int layer = layerOf(side);
            if (layer < 0 || !(side == BoardSide::Top ? pads.at(i).top : pads.at(i).bottom))
                continue;
            const GridNode node = m_grid->nearest(pads.at(i).at, layer);
            if (!m_grid->contains(node))
                continue;
            (groups.at(i) == groups.at(from) ? starts : goals).append(node);
            own.insert(m_grid->key(node), pads.at(i).at);
        }
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
    rules.stepCost = [&open, &grid](const GridNode &a, const GridNode &b) {
        if (!open(b))
            return -1;
        // A slanted step only across a cell clear on all its corners.
        if (a.column != b.column && a.row != b.row
            && (!open(GridNode{b.column, a.row, a.layer})
                || !open(GridNode{a.column, b.row, a.layer})))
            return -1;
        if (grid.layers() == 2
            && ((b.layer == 0 && a.column == b.column) || (b.layer == 1 && a.row == b.row)))
            return ACROSS_GRAIN_COST;
        return 0;
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
    const QList<GridNode> path = gridRoute(rules, starts, goals);
    if (path.size() < 2)
        return false;

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
        m_board.tracks.append(track);
        markTrack(track);
    };
    const QList<GridNode> corners = gridCorners(path);
    layTrack(own.value(grid.key(corners.first())), grid.point(corners.first()),
             corners.first().layer);
    for (int i = 1; i < corners.size(); ++i) {
        const GridNode &a = corners.at(i - 1);
        const GridNode &b = corners.at(i);
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
        m_board.vias.append(via);
        markVia(via);
    }
    layTrack(grid.point(corners.last()), own.value(grid.key(corners.last())),
             corners.last().layer);
    return true;
}

} // namespace

AutorouteResult autorouted(const Design &design, const QSet<QString> &nets,
                           const std::function<bool(int done, int total)> &progress)
{
    Router router(design);
    AutorouteResult result;
    QList<RatsnestLine> failed;
    const QList<RatsnestLine> none;
    // The connections still to be laid, but for those in 'skipped'.
    const auto waiting = [&](const QList<RatsnestLine> &skipped) {
        QList<RatsnestLine> lines;
        for (const RatsnestLine &line : ratsnest(router.board(), design.library)) {
            if ((nets.isEmpty() || nets.contains(line.net)) && !skipped.contains(line)
                && router.onBoard(line.from) && router.onBoard(line.to))
                lines.append(line);
        }
        return lines;
    };
    const int total = int(waiting(none).size());
    result.total = total;
    for (QList<RatsnestLine> lines = waiting(failed); !lines.isEmpty(); lines = waiting(failed)) {
        if (progress && !progress(result.routed + int(failed.size()), total)) {
            result.stopped = true;
            break;
        }
        // The shortest first.
        const auto length = [](const RatsnestLine &line) {
            return QLineF(toMm(line.from), toMm(line.to)).length();
        };
        const RatsnestLine line = *std::min_element(
            lines.cbegin(), lines.cend(),
            [&length](const RatsnestLine &a, const RatsnestLine &b) { return length(a) < length(b); });
        if (router.route(line))
            ++result.routed;
        else
            failed.append(line);
    }
    result.board = router.board();
    // What is left to lay, however it is drawn now.
    result.unrouted = int(waiting(none).size());
    result.vias = int(result.board.vias.size() - design.board.vias.size());
    return result;
}

} // namespace ppcb
