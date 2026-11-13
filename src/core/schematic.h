#pragma once

#include "graphic.h"
#include "objectid.h"
#include "symbol.h"

#include <QList>
#include <QMap>

namespace ppcb {

// A part placed on a sheet. What it needs from the library is copied in, so
// the schematic opens the same whether or not the library still has the part.
struct Component
{
    ObjectId id;
    // Library name of the part it was placed from.
    QString part;
    // Name of its symbol in Schematic::symbols.
    QString symbol;
    // Library name of the chosen footprint; empty when it has none.
    QString footprint;
    // Reference designator ("R1") and value ("10k").
    QString designator;
    QString value;
    // Designator prefixes of the part in both styles, for switching the style.
    QString prefix;
    QString prefixGost;
    // A connector or wire pad (used by ERC).
    bool connector = false;
    // Not on the board: mounted on the panel, on a heatsink and so on.
    bool offBoard = false;

    // Position of the symbol's origin; rotation is 0, 90, 180 or 270 degrees
    // clockwise on screen; mirroring flips the symbol left to right first.
    Point at;
    int rotation = 0;
    bool mirrored = false;
    // Where the designator and the value are drawn, relative to 'at'.
    Point refAt;
    Point valueAt;
};

// A wire: a chain of points; every segment is horizontal, vertical or at 45 degrees.
struct Wire
{
    ObjectId id;
    QList<Point> points;
};

enum class LabelScope { Global, Local };

// A net label: wires with the same label are one net.
struct Label
{
    ObjectId id;
    Point at;
    QString text;
    LabelScope scope = LabelScope::Global;
    int rotation = 0;
    // Net class given to the net through this label; empty: not given here.
    QString netClass;
};

// A power or ground symbol (GND, +5V): all with one name are one net.
struct PowerSymbol
{
    ObjectId id;
    Point at;
    QString name;
    int rotation = 0;
};

// The cross on a pin that is left unconnected on purpose.
struct NoConnect
{
    ObjectId id;
    Point at;
};

// Free drawing on a sheet: comments, frames around blocks. Connects nothing.
struct SheetGraphic
{
    ObjectId id;
    Graphic graphic;
};

enum class PaperSize { A4, A3 };

struct Sheet
{
    ObjectId id;
    QString title;
    PaperSize paper = PaperSize::A4;
    QList<Component> components;
    QList<Wire> wires;
    QList<Label> labels;
    QList<PowerSymbol> powerSymbols;
    QList<NoConnect> noConnects;
    QList<SheetGraphic> graphics;
};

// The whole schematic of a project: its sheets and the symbols they use.
struct Schematic
{
    // Symbols copied from the library, by library name. Shared by all the
    // components placed from the same part.
    QMap<QString, Symbol> symbols;
    // At least one in a schematic read from a file.
    QList<Sheet> sheets;
};

bool operator==(const Component &a, const Component &b);
bool operator==(const Wire &a, const Wire &b);
bool operator==(const Label &a, const Label &b);
bool operator==(const PowerSymbol &a, const PowerSymbol &b);
bool operator==(const NoConnect &a, const NoConnect &b);
bool operator==(const SheetGraphic &a, const SheetGraphic &b);
bool operator==(const Sheet &a, const Sheet &b);
bool operator==(const Schematic &a, const Schematic &b);
inline bool operator!=(const Schematic &a, const Schematic &b) { return !(a == b); }

QJsonObject schematicToJson(const Schematic &schematic);
// Reads the body of a schematic file (the header is checked by the caller).
// After a failure (see reader.ok()) the result is not meaningful.
Schematic schematicFromJson(const QJsonObject &root, JsonReader &reader);

// Schematic files (.ppsch). On failure both return false and put a message
// for the user into 'error'; read() changes 'schematic' only on success.
class SchematicFile
{
public:
    static bool read(const QString &path, Schematic &schematic, QString &error);
    static bool write(const QString &path, const Schematic &schematic, QString &error);
};

} // namespace ppcb
