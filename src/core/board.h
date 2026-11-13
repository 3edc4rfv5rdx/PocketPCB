#pragma once

#include "footprint.h"
#include "graphic.h"
#include "objectid.h"

#include <QList>
#include <QMap>

namespace ppcb {

// A side of the board; also the copper or the silkscreen layer on that side.
enum class BoardSide { Top, Bottom };

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
    // Name of its footprint in Board::footprints.
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
    // Footprints copied from the library, by library name. Shared by all the
    // placements made with the same footprint.
    QMap<QString, Footprint> footprints;
    // The board's edge: a closed chain of corners, every side horizontal,
    // vertical or at 45 degrees. Empty while the board has no outline yet.
    QList<OutlineCorner> outline;
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
