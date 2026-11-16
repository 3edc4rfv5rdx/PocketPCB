#include "project.h"

#include <QDir>
#include <QFileInfo>

namespace ppcb {

const char FILE_EXT_PROJECT[] = ".ppcb";
const char FILE_EXT_SCHEMATIC[] = ".ppsch";
const char FILE_EXT_BOARD[] = ".ppbrd";
const char PROJECT_LIBRARY_FOLDER[] = "library";
const char PROJECT_BACKUP_FOLDER[] = "backup";
const char PROJECT_AUTOSAVE_FOLDER[] = "autosave";

const char NET_CLASS_SIGNAL[] = "Signal";
const char NET_CLASS_POWER[] = "Power";

namespace {

const char KEY_SCHEMATIC[] = "schematic";
const char KEY_BOARD[] = "board";
const char KEY_TITLE_BLOCK[] = "titleBlock";
const char KEY_DESIGNATOR_STYLE[] = "designatorStyle";
const char KEY_NET_CLASSES[] = "netClasses";
const char KEY_DEFAULT_CLASS[] = "defaultClass";
const char KEY_POWER_CLASS[] = "powerClass";
const char KEY_NETS[] = "nets";
const char KEY_NETS_BY_PINS[] = "netsByPins";
const char KEY_RULES[] = "rules";
const char KEY_GRID[] = "grid";

const char KEY_TITLE[] = "title";
const char KEY_AUTHOR[] = "author";
const char KEY_ORGANIZATION[] = "organization";
const char KEY_CONTACTS[] = "contacts";
const char KEY_REVISION[] = "revision";
const char KEY_DATE[] = "date";
const char KEY_NOTE[] = "note";

const char KEY_NAME[] = "name";
const char KEY_PINS[] = "pins";
const char KEY_COMPONENT[] = "component";
const char KEY_PIN[] = "pin";
const char KEY_TRACK_WIDTH[] = "trackWidth";
const char KEY_CLEARANCE[] = "clearance";
const char KEY_VIA_DRILL[] = "viaDrill";
const char KEY_VIA_SIZE[] = "viaSize";

const char KEY_SET[] = "set";
const char KEY_TRACK_WIDTHS[] = "trackWidths";
const char KEY_DEFAULT_TRACK_WIDTH[] = "defaultTrackWidth";
const char KEY_ANNULAR_RING[] = "annularRing";
const char KEY_EDGE_CLEARANCE[] = "edgeClearance";
const char KEY_MASK_EXPANSION[] = "maskExpansion";
const char KEY_VIAS_COVERED[] = "viasCovered";
const char KEY_CHECKS[] = "checks";
const char KEY_CLASS_WIDTH[] = "classWidth";
const char KEY_MINIMUM_WIDTH[] = "minimumWidth";

// Sizes of the two rule sets and of the starting net classes, in nanometres.
const Coord PRODUCTION_DEFAULT_WIDTH = 500000;
const Coord PRODUCTION_CLEARANCE = 300000;
const Coord PRODUCTION_VIA_DRILL = 600000;
const Coord PRODUCTION_VIA_SIZE = 1200000;
const Coord PRODUCTION_ANNULAR_RING = 300000;
const Coord PRODUCTION_EDGE_CLEARANCE = 500000;
const Coord LUT_DEFAULT_WIDTH = 800000;
const Coord LUT_CLEARANCE = 500000;
const Coord LUT_VIA_DRILL = 800000;
const Coord LUT_VIA_SIZE = 2000000;
const Coord LUT_ANNULAR_RING = 600000;
const Coord LUT_EDGE_CLEARANCE = 1000000;
const Coord MASK_EXPANSION = 100000;
const Coord POWER_TRACK_WIDTH = 1500000;

// In the order of DesignatorStyle.
const QStringList &styleNames()
{
    static const QStringList names = {QStringLiteral("international"), QStringLiteral("gost")};
    return names;
}

// In the order of RuleSet.
const QStringList &ruleSetNames()
{
    static const QStringList names = {QStringLiteral("production"), QStringLiteral("lut")};
    return names;
}

// In the order of CheckLevel.
const QStringList &levelNames()
{
    static const QStringList names = {
        QStringLiteral("error"), QStringLiteral("warning"), QStringLiteral("off"),
    };
    return names;
}

// What reading needs to know about the file as a whole.
struct ReadContext
{
    JsonReader &reader;
    // Names of the net classes read so far.
    QStringList classNames;
};

// A name of a file in the project's folder: no folder in it, and the
// extension of its kind (letter case does not matter).
QString fileNameFromJson(const QJsonObject &object, const char *key, const char *extension,
                         JsonReader &reader)
{
    const QString name = reader.nonEmptyString(object, key, QString());
    if (!reader.ok())
        return name;

    const bool hasFolder = name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))
                           || name.contains(QLatin1Char(':'));
    const QString ending = QLatin1String(extension);
    if (hasFolder) {
        reader.fail(QLatin1String(key), JsonReader::tr("a file name without a folder is expected"));
    } else if (!name.endsWith(ending, Qt::CaseInsensitive) || name.size() == ending.size()) {
        reader.fail(QLatin1String(key),
                    JsonReader::tr("a file name ending in %1 is expected").arg(ending));
    }
    return name;
}

QJsonObject titleBlockToJson(const TitleBlock &block)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_TITLE), block.title);
    object.insert(QLatin1String(KEY_AUTHOR), block.author);
    object.insert(QLatin1String(KEY_ORGANIZATION), block.organization);
    object.insert(QLatin1String(KEY_CONTACTS), block.contacts);
    object.insert(QLatin1String(KEY_REVISION), block.revision);
    object.insert(QLatin1String(KEY_DATE), block.date);
    object.insert(QLatin1String(KEY_NOTE), block.note);
    return object;
}

TitleBlock titleBlockFromJson(const QJsonObject &root, JsonReader &reader)
{
    const QJsonObject object = reader.object(root, KEY_TITLE_BLOCK, QString());
    const QString where = QLatin1String(KEY_TITLE_BLOCK);
    TitleBlock block;
    if (!reader.ok())
        return block;
    block.title = reader.string(object, KEY_TITLE, where);
    block.author = reader.string(object, KEY_AUTHOR, where);
    block.organization = reader.string(object, KEY_ORGANIZATION, where);
    block.contacts = reader.string(object, KEY_CONTACTS, where);
    block.revision = reader.string(object, KEY_REVISION, where);
    block.date = reader.string(object, KEY_DATE, where);
    block.note = reader.string(object, KEY_NOTE, where);
    return block;
}

// The two diameters of a via, of the rules or of a net class.
void viaFromJson(const QJsonObject &object, const QString &where, JsonReader &reader,
                 Coord &drill, Coord &size)
{
    drill = reader.positiveMm(object, KEY_VIA_DRILL, where);
    size = reader.positiveMm(object, KEY_VIA_SIZE, where);
    if (reader.ok() && drill >= size) {
        reader.fail(JsonReader::pathOf(where, KEY_VIA_DRILL),
                    JsonReader::tr("the hole must be smaller than the via"));
    }
}

QJsonObject netClassToJson(const NetClass &netClass)
{
    QJsonObject object;
    object.insert(QLatin1String(KEY_NAME), netClass.name);
    object.insert(QLatin1String(KEY_TRACK_WIDTH), mmToJson(netClass.trackWidth));
    object.insert(QLatin1String(KEY_CLEARANCE), mmToJson(netClass.clearance));
    object.insert(QLatin1String(KEY_VIA_DRILL), mmToJson(netClass.viaDrill));
    object.insert(QLatin1String(KEY_VIA_SIZE), mmToJson(netClass.viaSize));
    return object;
}

NetClass netClassFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    NetClass netClass;
    netClass.name = reader.nonEmptyString(object, KEY_NAME, where);
    if (reader.ok() && context.classNames.contains(netClass.name))
        reader.fail(JsonReader::pathOf(where, KEY_NAME), JsonReader::tr("the name is used twice"));
    context.classNames.append(netClass.name);
    netClass.trackWidth = reader.positiveMm(object, KEY_TRACK_WIDTH, where);
    netClass.clearance = reader.positiveMm(object, KEY_CLEARANCE, where);
    viaFromJson(object, where, reader, netClass.viaDrill, netClass.viaSize);
    return netClass;
}

QJsonObject rulesToJson(const Rules &rules)
{
    QJsonArray widths;
    for (Coord width : rules.trackWidths)
        widths.append(mmToJson(width));

    QJsonObject checks;
    checks.insert(QLatin1String(KEY_CLEARANCE), levelNames().at(int(rules.clearanceLevel)));
    checks.insert(QLatin1String(KEY_ANNULAR_RING), levelNames().at(int(rules.annularRingLevel)));
    checks.insert(QLatin1String(KEY_CLASS_WIDTH), levelNames().at(int(rules.classWidthLevel)));
    checks.insert(QLatin1String(KEY_MINIMUM_WIDTH), levelNames().at(int(rules.minimumWidthLevel)));
    checks.insert(QLatin1String(KEY_EDGE_CLEARANCE),
                  levelNames().at(int(rules.edgeClearanceLevel)));

    QJsonObject object;
    object.insert(QLatin1String(KEY_SET), ruleSetNames().at(int(rules.set)));
    object.insert(QLatin1String(KEY_TRACK_WIDTHS), widths);
    object.insert(QLatin1String(KEY_DEFAULT_TRACK_WIDTH), mmToJson(rules.defaultTrackWidth));
    object.insert(QLatin1String(KEY_CLEARANCE), mmToJson(rules.clearance));
    object.insert(QLatin1String(KEY_VIA_DRILL), mmToJson(rules.viaDrill));
    object.insert(QLatin1String(KEY_VIA_SIZE), mmToJson(rules.viaSize));
    object.insert(QLatin1String(KEY_ANNULAR_RING), mmToJson(rules.annularRing));
    object.insert(QLatin1String(KEY_EDGE_CLEARANCE), mmToJson(rules.edgeClearance));
    object.insert(QLatin1String(KEY_MASK_EXPANSION), mmToJson(rules.maskExpansion));
    object.insert(QLatin1String(KEY_VIAS_COVERED), rules.viasCovered);
    object.insert(QLatin1String(KEY_CHECKS), checks);
    return object;
}

CheckLevel levelFromJson(const QJsonObject &checks, const char *key, const QString &where,
                         JsonReader &reader)
{
    return CheckLevel(reader.choice(checks, key, where, levelNames()));
}

Rules rulesFromJson(const QJsonObject &root, JsonReader &reader)
{
    const QJsonObject object = reader.object(root, KEY_RULES, QString());
    const QString where = QLatin1String(KEY_RULES);
    Rules rules;
    if (!reader.ok())
        return rules;

    rules.set = RuleSet(reader.choice(object, KEY_SET, where, ruleSetNames()));

    rules.trackWidths = reader.mmList(object, KEY_TRACK_WIDTHS, where);
    const QString widthsWhere = JsonReader::pathOf(where, KEY_TRACK_WIDTHS);
    if (reader.ok() && rules.trackWidths.isEmpty())
        reader.fail(widthsWhere, JsonReader::tr("at least one track width is expected"));
    for (int i = 0; i < rules.trackWidths.size(); ++i) {
        const QString widthWhere = JsonReader::pathOf(widthsWhere, i);
        if (rules.trackWidths.at(i) <= 0)
            reader.fail(widthWhere, JsonReader::tr("a size greater than zero is expected"));
        else if (i > 0 && rules.trackWidths.at(i) <= rules.trackWidths.at(i - 1)) {
            reader.fail(widthWhere,
                        JsonReader::tr("every track width must be greater than the one before it"));
        }
    }

    rules.defaultTrackWidth = reader.positiveMm(object, KEY_DEFAULT_TRACK_WIDTH, where);
    if (reader.ok() && !rules.trackWidths.contains(rules.defaultTrackWidth)) {
        reader.fail(JsonReader::pathOf(where, KEY_DEFAULT_TRACK_WIDTH),
                    JsonReader::tr("the width is not in the list of track widths"));
    }
    rules.clearance = reader.positiveMm(object, KEY_CLEARANCE, where);
    viaFromJson(object, where, reader, rules.viaDrill, rules.viaSize);
    rules.annularRing = reader.nonNegativeMm(object, KEY_ANNULAR_RING, where);
    rules.edgeClearance = reader.nonNegativeMm(object, KEY_EDGE_CLEARANCE, where);
    rules.maskExpansion = reader.nonNegativeMm(object, KEY_MASK_EXPANSION, where);
    rules.viasCovered = reader.boolean(object, KEY_VIAS_COVERED, where);

    const QJsonObject checks = reader.object(object, KEY_CHECKS, where);
    const QString checksWhere = JsonReader::pathOf(where, KEY_CHECKS);
    if (!reader.ok())
        return rules;
    rules.clearanceLevel = levelFromJson(checks, KEY_CLEARANCE, checksWhere, reader);
    rules.annularRingLevel = levelFromJson(checks, KEY_ANNULAR_RING, checksWhere, reader);
    rules.classWidthLevel = levelFromJson(checks, KEY_CLASS_WIDTH, checksWhere, reader);
    rules.minimumWidthLevel = levelFromJson(checks, KEY_MINIMUM_WIDTH, checksWhere, reader);
    rules.edgeClearanceLevel = levelFromJson(checks, KEY_EDGE_CLEARANCE, checksWhere, reader);
    return rules;
}

// The name of one of the project's net classes.
QString classNameFromJson(const QJsonObject &root, const char *key, const QStringList &classNames,
                          JsonReader &reader)
{
    const QString name = reader.nonEmptyString(root, key, QString());
    if (reader.ok() && !classNames.contains(name))
        reader.fail(QLatin1String(key),
                    JsonReader::tr("the net class is not in the list of net classes"));
    return name;
}

// The class chosen for single nets: an object of class names by net name.
QMap<QString, QString> classOfNetFromJson(const QJsonObject &root, const QStringList &classNames,
                                          JsonReader &reader)
{
    const QMap<QString, QString> classOfNet = reader.nameMap(root, KEY_NETS, QString());
    for (auto it = classOfNet.constBegin(); it != classOfNet.constEnd(); ++it) {
        const QString where = JsonReader::pathOf(QLatin1String(KEY_NETS), it.key());
        if (it.key().isEmpty())
            reader.fail(where, JsonReader::tr("the text must not be empty"));
        else if (!classNames.contains(it.value()))
            reader.fail(where, JsonReader::tr("the net class is not in the list of net classes"));
    }
    return classOfNet;
}

QJsonObject netByPinsToJson(const NetByPins &net)
{
    QJsonArray pins;
    for (const PinRef &pin : net.pins) {
        QJsonObject object;
        object.insert(QLatin1String(KEY_COMPONENT), idToString(pin.component));
        object.insert(QLatin1String(KEY_PIN), pin.number);
        pins.append(object);
    }
    QJsonObject object;
    object.insert(QLatin1String(KEY_NAME), net.name);
    object.insert(QLatin1String(KEY_PINS), pins);
    return object;
}

NetByPins netByPinsFromJson(const QJsonObject &object, const QString &where, ReadContext &context)
{
    JsonReader &reader = context.reader;
    NetByPins net;
    net.name = reader.nonEmptyString(object, KEY_NAME, where);
    const QList<QJsonObject> pins = reader.objectList(object, KEY_PINS, where);
    const QString pinsWhere = JsonReader::pathOf(where, KEY_PINS);
    for (int i = 0; i < pins.size() && reader.ok(); ++i) {
        const QString pinWhere = JsonReader::pathOf(pinsWhere, i);
        PinRef pin;
        pin.component = reader.id(pins.at(i), KEY_COMPONENT, pinWhere);
        pin.number = reader.nonEmptyString(pins.at(i), KEY_PIN, pinWhere);
        net.pins.append(pin);
    }
    return net;
}

// The names given to nets by their pins; none in a file written before them.
QList<NetByPins> netsByPinsFromJson(const QJsonObject &root, ReadContext &context)
{
    if (!root.contains(QLatin1String(KEY_NETS_BY_PINS)))
        return {};
    const QList<NetByPins> nets = listFromJson<NetByPins>(root, KEY_NETS_BY_PINS, QString(),
                                                          context, netByPinsFromJson);
    QStringList names;
    for (int i = 0; i < nets.size() && context.reader.ok(); ++i) {
        if (names.contains(nets.at(i).name)) {
            const QString where = JsonReader::pathOf(QLatin1String(KEY_NETS_BY_PINS), i);
            context.reader.fail(JsonReader::pathOf(where, KEY_NAME),
                                JsonReader::tr("the name is used twice"));
        }
        names.append(nets.at(i).name);
    }
    return nets;
}

NetClass startingNetClass(const char *name, Coord trackWidth)
{
    NetClass netClass;
    netClass.name = QLatin1String(name);
    netClass.trackWidth = trackWidth;
    netClass.clearance = PRODUCTION_CLEARANCE;
    netClass.viaDrill = PRODUCTION_VIA_DRILL;
    netClass.viaSize = PRODUCTION_VIA_SIZE;
    return netClass;
}

// The file 'name' beside the project file.
QString besideProject(const QString &projectPath, const QString &name)
{
    return QFileInfo(projectPath).dir().filePath(name);
}

} // namespace

Rules productionRules()
{
    Rules rules;
    rules.set = RuleSet::Production;
    rules.trackWidths = {300000, 500000, 800000, 1000000, 1500000};
    rules.defaultTrackWidth = PRODUCTION_DEFAULT_WIDTH;
    rules.clearance = PRODUCTION_CLEARANCE;
    rules.viaDrill = PRODUCTION_VIA_DRILL;
    rules.viaSize = PRODUCTION_VIA_SIZE;
    rules.annularRing = PRODUCTION_ANNULAR_RING;
    rules.edgeClearance = PRODUCTION_EDGE_CLEARANCE;
    rules.maskExpansion = MASK_EXPANSION;
    return rules;
}

Rules lutRules()
{
    Rules rules;
    rules.set = RuleSet::Lut;
    rules.trackWidths = {500000, 800000, 1000000, 1500000, 2000000};
    rules.defaultTrackWidth = LUT_DEFAULT_WIDTH;
    rules.clearance = LUT_CLEARANCE;
    rules.viaDrill = LUT_VIA_DRILL;
    rules.viaSize = LUT_VIA_SIZE;
    rules.annularRing = LUT_ANNULAR_RING;
    rules.edgeClearance = LUT_EDGE_CLEARANCE;
    rules.maskExpansion = MASK_EXPANSION;
    return rules;
}

bool operator==(const TitleBlock &a, const TitleBlock &b)
{
    return a.title == b.title && a.author == b.author && a.organization == b.organization
           && a.contacts == b.contacts && a.revision == b.revision && a.date == b.date
           && a.note == b.note;
}

bool operator==(const NetClass &a, const NetClass &b)
{
    return a.name == b.name && a.trackWidth == b.trackWidth && a.clearance == b.clearance
           && a.viaDrill == b.viaDrill && a.viaSize == b.viaSize;
}

bool operator==(const Rules &a, const Rules &b)
{
    return a.set == b.set && a.trackWidths == b.trackWidths
           && a.defaultTrackWidth == b.defaultTrackWidth && a.clearance == b.clearance
           && a.viaDrill == b.viaDrill && a.viaSize == b.viaSize
           && a.annularRing == b.annularRing && a.edgeClearance == b.edgeClearance
           && a.maskExpansion == b.maskExpansion && a.viasCovered == b.viasCovered
           && a.clearanceLevel == b.clearanceLevel && a.annularRingLevel == b.annularRingLevel
           && a.classWidthLevel == b.classWidthLevel
           && a.minimumWidthLevel == b.minimumWidthLevel
           && a.edgeClearanceLevel == b.edgeClearanceLevel;
}

bool operator==(const PinRef &a, const PinRef &b)
{
    return a.component == b.component && a.number == b.number;
}

bool operator==(const NetByPins &a, const NetByPins &b)
{
    return a.name == b.name && a.pins == b.pins;
}

bool operator==(const Project &a, const Project &b)
{
    return a.schematicFile == b.schematicFile && a.boardFile == b.boardFile
           && a.titleBlock == b.titleBlock && a.designatorStyle == b.designatorStyle
           && a.netClasses == b.netClasses && a.defaultClass == b.defaultClass
           && a.powerClass == b.powerClass && a.classOfNet == b.classOfNet
           && a.netsByPins == b.netsByPins && a.rules == b.rules
           && a.schematicGridStep == b.schematicGridStep && a.boardGridStep == b.boardGridStep;
}

bool operator==(const Design &a, const Design &b)
{
    return a.project == b.project && a.schematic == b.schematic && a.board == b.board
           && a.library == b.library;
}

QJsonObject projectToJson(const Project &project)
{
    QJsonObject nets;
    for (auto it = project.classOfNet.constBegin(); it != project.classOfNet.constEnd(); ++it)
        nets.insert(it.key(), it.value());

    QJsonObject grid;
    grid.insert(QLatin1String(KEY_SCHEMATIC), mmToJson(project.schematicGridStep));
    grid.insert(QLatin1String(KEY_BOARD), mmToJson(project.boardGridStep));

    QJsonObject root;
    root.insert(QLatin1String(KEY_SCHEMATIC), project.schematicFile);
    root.insert(QLatin1String(KEY_BOARD), project.boardFile);
    root.insert(QLatin1String(KEY_TITLE_BLOCK), titleBlockToJson(project.titleBlock));
    root.insert(QLatin1String(KEY_DESIGNATOR_STYLE),
                styleNames().at(int(project.designatorStyle)));
    root.insert(QLatin1String(KEY_NET_CLASSES), listToJson(project.netClasses, netClassToJson));
    root.insert(QLatin1String(KEY_DEFAULT_CLASS), project.defaultClass);
    root.insert(QLatin1String(KEY_POWER_CLASS), project.powerClass);
    root.insert(QLatin1String(KEY_NETS), nets);
    root.insert(QLatin1String(KEY_NETS_BY_PINS), listToJson(project.netsByPins, netByPinsToJson));
    root.insert(QLatin1String(KEY_RULES), rulesToJson(project.rules));
    root.insert(QLatin1String(KEY_GRID), grid);
    return root;
}

Project projectFromJson(const QJsonObject &root, JsonReader &reader)
{
    Project project;
    ReadContext context{reader, {}};

    project.schematicFile = fileNameFromJson(root, KEY_SCHEMATIC, FILE_EXT_SCHEMATIC, reader);
    project.boardFile = fileNameFromJson(root, KEY_BOARD, FILE_EXT_BOARD, reader);
    project.titleBlock = titleBlockFromJson(root, reader);
    project.designatorStyle = DesignatorStyle(
        reader.choice(root, KEY_DESIGNATOR_STYLE, QString(), styleNames()));

    // Net classes first: the names below are checked against theirs.
    project.netClasses = listFromJson<NetClass>(root, KEY_NET_CLASSES, QString(), context,
                                                netClassFromJson);
    if (reader.ok() && project.netClasses.isEmpty()) {
        reader.fail(QLatin1String(KEY_NET_CLASSES),
                    JsonReader::tr("a project needs at least one net class"));
    }
    project.defaultClass = classNameFromJson(root, KEY_DEFAULT_CLASS, context.classNames, reader);
    project.powerClass = classNameFromJson(root, KEY_POWER_CLASS, context.classNames, reader);
    project.classOfNet = classOfNetFromJson(root, context.classNames, reader);
    project.netsByPins = netsByPinsFromJson(root, context);
    project.rules = rulesFromJson(root, reader);

    const QJsonObject grid = reader.object(root, KEY_GRID, QString());
    if (reader.ok()) {
        const QString gridWhere = QLatin1String(KEY_GRID);
        project.schematicGridStep = reader.positiveMm(grid, KEY_SCHEMATIC, gridWhere);
        project.boardGridStep = reader.positiveMm(grid, KEY_BOARD, gridWhere);
    }
    return project;
}

bool ProjectFile::read(const QString &path, Project &project, QString &error)
{
    QJsonObject root;
    if (!JsonFile::read(path, root, error))
        return false;

    JsonReader reader;
    reader.header(root, FILE_TYPE_PROJECT);
    const Project result = projectFromJson(root, reader);
    if (!reader.ok()) {
        error = withPlace(path, reader.error());
        return false;
    }
    project = result;
    return true;
}

bool ProjectFile::write(const QString &path, const Project &project, QString &error)
{
    QJsonObject root = projectToJson(project);
    writeHeader(root, FILE_TYPE_PROJECT);
    return JsonFile::write(path, root, error);
}

Design newDesign(const QString &baseName)
{
    Design design;
    design.project.schematicFile = baseName + QLatin1String(FILE_EXT_SCHEMATIC);
    design.project.boardFile = baseName + QLatin1String(FILE_EXT_BOARD);
    design.project.titleBlock.title = baseName;
    design.project.netClasses = {startingNetClass(NET_CLASS_SIGNAL, PRODUCTION_DEFAULT_WIDTH),
                                 startingNetClass(NET_CLASS_POWER, POWER_TRACK_WIDTH)};
    design.project.defaultClass = QLatin1String(NET_CLASS_SIGNAL);
    design.project.powerClass = QLatin1String(NET_CLASS_POWER);
    design.project.rules = productionRules();

    Sheet sheet;
    sheet.id = newObjectId();
    design.schematic.sheets.append(sheet);
    return design;
}

QString DesignFiles::schematicPath(const QString &projectPath, const Project &project)
{
    return besideProject(projectPath, project.schematicFile);
}

QString DesignFiles::boardPath(const QString &projectPath, const Project &project)
{
    return besideProject(projectPath, project.boardFile);
}

QString DesignFiles::libraryPath(const QString &projectPath)
{
    return besideProject(projectPath, QLatin1String(PROJECT_LIBRARY_FOLDER));
}

QString DesignFiles::backupPath(const QString &projectPath)
{
    return besideProject(projectPath, QLatin1String(PROJECT_BACKUP_FOLDER));
}

QString DesignFiles::autosaveProjectPath(const QString &projectPath)
{
    return QDir(besideProject(projectPath, QLatin1String(PROJECT_AUTOSAVE_FOLDER)))
        .filePath(QFileInfo(projectPath).fileName());
}

QStringList missingLibraryItems(const Design &design)
{
    const QString folder = QLatin1String(PROJECT_LIBRARY_FOLDER) + QLatin1Char('/');
    const auto fileOf = [&folder](const char *subfolder, const QString &name) {
        return folder + QLatin1String(subfolder) + QLatin1Char('/') + name
               + QLatin1String(LIBRARY_FILE_EXT);
    };

    QStringList files;
    const Library &library = design.library;
    for (const Sheet &sheet : design.schematic.sheets) {
        for (const Component &component : sheet.components) {
            const Part *part = library.findPart(component.part);
            if (part == nullptr)
                files.append(fileOf(LIBRARY_PARTS_FOLDER, component.part));
            else if (library.findSymbolOfPart(component.part) == nullptr)
                files.append(fileOf(LIBRARY_SYMBOLS_FOLDER, part->symbol));
            if (!component.footprint.isEmpty()
                && library.findFootprint(component.footprint) == nullptr)
                files.append(fileOf(LIBRARY_FOOTPRINTS_FOLDER, component.footprint));
        }
    }
    for (const Sheet &sheet : design.schematic.sheets) {
        for (const PowerSymbol &power : sheet.powerSymbols) {
            if (library.findSymbol(power.symbol) == nullptr)
                files.append(fileOf(LIBRARY_SYMBOLS_FOLDER, power.symbol));
        }
    }
    for (const Placement &placement : design.board.placements) {
        if (library.findFootprint(placement.footprint) == nullptr)
            files.append(fileOf(LIBRARY_FOOTPRINTS_FOLDER, placement.footprint));
    }
    files.removeDuplicates();

    QStringList messages;
    for (const QString &file : files)
        messages.append(withPlace(file, LibraryFolder::tr("the file is missing")));
    return messages;
}

bool DesignFiles::read(const QString &projectPath, Design &design, QString &error,
                       QStringList *problems)
{
    Design result;
    if (!ProjectFile::read(projectPath, result.project, error))
        return false;
    if (!SchematicFile::read(schematicPath(projectPath, result.project), result.schematic, error))
        return false;
    if (!BoardFile::read(boardPath(projectPath, result.project), result.board, error))
        return false;
    QStringList found;
    result.library = LibraryFolder::read(libraryPath(projectPath), found);
    found += missingLibraryItems(result);
    if (problems != nullptr)
        *problems = found;
    design = result;
    return true;
}

bool DesignFiles::write(const QString &projectPath, const Design &design, QString &error)
{
    // The project goes last, so that it never names a file that is not there.
    Library written;
    return LibraryFolder::write(libraryPath(projectPath), design.library, written, true,
                                      error)
           && SchematicFile::write(schematicPath(projectPath, design.project), design.schematic,
                                   error)
           && BoardFile::write(boardPath(projectPath, design.project), design.board, error)
           && ProjectFile::write(projectPath, design.project, error);
}

} // namespace ppcb
