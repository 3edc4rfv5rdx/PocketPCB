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
};

// A mounting hole. A plated one has a copper ring around it, which may be
// given a net; one without plating has neither.
struct MountingHole
{
    ObjectId id;
    Point at;
    // Diameter.
    Coord drill = 0;
    bool plated = false;
    // Width of the copper ring.
    Coord ring = 0;
    QString net;
};

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
