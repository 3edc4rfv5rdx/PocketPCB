#pragma once

#include "footprint.h"
#include "graphic.h"
#include "objectid.h"

#include <QList>
#include <QMap>

namespace ppcb {

// A side of the board.
enum class BoardSide { Top, Bottom };

// The layers of the board: each side has its copper, solder mask, silkscreen
// and solder paste (for parts on the surface, later); the outline is one
// for both. What is shown on the screen goes by layer.
enum class BoardLayer {
    CopperTop,
    CopperBottom,
    MaskTop,
    MaskBottom,
    SilkscreenTop,
    SilkscreenBottom,
    PasteTop,
    PasteBottom,
    Outline,
};
constexpr int BOARD_LAYER_COUNT = int(BoardLayer::Outline) + 1;

BoardLayer copperLayer(BoardSide side);
BoardLayer maskLayer(BoardSide side);
BoardLayer silkscreenLayer(BoardSide side);
BoardLayer pasteLayer(BoardSide side);

// A set of layers, such as those shown.
class BoardLayerSet
{
public:
    // Every layer.
    static BoardLayerSet all() { return BoardLayerSet((1u << BOARD_LAYER_COUNT) - 1); }
    BoardLayerSet() = default;

    bool contains(BoardLayer layer) const { return (m_bits & bit(layer)) != 0; }
    void set(BoardLayer layer, bool on)
    {
        m_bits = on ? (m_bits | bit(layer)) : (m_bits & ~bit(layer));
    }
    bool operator==(const BoardLayerSet &other) const { return m_bits == other.m_bits; }
    bool operator!=(const BoardLayerSet &other) const { return m_bits != other.m_bits; }

private:
    explicit BoardLayerSet(quint32 bits) : m_bits(bits) {}
    static quint32 bit(BoardLayer layer) { return 1u << int(layer); }

    quint32 m_bits = 0;
};

// A corner of the board outline. A radius greater than zero rounds it.
struct OutlineCorner
{
    Point at;
    Coord radius = 0;
};

// A footprint placed on the board for one component of the schematic.
struct Placement
{
    ObjectId id;
    // The schematic component it stands for. Renumbering the schematic does
    // not break this tie.
    ObjectId component;
    // Name of its footprint in the project library (see Library).
    QString footprint;
    // Copied from the component when the board is updated from the schematic.
    QString designator;
    QString value;

    // Position of the footprint's origin. On the bottom side the footprint
    // is flipped left to right first; then it is turned by 'rotation': 0, 90,
    // 180 or 270 degrees clockwise as seen from the top.
    Point at;
    int rotation = 0;
    BoardSide side = BoardSide::Top;
    // Where the designator and the value are drawn, relative to 'at', and
    // whether they go to the silkscreen.
    Point refAt;
    Point valueAt;
    bool showRef = true;
    bool showValue = false;
    // A line on the silkscreen joins the designator, or the value, to the
    // part, when that text is shown (see leaderLine() in paint.h).
    bool refLine = false;
    bool valueLine = false;
    // It may not be moved or turned until this is taken off.
    bool locked = false;

    // Net of every connected pad, by pad number. A pad that is not here
    // belongs to no net.
    QMap<QString, QString> padNets;
};

// One straight piece of a track, with round ends. An empty net: not
// connected to anything of the schematic.
struct Track
{
    ObjectId id;
    BoardSide side = BoardSide::Top;
    QString net;
    Coord width = 0;
    Point from;
    Point to;
    // Laid by the autorouter and not touched by hand since: the autorouter
    // may take it up again. Not written when false.
    bool autorouted = false;
};

// A via: a round pad with a hole, on both copper layers.
struct Via
{
    ObjectId id;
    Point at;
    QString net;
    // Diameters of the hole and of the copper.
    Coord drill = 0;
    Coord size = 0;
    // As Track::autorouted.
    bool autorouted = false;
};

// Where copper is: on both sides, or on one; of the board, or of the pad
// of a mounting hole.
enum class CopperSides { Both, Top, Bottom };

// A mounting hole. It may have a copper ring around it, a pad on 'padSides',
// which may be given a net; one without a ring has no net. Plating, for a
// board from a factory, needs the ring on both sides; a board made at home
// has a ring without plating.
struct MountingHole
{
    ObjectId id;
    Point at;
    // Diameter.
    Coord drill = 0;
    bool plated = false;
    // Width of the copper ring.
    Coord ring = 0;
    CopperSides padSides = CopperSides::Both;
    QString net;
    // It may not be moved, changed or deleted until this is taken off.
    bool locked = false;
};

// The diameter a hole takes on the board: of its copper ring, or of the
// hole itself without one.
Coord holeOuterDiameter(const MountingHole &hole);


// The author's own drawing on the silkscreen of one side: text and lines.
struct BoardGraphic
{
    ObjectId id;
    BoardSide side = BoardSide::Top;
    Graphic graphic;
};

// A copper pour. Only kept in the file for now: nothing fills it yet.
struct Zone
{
    ObjectId id;
    BoardSide side = BoardSide::Top;
    QString net;
    // Gap between the pour and the copper of other nets.
    Coord clearance = 0;
    // Corners of the outline, at least three; the last is joined to the first.
    QList<Point> points;
};

// The board of a project.
struct Board
{
    // The sides of the board with copper: both, or one.
    CopperSides copper = CopperSides::Both;
    // The board's edge: a closed chain of corners (see outline.h). Empty
    // while the board has no outline yet.
    QList<OutlineCorner> outline;
    // The outline may not be changed or moved until this is taken off.
    bool outlineLocked = false;
    QList<Placement> placements;
    QList<Track> tracks;
    QList<Via> vias;
    QList<MountingHole> holes;
    QList<BoardGraphic> graphics;
    QList<Zone> zones;
};

bool operator==(const OutlineCorner &a, const OutlineCorner &b);
bool operator==(const Placement &a, const Placement &b);
bool operator==(const Track &a, const Track &b);
bool operator==(const Via &a, const Via &b);
bool operator==(const MountingHole &a, const MountingHole &b);
bool operator==(const BoardGraphic &a, const BoardGraphic &b);
bool operator==(const Zone &a, const Zone &b);
bool operator==(const Board &a, const Board &b);

// True when 'sides' hold 'side'.
bool copperOn(CopperSides sides, BoardSide side);
// The sides a pad of a footprint has copper on, once its part is placed on
// 'partSide': a pad's Front is the side the part is on.
CopperSides padCopperSides(PadLayers layers, BoardSide partSide);
// True when the pad of 'hole' on 'board' has copper on 'side': it has a
// ring, on that side or on both; on a board of one side, on that one.
bool holePadOn(const Board &board, const MountingHole &hole, BoardSide side);
inline bool operator!=(const Board &a, const Board &b) { return !(a == b); }

QJsonObject boardToJson(const Board &board);
// Reads the body of a board file (the header is checked by the caller).
// After a failure (see reader.ok()) the result is not meaningful.
Board boardFromJson(const QJsonObject &root, JsonReader &reader);

// Board files (.ppbrd). On failure both return false and put a message for
// the user into 'error'; read() changes 'board' only on success.
class BoardFile
{
public:
    static bool read(const QString &path, Board &board, QString &error);
    static bool write(const QString &path, const Board &board, QString &error);
};

} // namespace ppcb
