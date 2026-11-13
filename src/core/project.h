#pragma once

#include "board.h"
#include "grid.h"
#include "schematic.h"

#include <QList>
#include <QMap>
#include <QString>

namespace ppcb {

// Extensions of the three files of a project, with the dot.
extern const char FILE_EXT_PROJECT[];
extern const char FILE_EXT_SCHEMATIC[];
extern const char FILE_EXT_BOARD[];

// Names of the two net classes a new project starts with: the class of an
// ordinary net and the class of the nets of power and ground symbols.
extern const char NET_CLASS_SIGNAL[];
extern const char NET_CLASS_POWER[];

// Fields of the title block shared by all the sheets. An empty field is not
// printed; an empty date stands for the date of the last save.
struct TitleBlock
{
    QString title;
    QString author;
    QString organization;
    QString contacts;
    QString revision;
    QString date;
    QString note;
};

// Which of a part's two designator prefixes is used: R, D, Q, U or R, VD, VT, DD.
enum class DesignatorStyle { International, Gost };

// A kind of track: what the nets of the class are routed with.
struct NetClass
{
    QString name;
    Coord trackWidth = 0;
    Coord clearance = 0;
    // Diameters of the hole and of the copper of a via.
    Coord viaDrill = 0;
    Coord viaSize = 0;
};

// The set of values the rules were last filled from.
enum class RuleSet { Production, Lut };

// What a broken rule is reported as.
enum class CheckLevel { Error, Warning, Off };

// Routing rules of the project. Copper of different nets that touches is
// always an error and has no level here.
struct Rules
{
    RuleSet set = RuleSet::Production;
    // Track widths offered for routing, ascending. The smallest is also the
    // narrowest track the rules allow.
    QList<Coord> trackWidths;
    // One of trackWidths.
    Coord defaultTrackWidth = 0;
    // Copper to copper of another net.
    Coord clearance = 0;
    // A new via: diameters of the hole and of the copper.
    Coord viaDrill = 0;
    Coord viaSize = 0;
    // The least copper around the hole of a pad.
    Coord annularRing = 0;
    // Copper to the edge of the board.
    Coord edgeClearance = 0;
    // How far the opening in the solder mask reaches beyond a pad.
    Coord maskExpansion = 0;
    // Vias are under the solder mask.
    bool viasCovered = true;

    CheckLevel clearanceLevel = CheckLevel::Error;
    CheckLevel annularRingLevel = CheckLevel::Warning;
    // A track narrower than its net class asks for.
    CheckLevel classWidthLevel = CheckLevel::Warning;
    // A track narrower than the smallest of trackWidths.
    CheckLevel minimumWidthLevel = CheckLevel::Error;
    CheckLevel edgeClearanceLevel = CheckLevel::Warning;
};

// The rules of the two sets: for a board ordered from a factory and for one
// made at home by toner transfer.
Rules productionRules();
Rules lutRules();

// The content of a project file (.ppcb).
struct Project
{
    // Names of the schematic and the board files. They lie in the folder of
    // the project file, so these are names without a folder.
    QString schematicFile;
    QString boardFile;

    TitleBlock titleBlock;
    DesignatorStyle designatorStyle = DesignatorStyle::International;

    // At least one in a project read from a file; names do not repeat.
    QList<NetClass> netClasses;
    // Class of the nets it was chosen for in the table of nets, by net name.
    // A net that is not here takes its class from a label or the default one.
    QMap<QString, QString> classOfNet;

    Rules rules;

    Coord schematicGridStep = DEFAULT_GRID_STEP;
    Coord boardGridStep = DEFAULT_GRID_STEP;
};

bool operator==(const TitleBlock &a, const TitleBlock &b);
bool operator==(const NetClass &a, const NetClass &b);
bool operator==(const Rules &a, const Rules &b);
bool operator==(const Project &a, const Project &b);
inline bool operator!=(const Rules &a, const Rules &b) { return !(a == b); }
inline bool operator!=(const Project &a, const Project &b) { return !(a == b); }

QJsonObject projectToJson(const Project &project);
// Reads the body of a project file (the header is checked by the caller).
// After a failure (see reader.ok()) the result is not meaningful.
Project projectFromJson(const QJsonObject &root, JsonReader &reader);

// Project files (.ppcb) alone, without the schematic and the board. On
// failure both return false and put a message for the user into 'error';
// read() changes 'project' only on success.
class ProjectFile
{
public:
    static bool read(const QString &path, Project &project, QString &error);
    static bool write(const QString &path, const Project &project, QString &error);
};

// Everything of one project: the content of its three files.
struct Design
{
    Project project;
    Schematic schematic;
    Board board;
};

bool operator==(const Design &a, const Design &b);
inline bool operator!=(const Design &a, const Design &b) { return !(a == b); }

// A new project whose files are named after 'baseName' ("amp" gives
// amp.ppsch and amp.ppbrd): the production rules, the two starting net
// classes, a schematic of one empty sheet and an empty board.
Design newDesign(const QString &baseName);

// The three files together. 'projectPath' is the .ppcb file; the other two
// are found beside it by the names it holds. On failure both return false
// and put a message that names the file into 'error'.
class DesignFiles
{
public:
    // Where the schematic and the board of the project file 'projectPath' lie.
    static QString schematicPath(const QString &projectPath, const Project &project);
    static QString boardPath(const QString &projectPath, const Project &project);

    // Changes 'design' only when all three files were read.
    static bool read(const QString &projectPath, Design &design, QString &error);
    // Writes the schematic and the board first and the project last. A
    // failure may leave the files written before it already replaced.
    static bool write(const QString &projectPath, const Design &design, QString &error);
};

} // namespace ppcb
