#include "project.h"

#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

namespace {

// A resistor in the library, on the first sheet and on the board.
void addResistor(Design &design)
{
    Symbol symbol;
    Pin pin;
    pin.number = QStringLiteral("1");
    symbol.pins.append(pin);
    design.library.symbols.insert(QStringLiteral("resistor"), symbol);
    Footprint footprint;
    Pad pad;
    pad.number = QStringLiteral("1");
    pad.sizeX = NM_PER_MM;
    pad.sizeY = NM_PER_MM;
    pad.hole = NM_PER_MM / 2;
    footprint.pads.append(pad);
    design.library.footprints.insert(QStringLiteral("r-7.62"), footprint);
    Part part;
    part.symbol = QStringLiteral("resistor");
    part.footprints = QStringList({QStringLiteral("r-7.62")});
    part.prefix = QStringLiteral("R");
    part.prefixGost = QStringLiteral("R");
    design.library.parts.insert(QStringLiteral("resistor"), part);

    Component component;
    component.id = newObjectId();
    component.part = QStringLiteral("resistor");
    component.footprint = QStringLiteral("r-7.62");
    component.designator = QStringLiteral("R1");
    design.schematic.sheets[0].components.append(component);
    Placement placement;
    placement.id = newObjectId();
    placement.component = component.id;
    placement.footprint = QStringLiteral("r-7.62");
    placement.designator = QStringLiteral("R1");
    design.board.placements.append(placement);
}

// Every field non-default.
Project fullProject()
{
    Project project;
    project.schematicFile = QStringLiteral("Усилитель.ppsch");
    project.boardFile = QStringLiteral("Усилитель.ppbrd");

    project.titleBlock.title = QStringLiteral("Усилитель");
    project.titleBlock.author = QStringLiteral("Автор");
    project.titleBlock.organization = QStringLiteral("Lab");
    project.titleBlock.contacts = QStringLiteral("mail@example.org");
    project.titleBlock.revision = QStringLiteral("B");
    project.titleBlock.date = QStringLiteral("2026-11-13");
    project.titleBlock.note = QStringLiteral("draft");
    project.designatorStyle = DesignatorStyle::Gost;

    NetClass signal;
    signal.name = QStringLiteral("Signal");
    signal.trackWidth = 500000;
    signal.clearance = 300000;
    signal.viaDrill = 600000;
    signal.viaSize = 1200000;
    project.netClasses.append(signal);

    NetClass power;
    power.name = QStringLiteral("Силовые");
    power.trackWidth = 1500000;
    power.clearance = 400000;
    power.viaDrill = 800000;
    power.viaSize = 2000000;
    project.netClasses.append(power);
    project.defaultClass = QStringLiteral("Signal");
    project.powerClass = QStringLiteral("Силовые");

    project.classOfNet.insert(QStringLiteral("GND"), QStringLiteral("Силовые"));
    project.classOfNet.insert(QStringLiteral("N7"), QStringLiteral("Signal"));
    project.classOfNet.insert(QStringLiteral("NN1"), QStringLiteral("Силовые"));
    project.netsByPins = {NetByPins{QStringLiteral("NN1"),
                                    {PinRef{ObjectId{0x0123456789ab}, QStringLiteral("2")},
                                     PinRef{ObjectId{0x0000000000c4}, QStringLiteral("A1")}}}};

    project.rules.set = RuleSet::Lut;
    project.rules.trackWidths = {400000, 700000, 1200000};
    project.rules.defaultTrackWidth = 700000;
    project.rules.clearance = 450000;
    project.rules.viaDrill = 700000;
    project.rules.viaSize = 1800000;
    project.rules.annularRing = 550000;
    project.rules.edgeClearance = 900000;
    project.rules.maskExpansion = 150000;
    project.rules.viasCovered = false;
    project.rules.drills = {700000, 3300000};
    project.rules.drillsOnly = false;
    project.rules.clearanceLevel = CheckLevel::Warning;
    project.rules.annularRingLevel = CheckLevel::Off;
    project.rules.classWidthLevel = CheckLevel::Error;
    project.rules.minimumWidthLevel = CheckLevel::Off;
    project.rules.edgeClearanceLevel = CheckLevel::Error;

    project.schematicGridStep = 1270000;
    project.boardGridStep = 635000;
    return project;
}

QString errorOf(const QJsonObject &root)
{
    JsonReader reader;
    projectFromJson(root, reader);
    return reader.error();
}

// Sets or removes (an undefined value) one field of an object.
void change(QJsonObject &object, const char *key, const QJsonValue &value)
{
    if (value.isUndefined())
        object.remove(QLatin1String(key));
    else
        object.insert(QLatin1String(key), value);
}

// The error of reading the full project with one field of the root changed.
QString errorOfRoot(const char *key, const QJsonValue &value)
{
    QJsonObject root = projectToJson(fullProject());
    change(root, key, value);
    return errorOf(root);
}

// The same for a field of an object of the root: "rules", "grid", "titleBlock".
QString errorOfPart(const char *partKey, const char *key, const QJsonValue &value)
{
    QJsonObject root = projectToJson(fullProject());
    QJsonObject part = root.value(QLatin1String(partKey)).toObject();
    change(part, key, value);
    root.insert(QLatin1String(partKey), part);
    return errorOf(root);
}

// The same for a field of net class 'index'.
QString errorOfClass(int index, const char *key, const QJsonValue &value)
{
    QJsonObject root = projectToJson(fullProject());
    QJsonArray classes = root.value(QStringLiteral("netClasses")).toArray();
    QJsonObject netClass = classes.at(index).toObject();
    change(netClass, key, value);
    classes.replace(index, netClass);
    root.insert(QStringLiteral("netClasses"), classes);
    return errorOf(root);
}

// The same for a level of rules.checks.
QString errorOfCheck(const char *key, const QJsonValue &value)
{
    QJsonObject root = projectToJson(fullProject());
    QJsonObject rules = root.value(QStringLiteral("rules")).toObject();
    QJsonObject checks = rules.value(QStringLiteral("checks")).toObject();
    change(checks, key, value);
    rules.insert(QStringLiteral("checks"), checks);
    root.insert(QStringLiteral("rules"), rules);
    return errorOf(root);
}

QJsonArray sizes(std::initializer_list<double> mm)
{
    QJsonArray array;
    for (double value : mm)
        array.append(value);
    return array;
}

QJsonObject oneNet(const QString &net, const QJsonValue &netClass)
{
    QJsonObject nets;
    nets.insert(net, netClass);
    return nets;
}

const QJsonValue REMOVED(QJsonValue::Undefined);

} // namespace

class TestProject : public QObject
{
    Q_OBJECT

private slots:
    void everyFieldSurvivesTheFile()
    {
        const Project project = fullProject();
        const QByteArray text = QJsonDocument(projectToJson(project)).toJson();
        JsonReader reader;
        const Project back = projectFromJson(QJsonDocument::fromJson(text).object(), reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QVERIFY(back == project);
        QCOMPARE(QJsonDocument(projectToJson(back)).toJson(), text);
    }

    void comparisonSeesEveryField()
    {
        const Project project = fullProject();
        Project other = project;
        QVERIFY(other == project);

#define CHANGED(statement) \
    other = project; \
    statement; \
    QVERIFY2(other != project, #statement)

        CHANGED(other.schematicFile = QStringLiteral("a.ppsch"));
        CHANGED(other.boardFile = QStringLiteral("a.ppbrd"));
        CHANGED(other.titleBlock.title.clear());
        CHANGED(other.titleBlock.author.clear());
        CHANGED(other.titleBlock.organization.clear());
        CHANGED(other.titleBlock.contacts.clear());
        CHANGED(other.titleBlock.revision.clear());
        CHANGED(other.titleBlock.date.clear());
        CHANGED(other.titleBlock.note.clear());
        CHANGED(other.designatorStyle = DesignatorStyle::International);

        CHANGED(other.netClasses[1].name = QStringLiteral("Power"));
        CHANGED(other.netClasses[1].trackWidth += 1);
        CHANGED(other.netClasses[1].clearance += 1);
        CHANGED(other.netClasses[1].viaDrill += 1);
        CHANGED(other.netClasses[1].viaSize += 1);
        CHANGED(other.netClasses.removeLast());
        CHANGED(other.defaultClass = QStringLiteral("Силовые"));
        CHANGED(other.powerClass = QStringLiteral("Signal"));
        CHANGED(other.classOfNet[QStringLiteral("GND")] = QStringLiteral("Signal"));
        CHANGED(other.classOfNet.remove(QStringLiteral("N7")));
        CHANGED(other.netsByPins[0].name = QStringLiteral("NN2"));
        CHANGED(other.netsByPins[0].pins[0].component = ObjectId{7});
        CHANGED(other.netsByPins[0].pins[0].number = QStringLiteral("3"));
        CHANGED(other.netsByPins[0].pins.removeLast());
        CHANGED(other.netsByPins.clear());

        CHANGED(other.rules.set = RuleSet::Production);
        CHANGED(other.rules.trackWidths[0] += 1);
        CHANGED(other.rules.trackWidths.removeLast());
        CHANGED(other.rules.defaultTrackWidth += 1);
        CHANGED(other.rules.clearance += 1);
        CHANGED(other.rules.viaDrill += 1);
        CHANGED(other.rules.viaSize += 1);
        CHANGED(other.rules.annularRing += 1);
        CHANGED(other.rules.edgeClearance += 1);
        CHANGED(other.rules.maskExpansion += 1);
        CHANGED(other.rules.viasCovered = true);
        CHANGED(other.rules.drills[0] += 1);
        CHANGED(other.rules.drills.removeLast());
        CHANGED(other.rules.drillsOnly = true);
        CHANGED(other.rules.clearanceLevel = CheckLevel::Error);
        CHANGED(other.rules.annularRingLevel = CheckLevel::Warning);
        CHANGED(other.rules.classWidthLevel = CheckLevel::Warning);
        CHANGED(other.rules.minimumWidthLevel = CheckLevel::Error);
        CHANGED(other.rules.edgeClearanceLevel = CheckLevel::Warning);

        CHANGED(other.schematicGridStep += 1);
        CHANGED(other.boardGridStep += 1);
#undef CHANGED
    }

    // The table of the specification, value by value.
    void ruleSetsFollowTheSpecification()
    {
        const Rules production = productionRules();
        QVERIFY(production.set == RuleSet::Production);
        QCOMPARE(production.trackWidths, QList<Coord>({300000, 500000, 800000, 1000000, 1500000}));
        QCOMPARE(production.defaultTrackWidth, Coord(500000));
        QCOMPARE(production.clearance, Coord(300000));
        QCOMPARE(production.viaDrill, Coord(600000));
        QCOMPARE(production.viaSize, Coord(1200000));
        QCOMPARE(production.annularRing, Coord(300000));
        QCOMPARE(production.edgeClearance, Coord(500000));
        QCOMPARE(production.drills,
                 QList<Coord>({600000, 800000, 1000000, 1200000, 1500000, 2000000, 2500000,
                               3000000, 3200000, 3500000, 4000000, 5000000, 6000000}));
        QCOMPARE(production.drillsOnly, false);

        const Rules lut = lutRules();
        QVERIFY(lut.set == RuleSet::Lut);
        QCOMPARE(lut.trackWidths, QList<Coord>({500000, 800000, 1000000, 1500000, 2000000}));
        QCOMPARE(lut.defaultTrackWidth, Coord(800000));
        QCOMPARE(lut.clearance, Coord(500000));
        QCOMPARE(lut.viaDrill, Coord(800000));
        QCOMPARE(lut.viaSize, Coord(2000000));
        QCOMPARE(lut.annularRing, Coord(600000));
        QCOMPARE(lut.edgeClearance, Coord(1000000));
        QCOMPARE(lut.drills,
                 QList<Coord>({600000, 800000, 1000000, 1200000, 1500000, 2000000, 3000000,
                               3200000, 4000000, 5000000, 6000000, 8000000, 10000000}));
        QCOMPARE(lut.drillsOnly, true);

        // The same in both sets: the mask, covered vias and the levels.
        for (const Rules &rules : {production, lut}) {
            QCOMPARE(rules.maskExpansion, Coord(100000));
            QCOMPARE(rules.viasCovered, true);
            QVERIFY(rules.clearanceLevel == CheckLevel::Error);
            QVERIFY(rules.annularRingLevel == CheckLevel::Warning);
            QVERIFY(rules.classWidthLevel == CheckLevel::Warning);
            QVERIFY(rules.minimumWidthLevel == CheckLevel::Error);
            QVERIFY(rules.edgeClearanceLevel == CheckLevel::Warning);
        }
    }

    // Both sets pass the checks of reading.
    void ruleSetsAreValid()
    {
        for (const Rules &rules : {productionRules(), lutRules()}) {
            Project project = fullProject();
            project.rules = rules;
            JsonReader reader;
            const Project back = projectFromJson(projectToJson(project), reader);
            QVERIFY2(reader.ok(), qPrintable(reader.error()));
            QVERIFY(back.rules == rules);
        }
    }

    void fileNameRules()
    {
        const QStringList wrong = {
            QStringLiteral(""),            QStringLiteral("sub/a.ppsch"),
            QStringLiteral("..\\a.ppsch"), QStringLiteral("C:a.ppsch"),
            QStringLiteral("/a.ppsch"),    QStringLiteral("a.ppbrd"),
            QStringLiteral("a.ppsch.bak"), QStringLiteral(".ppsch"),
            QStringLiteral("a"),           QStringLiteral(".."),
        };
        for (const QString &name : wrong) {
            const QString error = errorOfRoot("schematic", name);
            QVERIFY2(error.startsWith(QStringLiteral("schematic: ")),
                     qPrintable(name + QStringLiteral(" -> ") + error));
        }
        QVERIFY(errorOfRoot("schematic", REMOVED).startsWith(QStringLiteral("schematic: ")));
        QVERIFY(errorOfRoot("schematic", 5).startsWith(QStringLiteral("schematic: ")));

        // The board must not be a schematic file, nor the project file.
        QVERIFY(errorOfRoot("board", QStringLiteral("a.ppsch")).startsWith(QStringLiteral("board: ")));
        QVERIFY(errorOfRoot("board", QStringLiteral("a.ppcb")).startsWith(QStringLiteral("board: ")));
        QVERIFY(errorOfRoot("board", REMOVED).startsWith(QStringLiteral("board: ")));

        // Any letters, spaces and letter case of the extension are fine.
        QCOMPARE(errorOfRoot("schematic", QStringLiteral("Мой усилитель v2.PPSCH")), QString());
        QCOMPARE(errorOfRoot("board", QStringLiteral("a.b.PpBrd")), QString());
    }

    void titleBlockAndStyleErrors()
    {
        QString error = errorOfPart("titleBlock", "note", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("titleBlock.note: ")), qPrintable(error));
        error = errorOfPart("titleBlock", "title", 5);
        QVERIFY2(error.startsWith(QStringLiteral("titleBlock.title: ")), qPrintable(error));
        error = errorOfRoot("titleBlock", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("titleBlock: ")), qPrintable(error));

        // Every field may be empty.
        for (const char *key : {"title", "author", "organization", "contacts", "revision", "date",
                                "note"}) {
            QCOMPARE(errorOfPart("titleBlock", key, QStringLiteral("")), QString());
        }

        error = errorOfRoot("designatorStyle", QStringLiteral("ansi"));
        QVERIFY2(error.startsWith(QStringLiteral("designatorStyle: ")), qPrintable(error));
    }

    void netClassRules()
    {
        // The second class takes the name of the first.
        QString error = errorOfClass(1, "name", QStringLiteral("Signal"));
        QVERIFY2(error.startsWith(QStringLiteral("netClasses[1].name: ")), qPrintable(error));

        error = errorOfClass(0, "name", QStringLiteral(""));
        QVERIFY2(error.startsWith(QStringLiteral("netClasses[0].name: ")), qPrintable(error));

        error = errorOfClass(0, "trackWidth", 0);
        QVERIFY2(error.startsWith(QStringLiteral("netClasses[0].trackWidth: ")), qPrintable(error));

        error = errorOfClass(0, "clearance", 0);
        QVERIFY2(error.startsWith(QStringLiteral("netClasses[0].clearance: ")), qPrintable(error));

        // The via of the first class is 1.2 mm across.
        error = errorOfClass(0, "viaDrill", 1.2);
        QVERIFY2(error.startsWith(QStringLiteral("netClasses[0].viaDrill: ")), qPrintable(error));
        error = errorOfClass(0, "viaSize", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("netClasses[0].viaSize: ")), qPrintable(error));

        error = errorOfRoot("netClasses", QJsonArray());
        QVERIFY2(error.startsWith(QStringLiteral("netClasses: ")), qPrintable(error));
        error = errorOfRoot("netClasses", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("netClasses: ")), qPrintable(error));
    }

    void defaultAndPowerClassRules()
    {
        for (const char *key : {"defaultClass", "powerClass"}) {
            const QString where = QLatin1String(key) + QStringLiteral(": ");
            QString error = errorOfRoot(key, QStringLiteral("Power"));
            QVERIFY2(error.startsWith(where), qPrintable(error));
            error = errorOfRoot(key, QStringLiteral(""));
            QVERIFY2(error.startsWith(where), qPrintable(error));
            error = errorOfRoot(key, 5);
            QVERIFY2(error.startsWith(where), qPrintable(error));
            error = errorOfRoot(key, REMOVED);
            QVERIFY2(error.startsWith(where), qPrintable(error));
        }
        // One class may be both.
        QCOMPARE(errorOfRoot("powerClass", QStringLiteral("Signal")), QString());
    }

    void classOfNetRules()
    {
        QString error = errorOfRoot("nets", oneNet(QStringLiteral("GND"), QStringLiteral("Power")));
        QVERIFY2(error.startsWith(QStringLiteral("nets.GND: ")), qPrintable(error));

        error = errorOfRoot("nets", oneNet(QStringLiteral("GND"), 5));
        QVERIFY2(error.startsWith(QStringLiteral("nets.GND: ")), qPrintable(error));

        error = errorOfRoot("nets", oneNet(QStringLiteral("GND"), QStringLiteral("")));
        QVERIFY2(error.startsWith(QStringLiteral("nets.GND: ")), qPrintable(error));

        // A net without a name.
        error = errorOfRoot("nets", oneNet(QString(), QStringLiteral("Signal")));
        QVERIFY2(error.startsWith(QStringLiteral("nets.: ")), qPrintable(error));

        error = errorOfRoot("nets", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("nets: ")), qPrintable(error));

        // No net was given a class by hand.
        QCOMPARE(errorOfRoot("nets", QJsonObject()), QString());
    }

    void netsByPinsRules()
    {
        // A file written before them has none.
        QCOMPARE(errorOfRoot("netsByPins", REMOVED), QString());
        QCOMPARE(errorOfRoot("netsByPins", QJsonArray()), QString());

        const auto netByPins = [](const QJsonValue &name, const QJsonValue &component,
                                  const QJsonValue &pin) {
            QJsonObject pinObject;
            pinObject.insert(QStringLiteral("component"), component);
            pinObject.insert(QStringLiteral("pin"), pin);
            QJsonObject net;
            net.insert(QStringLiteral("name"), name);
            net.insert(QStringLiteral("pins"), QJsonArray({pinObject}));
            return net;
        };
        const QJsonObject good =
            netByPins(QStringLiteral("NN1"), QStringLiteral("0123456789ab"), QStringLiteral("2"));
        QCOMPARE(errorOfRoot("netsByPins", QJsonArray({good})), QString());

        QString error = errorOfRoot("netsByPins", QJsonArray({good, good}));
        QVERIFY2(error.startsWith(QStringLiteral("netsByPins[1].name: ")), qPrintable(error));
        const QString id = QStringLiteral("0123456789ab");
        error = errorOfRoot("netsByPins",
                            QJsonArray({netByPins(QString(), id, QStringLiteral("2"))}));
        QVERIFY2(error.startsWith(QStringLiteral("netsByPins[0].name: ")), qPrintable(error));
        error = errorOfRoot("netsByPins", QJsonArray({netByPins(QStringLiteral("NN1"),
                                                                QStringLiteral("R1"),
                                                                QStringLiteral("2"))}));
        QVERIFY2(error.startsWith(QStringLiteral("netsByPins[0].pins[0].component: ")),
                 qPrintable(error));
        error = errorOfRoot("netsByPins",
                            QJsonArray({netByPins(QStringLiteral("NN1"), id, QString())}));
        QVERIFY2(error.startsWith(QStringLiteral("netsByPins[0].pins[0].pin: ")),
                 qPrintable(error));
    }

    void trackWidthRules()
    {
        QString error = errorOfPart("rules", "trackWidths", QJsonArray());
        QVERIFY2(error.startsWith(QStringLiteral("rules.trackWidths: ")), qPrintable(error));

        error = errorOfPart("rules", "trackWidths", sizes({0, 0.7}));
        QVERIFY2(error.startsWith(QStringLiteral("rules.trackWidths[0]: ")), qPrintable(error));

        error = errorOfPart("rules", "trackWidths", sizes({0.4, 0.7, 0.7}));
        QVERIFY2(error.startsWith(QStringLiteral("rules.trackWidths[2]: ")), qPrintable(error));

        error = errorOfPart("rules", "trackWidths", sizes({0.7, 0.4}));
        QVERIFY2(error.startsWith(QStringLiteral("rules.trackWidths[1]: ")), qPrintable(error));

        error = errorOfPart("rules", "trackWidths", QJsonArray{0.7, QStringLiteral("1.2")});
        QVERIFY2(error.startsWith(QStringLiteral("rules.trackWidths[1]: ")), qPrintable(error));

        // The default width of the full project is 0.7 mm.
        error = errorOfPart("rules", "trackWidths", sizes({0.4, 1.2}));
        QVERIFY2(error.startsWith(QStringLiteral("rules.defaultTrackWidth: ")), qPrintable(error));

        error = errorOfPart("rules", "defaultTrackWidth", 0);
        QVERIFY2(error.startsWith(QStringLiteral("rules.defaultTrackWidth: ")), qPrintable(error));

        // One width is enough.
        QCOMPARE(errorOfPart("rules", "trackWidths", sizes({0.7})), QString());
    }

    void drillRules()
    {
        QString error = errorOfPart("rules", "drills", QJsonArray());
        QVERIFY2(error.startsWith(QStringLiteral("rules.drills: ")), qPrintable(error));

        error = errorOfPart("rules", "drills", sizes({0, 0.7}));
        QVERIFY2(error.startsWith(QStringLiteral("rules.drills[0]: ")), qPrintable(error));

        error = errorOfPart("rules", "drills", sizes({0.7, 0.7}));
        QVERIFY2(error.startsWith(QStringLiteral("rules.drills[1]: ")), qPrintable(error));

        error = errorOfPart("rules", "drillsOnly", QStringLiteral("yes"));
        QVERIFY2(error.startsWith(QStringLiteral("rules.drillsOnly: ")), qPrintable(error));

        QCOMPARE(errorOfPart("rules", "drills", sizes({3.2})), QString());
    }

    // A file written before there were drills takes those of its set.
    void drillsMissingFromAnOldFile()
    {
        for (const Rules &rules : {productionRules(), lutRules()}) {
            Project project = fullProject();
            project.rules = rules;
            QJsonObject root = projectToJson(project);
            QJsonObject object = root.value(QStringLiteral("rules")).toObject();
            object.remove(QStringLiteral("drills"));
            object.remove(QStringLiteral("drillsOnly"));
            root.insert(QStringLiteral("rules"), object);
            JsonReader reader;
            const Project back = projectFromJson(root, reader);
            QVERIFY2(reader.ok(), qPrintable(reader.error()));
            QCOMPARE(back.rules.drills, rules.drills);
            QCOMPARE(back.rules.drillsOnly, rules.drillsOnly);
        }
    }

    void otherRuleErrors()
    {
        QString error = errorOfRoot("rules", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("rules: ")), qPrintable(error));

        error = errorOfPart("rules", "set", QStringLiteral("home"));
        QVERIFY2(error.startsWith(QStringLiteral("rules.set: ")), qPrintable(error));

        error = errorOfPart("rules", "clearance", 0);
        QVERIFY2(error.startsWith(QStringLiteral("rules.clearance: ")), qPrintable(error));

        // The via of the full project is 1.8 mm across.
        error = errorOfPart("rules", "viaDrill", 1.8);
        QVERIFY2(error.startsWith(QStringLiteral("rules.viaDrill: ")), qPrintable(error));

        for (const char *key : {"annularRing", "edgeClearance", "maskExpansion"}) {
            error = errorOfPart("rules", key, -0.1);
            const QString place = QStringLiteral("rules.") + QLatin1String(key) + QStringLiteral(": ");
            QVERIFY2(error.startsWith(place), qPrintable(error));
            // Zero switches the margin off.
            QCOMPARE(errorOfPart("rules", key, 0), QString());
        }

        error = errorOfPart("rules", "viasCovered", QStringLiteral("yes"));
        QVERIFY2(error.startsWith(QStringLiteral("rules.viasCovered: ")), qPrintable(error));

        error = errorOfPart("rules", "checks", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("rules.checks: ")), qPrintable(error));

        for (const char *key : {"clearance", "annularRing", "classWidth", "minimumWidth",
                                "edgeClearance"}) {
            const QString place = QStringLiteral("rules.checks.") + QLatin1String(key)
                                  + QStringLiteral(": ");
            error = errorOfCheck(key, QStringLiteral("fatal"));
            QVERIFY2(error.startsWith(place), qPrintable(error));
            error = errorOfCheck(key, REMOVED);
            QVERIFY2(error.startsWith(place), qPrintable(error));
            for (const char *level : {"error", "warning", "off"})
                QCOMPARE(errorOfCheck(key, QLatin1String(level)), QString());
        }
    }

    void gridErrors()
    {
        QString error = errorOfPart("grid", "board", 0);
        QVERIFY2(error.startsWith(QStringLiteral("grid.board: ")), qPrintable(error));
        error = errorOfPart("grid", "schematic", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("grid.schematic: ")), qPrintable(error));
        error = errorOfRoot("grid", REMOVED);
        QVERIFY2(error.startsWith(QStringLiteral("grid: ")), qPrintable(error));
    }

    void projectFileRoundTrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("test.ppcb"));
        const Project project = fullProject();

        QString error;
        QVERIFY2(ProjectFile::write(path, project, error), qPrintable(error));
        Project back;
        QVERIFY2(ProjectFile::read(path, back, error), qPrintable(error));
        QVERIFY(back == project);

        QJsonObject root;
        QVERIFY(JsonFile::read(path, root, error));
        QCOMPARE(fileTypeOf(root), QString::fromLatin1(FILE_TYPE_PROJECT));
    }

    void fileOfAnotherKindIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("board.ppcb"));
        QJsonObject root = projectToJson(fullProject());
        writeHeader(root, FILE_TYPE_BOARD);
        QString error;
        QVERIFY(JsonFile::write(path, root, error));

        Project project = fullProject();
        project.titleBlock.note = QStringLiteral("mine");
        const Project before = project;
        QVERIFY(!ProjectFile::read(path, project, error));
        QVERIFY2(error.startsWith(path), qPrintable(error));
        QVERIFY(project == before); // untouched
    }

    void newDesignIsComplete()
    {
        const Design design = newDesign(QStringLiteral("amp"));
        QCOMPARE(design.project.schematicFile, QStringLiteral("amp.ppsch"));
        QCOMPARE(design.project.boardFile, QStringLiteral("amp.ppbrd"));
        QCOMPARE(design.project.titleBlock.title, QStringLiteral("amp"));
        QVERIFY(design.project.rules == productionRules());
        QVERIFY(design.project.designatorStyle == DesignatorStyle::International);
        QCOMPARE(design.project.schematicGridStep, DEFAULT_GRID_STEP);
        QCOMPARE(design.project.boardGridStep, DEFAULT_BOARD_GRID_STEP);

        QCOMPARE(design.project.netClasses.size(), 2);
        QCOMPARE(design.project.netClasses.at(0).name, QString::fromLatin1(NET_CLASS_SIGNAL));
        QCOMPARE(design.project.netClasses.at(0).trackWidth, Coord(500000));
        QCOMPARE(design.project.netClasses.at(1).name, QString::fromLatin1(NET_CLASS_POWER));
        QCOMPARE(design.project.netClasses.at(1).trackWidth, Coord(1500000));
        QCOMPARE(design.project.defaultClass, QString::fromLatin1(NET_CLASS_SIGNAL));
        QCOMPARE(design.project.powerClass, QString::fromLatin1(NET_CLASS_POWER));

        QCOMPARE(design.schematic.sheets.size(), 1);
        QVERIFY(!design.schematic.sheets.at(0).id.isNull());
        QVERIFY(design.board == Board());

        // Two new designs do not share the id of their sheets.
        const Design other = newDesign(QStringLiteral("amp"));
        QVERIFY(other.schematic.sheets.at(0).id != design.schematic.sheets.at(0).id);
    }

    void designRoundTripThroughThreeFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        Design design = newDesign(QStringLiteral("amp"));
        design.project.titleBlock.author = QStringLiteral("Автор");
        design.schematic.sheets[0].title = QStringLiteral("Вход");
        design.board.outline = {OutlineCorner{Point{0, 0}, 0}, OutlineCorner{Point{NM_PER_MM, 0}, 0},
                                OutlineCorner{Point{0, NM_PER_MM}, 0}};
        addResistor(design);

        QString error;
        QVERIFY2(DesignFiles::write(path, design, error), qPrintable(error));
        QVERIFY(QFile::exists(path));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("amp.ppsch"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("amp.ppbrd"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("library/symbols/resistor.json"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("library/footprints/r-7.62.json"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("library/parts/resistor.json"))));

        Design back;
        QStringList problems;
        QVERIFY2(DesignFiles::read(path, back, error, &problems), qPrintable(error));
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QVERIFY(back == design);
    }

    // What is wrong with the library is told, but the project is read.
    void libraryProblemsDoNotStopReading()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        Design design = newDesign(QStringLiteral("amp"));
        addResistor(design);
        QString error;
        QVERIFY2(DesignFiles::write(path, design, error), qPrintable(error));

        // The part's symbol is gone, a footprint lies in the parts folder,
        // and a file is not a library file at all.
        QVERIFY(QFile::remove(dir.filePath(QStringLiteral("library/symbols/resistor.json"))));
        QVERIFY(QFile::copy(dir.filePath(QStringLiteral("library/footprints/r-7.62.json")),
                            dir.filePath(QStringLiteral("library/parts/r-7.62.json"))));
        QFile broken(dir.filePath(QStringLiteral("library/footprints/broken.json")));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not json");
        broken.close();

        Design back;
        QStringList problems;
        QVERIFY2(DesignFiles::read(path, back, error, &problems), qPrintable(error));
        QCOMPARE(problems.size(), 3);
        QVERIFY2(problems.at(0).startsWith(broken.fileName()), qPrintable(problems.at(0)));
        QVERIFY2(problems.at(1).startsWith(dir.filePath(QStringLiteral("library/parts/r-7.62.json"))),
                 qPrintable(problems.at(1)));
        QVERIFY2(problems.at(2).startsWith(QStringLiteral("library/symbols/resistor.json: ")),
                 qPrintable(problems.at(2)));
        QVERIFY(back.library.symbols.isEmpty());
        QCOMPARE(back.library.footprints.keys(), QStringList({QStringLiteral("r-7.62")}));
        QCOMPARE(back.library.parts.keys(), QStringList({QStringLiteral("resistor")}));
        QVERIFY(back.schematic == design.schematic);
        QVERIFY(back.board == design.board);
    }

    void missingLibraryItemsAreNamedOnce()
    {
        Design design = newDesign(QStringLiteral("amp"));
        addResistor(design);
        QVERIFY(missingLibraryItems(design).isEmpty());

        // A second component of a part the library lacks, and a placement
        // whose footprint it lacks too.
        Component other = design.schematic.sheets[0].components.at(0);
        other.part = QStringLiteral("diode");
        other.footprint = QString();
        design.schematic.sheets[0].components.append(other);
        design.schematic.sheets[0].components.append(other);
        design.board.placements[0].footprint = QStringLiteral("dip-8");
        const QStringList missing = missingLibraryItems(design);
        QCOMPARE(missing.size(), 2);
        QVERIFY2(missing.at(0).startsWith(QStringLiteral("library/parts/diode.json: ")),
                 qPrintable(missing.at(0)));
        QVERIFY2(missing.at(1).startsWith(QStringLiteral("library/footprints/dip-8.json: ")),
                 qPrintable(missing.at(1)));

        // The component's own footprint is looked up too.
        design.schematic.sheets[0].components[0].footprint = QStringLiteral("to-92");
        QVERIFY(missingLibraryItems(design).join(QLatin1Char(' '))
                    .contains(QStringLiteral("library/footprints/to-92.json")));
    }

    void designWithAMissingFileIsNotRead()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const Design design = newDesign(QStringLiteral("amp"));
        QString error;
        QVERIFY2(DesignFiles::write(path, design, error), qPrintable(error));

        // Something already open must survive a failed read.
        Design open = newDesign(QStringLiteral("other"));
        const Design before = open;

        const QString boardPath = dir.filePath(QStringLiteral("amp.ppbrd"));
        QVERIFY(QFile::remove(boardPath));
        QVERIFY(!DesignFiles::read(path, open, error));
        QVERIFY2(error.startsWith(boardPath), qPrintable(error));
        QVERIFY(open == before);

        // A schematic put in the place of the board is named too.
        const QString schematicPath = dir.filePath(QStringLiteral("amp.ppsch"));
        QVERIFY(QFile::copy(schematicPath, boardPath));
        QVERIFY(!DesignFiles::read(path, open, error));
        QVERIFY2(error.startsWith(boardPath), qPrintable(error));
        QVERIFY(open == before);

        QVERIFY(QFile::remove(schematicPath));
        QVERIFY(!DesignFiles::read(path, open, error));
        QVERIFY2(error.startsWith(schematicPath), qPrintable(error));
        QVERIFY(open == before);
    }

    void designIsNotWrittenIntoAMissingFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("no-such-folder/amp.ppcb"));
        QString error;
        QVERIFY(!DesignFiles::write(path, newDesign(QStringLiteral("amp")), error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFile::exists(path));
    }
};

QTEST_GUILESS_MAIN(TestProject)
#include "test_project.moc"
