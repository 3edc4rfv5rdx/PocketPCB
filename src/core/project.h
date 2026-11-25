#pragma once

#include "board.h"
#include "grid.h"
#include "library.h"
#include "schematic.h"

#include <QList>
#include <QMap>
#include <QString>

namespace ppcb {

// Extensions of the three files of a project, with the dot.
extern const char FILE_EXT_PROJECT[];
extern const char FILE_EXT_SCHEMATIC[];
extern const char FILE_EXT_BOARD[];
// The folder of the project library, beside the project file.
extern const char PROJECT_LIBRARY_FOLDER[];
// The folder of the backup copies of the project's files, beside it too.
extern const char PROJECT_BACKUP_FOLDER[];
// The folder of the history of the project's steps (writeStep()), beside it
// too.
extern const char PROJECT_HISTORY_FOLDER[];
// The folder of the autosaved copy of the project, beside it too.
extern const char PROJECT_AUTOSAVE_FOLDER[];
// The folder of the files made from the project, beside it too.
extern const char PROJECT_OUTPUT_FOLDER[];
// The folder of the versions of the project, beside it too.
extern const char PROJECT_VERSIONS_FOLDER[];

// Names of the two net classes a new project starts with, which are also its
// default class and its power class (see Project).
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

// A pin of a component of the schematic, by the component's id, which no
// renumbering changes.
struct PinRef
{
    ObjectId component;
    QString number;
};

bool operator==(const PinRef &a, const PinRef &b);

// A net with no name of its own that was given a class in the table of
// nets, or a name by updating the board (withNetsNamedByPins()): the name
// it got ("NN1") and the pins it had then. The net
// holding the most of these pins goes by that name (see nets()).
struct NetByPins
{
    QString name;
    QList<PinRef> pins;
};

bool operator==(const NetByPins &a, const NetByPins &b);

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
    // A new wire link: diameters of its holes and of its pads.
    Coord linkDrill = 0;
    Coord linkSize = 0;
    // The least copper around the hole of a pad.
    Coord annularRing = 0;
    // Copper to the edge of the board.
    Coord edgeClearance = 0;
    // How far the opening in the solder mask reaches beyond a pad.
    Coord maskExpansion = 0;
    // Vias are under the solder mask.
    bool viasCovered = true;
    // The drills there are, ascending: the diameters offered for holes.
    QList<Coord> drills;
    // Only these drills may be used: holes are drilled with the nearest one
    // not smaller, and the rules check warns of a diameter not among them.
    bool drillsOnly = false;

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
    // Names of two of them: the class of a net nothing else gives one, and
    // the class of the nets of power and ground symbols. They may be the same.
    QString defaultClass;
    QString powerClass;
    // Class of the nets it was chosen for in the table of nets, by net name,
    // or by the name given in 'netsByPins'. A net that is not here takes its
    // class from a label or the default one (see netClassOf()).
    QMap<QString, QString> classOfNet;
    // Names given in the table of nets, or by updating the board, to nets
    // with no name of their own; the names do not repeat.
    QList<NetByPins> netsByPins;

    Rules rules;

    Coord schematicGridStep = DEFAULT_GRID_STEP;
    Coord boardGridStep = DEFAULT_BOARD_GRID_STEP;
};

// The revision after 'revision', trimmed: its last number one more, keeping
// its width ("9" to "10", "1.09" to "1.10"), or its last Latin letters
// stepped as a column name is ("A" to "B", "AZ" to "BA", case kept); "1"
// after an empty one, and "1" added after anything else ("rev-" to
// "rev-1").
QString nextRevision(const QString &revision);

// "-revB" for revision "B" in the name of a file made from the project:
// trimmed, characters other than letters, digits, '.', '-' and '_' made
// '_'; nothing for an empty revision.
QString revisionInFileName(const QString &revision);

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

// Everything of one project: the content of its three files and of its
// library folder.
struct Design
{
    Project project;
    Schematic schematic;
    Board board;
    Library library;
};

bool operator==(const Design &a, const Design &b);
inline bool operator!=(const Design &a, const Design &b) { return !(a == b); }

// A new project whose files are named after 'baseName' ("amp" gives
// amp.ppsch and amp.ppbrd): the production rules, the two starting net
// classes, a schematic of one empty sheet and an empty board.
Design newDesign(const QString &baseName);

// Messages for the user about what the schematic and the board use that
// the project library does not have: a part, the symbol of a part or of a
// power symbol, a footprint. Each names the missing file, as "library/parts/diode.json".
QStringList missingLibraryItems(const Design &design);

// The highest number the names of the files in 'folder' matching
// 'nameFilter' ("*.zip") start with, the number ending at 'separator'; 0
// for none.
int highestFileNumber(const QString &folder, const QString &nameFilter, QChar separator);

// The three files and the library folder together. 'projectPath' is the
// .ppcb file; the other two files are found beside it by the names it
// holds, the library folder beside it as well. On failure both return false
// and put a message that names the file into 'error'.
class DesignFiles
{
public:
    // Where the schematic, the board and the library folder of the project
    // file 'projectPath' lie.
    static QString schematicPath(const QString &projectPath, const Project &project);
    static QString boardPath(const QString &projectPath, const Project &project);
    static QString libraryPath(const QString &projectPath);
    static QString backupPath(const QString &projectPath);
    // Where the history of the project's steps goes (writeStep()).
    static QString historyPath(const QString &projectPath);
    // Where the files made from the project go: the BOM, later Gerber.
    static QString outputPath(const QString &projectPath);
    // Where the versions of the project go (projectversions.h).
    static QString versionsPath(const QString &projectPath);
    // The project file of the autosaved copy: the project's own name inside
    // the autosave folder. The copy is a whole project of its own there.
    static QString autosaveProjectPath(const QString &projectPath);

    // Changes 'design' only when all three files were read. The library does
    // not stop a project from opening: library files that cannot be read and
    // items the project uses but the library lacks go into 'problems', when
    // given, as messages for the user.
    static bool read(const QString &projectPath, Design &design, QString &error,
                     QStringList *problems = nullptr);
    // Writes the library first, then the schematic and the board, and the
    // project last. A failure may leave the files written before it already
    // replaced.
    static bool write(const QString &projectPath, const Design &design, QString &error);
};

} // namespace ppcb
