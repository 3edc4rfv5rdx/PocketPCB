#include "erc.h"

#include <QtTest>

using namespace ppcb;

namespace {

const Coord MM = NM_PER_MM;

Point mm(Coord x, Coord y)
{
    return Point{x * MM, y * MM};
}

// A part with pins "1" at (-5, 0) and "2" at (5, 0) mm, and a value.
Library twoPinLibrary()
{
    Symbol symbol;
    Pin left;
    left.number = QStringLiteral("1");
    left.at = mm(-5, 0);
    left.side = PinSide::Left;
    Pin right = left;
    right.number = QStringLiteral("2");
    right.side = PinSide::Right;
    right.at = mm(5, 0);
    symbol.pins = {left, right};
    Library library;
    library.symbols.insert(QStringLiteral("two"), symbol);
    Part part;
    part.symbol = QStringLiteral("two");
    library.parts.insert(QStringLiteral("two"), part);
    Part plain = part;
    plain.noValue = true;
    library.parts.insert(QStringLiteral("plain"), plain);
    return library;
}

Component part(const QString &designator, const Point &at)
{
    Component component;
    component.id = newObjectId();
    component.part = QStringLiteral("two");
    component.designator = designator;
    component.value = QStringLiteral("1k");
    component.at = at;
    return component;
}

Wire wire(const QList<Point> &points)
{
    Wire result;
    result.id = newObjectId();
    result.points = points;
    return result;
}

Label label(const QString &text, const Point &at)
{
    Label result;
    result.id = newObjectId();
    result.text = text;
    result.at = at;
    return result;
}

NoConnect noConnect(const Point &at)
{
    NoConnect result;
    result.id = newObjectId();
    result.at = at;
    return result;
}

QList<ErcProblem> check(const Schematic &schematic, const Project &project = Project(),
                        const QStringList &links = {})
{
    const Library library = twoPinLibrary();
    return checkSchematic(schematic, library, project, nets(schematic, library, project), links);
}

// "UnconnectedPin R1.1": the kinds and subjects, one per line.
QStringList describe(const QList<ErcProblem> &problems)
{
    static const QStringList kinds = {
        QStringLiteral("EmptyValue"),       QStringLiteral("DuplicateDesignator"),
        QStringLiteral("LinkDesignator"),
        QStringLiteral("UnconnectedPin"),   QStringLiteral("MisplacedNoConnect"),
        QStringLiteral("LooseLabel"),       QStringLiteral("LooseWireEnd"),
        QStringLiteral("OverlappingWires"), QStringLiteral("WireLoop"),
        QStringLiteral("ShortedComponent"), QStringLiteral("TwoNames"),         QStringLiteral("LostTableName"),
        QStringLiteral("NameOfAnotherNet"),
    };
    QStringList lines;
    for (const ErcProblem &problem : problems)
        lines.append((kinds.at(int(problem.kind)) + QLatin1Char(' ') + problem.subject).trimmed());
    return lines;
}

// R1 at (0, 0) and R2 at (20, 0) joined by a wire from R1.2 to R2.1; the
// outer pins R1.1 at (-5, 0) and R2.2 at (25, 0) marked as free.
Sheet cleanSheet()
{
    Sheet sheet;
    sheet.components = {part(QStringLiteral("R1"), mm(0, 0)), part(QStringLiteral("R2"), mm(20, 0))};
    sheet.wires = {wire({mm(5, 0), mm(15, 0)})};
    sheet.noConnects = {noConnect(mm(-5, 0)), noConnect(mm(25, 0))};
    return sheet;
}

} // namespace

class TestErc : public QObject
{
    Q_OBJECT

private slots:
    void aCleanSchematicHasNoProblems()
    {
        Schematic schematic;
        schematic.sheets = {cleanSheet()};
        QCOMPARE(describe(check(schematic)), QStringList());
    }

    void aDesignatorOfAWireLink()
    {
        Schematic schematic;
        schematic.sheets = {cleanSheet()};
        const QList<ErcProblem> problems =
            check(schematic, Project(), {QStringLiteral("xx1"), QStringLiteral("R2")});
        QCOMPARE(describe(problems), QStringList({QStringLiteral("LinkDesignator R2")}));
        QCOMPARE(problems.at(0).sheet, 0);
        QVERIFY(problems.at(0).object == schematic.sheets.at(0).components.at(1).id);
    }

    void valuesAndDesignators()
    {
        Schematic schematic;
        Sheet first = cleanSheet();
        first.components[0].value.clear();
        // A part with no value is not asked for one.
        first.components[1].part = QStringLiteral("plain");
        first.components[1].value.clear();
        Sheet second = cleanSheet();
        second.components[0].designator = QStringLiteral("R3");
        schematic.sheets = {first, second};
        const QList<ErcProblem> problems = check(schematic);
        QCOMPARE(describe(problems), QStringList({QStringLiteral("EmptyValue R1"),
                                                  QStringLiteral("DuplicateDesignator R2"),
                                                  QStringLiteral("DuplicateDesignator R2")}));
        QCOMPARE(problems.at(0).sheet, 0);
        QVERIFY(problems.at(0).object == first.components.at(0).id);
        QCOMPARE(problems.at(2).sheet, 1);
        QVERIFY(problems.at(2).object == second.components.at(1).id);
    }

    void pinsJoinedToNothingAndPiecesOfWire()
    {
        Schematic schematic;
        Sheet sheet;
        sheet.components = {part(QStringLiteral("R1"), mm(0, 0))};
        // From R1.2 to nowhere.
        sheet.wires = {wire({mm(5, 0), mm(10, 0)})};
        schematic.sheets = {sheet};
        const QList<ErcProblem> problems = check(schematic);
        QCOMPARE(describe(problems),
                 QStringList({QStringLiteral("UnconnectedPin R1.1"),
                              QStringLiteral("UnconnectedPin R1.2"),
                              QStringLiteral("LooseWireEnd")}));
        QVERIFY(problems.at(0).at == mm(-5, 0));
        QVERIFY(problems.at(2).at == mm(10, 0));
        QVERIFY(problems.at(2).object == sheet.wires.first().id);

        // A label at the end names the wire: the end is held, and the pin is
        // on a named net.
        schematic.sheets[0].labels = {label(QStringLiteral("OUT"), mm(10, 0))};
        QCOMPARE(describe(check(schematic)), QStringList({QStringLiteral("UnconnectedPin R1.1")}));

        // A piece of wire on its own has two loose ends.
        Schematic piece;
        Sheet bare;
        bare.wires = {wire({mm(0, 30), mm(10, 30), mm(10, 40)})};
        piece.sheets = {bare};
        QCOMPARE(describe(check(piece)),
                 QStringList({QStringLiteral("LooseWireEnd"), QStringLiteral("LooseWireEnd")}));
        // A wire ending on the middle of another is held there.
        piece.sheets[0].wires.append(wire({mm(5, 30), mm(5, 20)}));
        QCOMPARE(describe(check(piece)),
                 QStringList({QStringLiteral("LooseWireEnd"), QStringLiteral("LooseWireEnd"),
                              QStringLiteral("LooseWireEnd")}));
    }

    void wiresLyingOnWires()
    {
        Schematic schematic;
        Sheet sheet = cleanSheet();
        // A piece over the middle of the wire from R1.2 to R2.1, its ends on it.
        Wire piece = wire({mm(7, 0), mm(11, 0)});
        sheet.wires.append(piece);
        schematic.sheets = {sheet};
        QList<ErcProblem> problems = check(schematic);
        QCOMPARE(describe(problems), QStringList({QStringLiteral("OverlappingWires")}));
        QVERIFY(problems.first().at == mm(9, 0));
        QVERIFY(problems.first().object == piece.id);

        // One that only goes on from its end, or ends on its middle square
        // on, does not lie on it; labels of one name hold their far ends.
        schematic.sheets[0].wires[1] = wire({mm(15, 0), mm(17, 0), mm(17, 10)});
        schematic.sheets[0].wires.append(wire({mm(10, 0), mm(10, -10)}));
        schematic.sheets[0].labels = {label(QStringLiteral("X"), mm(17, 10)),
                                      label(QStringLiteral("X"), mm(10, -10))};
        QCOMPARE(describe(check(schematic)), QStringList());

        // A wire going back over itself, at 45 degrees.
        Schematic back;
        Sheet slanted;
        slanted.wires = {wire({mm(0, 0), mm(4, 4), mm(2, 2)})};
        back.sheets = {slanted};
        problems = check(back);
        QCOMPARE(describe(problems), QStringList({QStringLiteral("LooseWireEnd"),
                                                  QStringLiteral("LooseWireEnd"),
                                                  QStringLiteral("OverlappingWires")}));
        QVERIFY(problems.last().at == mm(3, 3));
    }

    void wiresClosingALoop()
    {
        Schematic schematic;
        Sheet sheet = cleanSheet();
        // A second way from R1.2 to R2.1, under the first.
        Wire around = wire({mm(5, 0), mm(5, 10), mm(15, 10), mm(15, 0)});
        sheet.wires.append(around);
        schematic.sheets = {sheet};
        QList<ErcProblem> problems = check(schematic);
        QCOMPARE(describe(problems), QStringList({QStringLiteral("WireLoop")}));
        QVERIFY(problems.first().at == mm(15, 5));
        QVERIFY(problems.first().object == around.id);

        // Ending on the middle of the first wire closes one as well.
        schematic.sheets[0].wires.last() = wire({mm(5, 0), mm(5, 10), mm(10, 10), mm(10, 0)});
        QCOMPARE(describe(check(schematic)), QStringList({QStringLiteral("WireLoop")}));
        // Crossing it does not.
        schematic.sheets[0].wires.last() = wire({mm(10, 10), mm(10, -10)});
        schematic.sheets[0].labels = {label(QStringLiteral("X"), mm(10, 10)),
                                      label(QStringLiteral("X"), mm(10, -10))};
        QCOMPARE(describe(check(schematic)), QStringList());
    }

    void aComponentWithBothPinsOnOneNet()
    {
        Schematic schematic;
        Sheet sheet;
        sheet.components = {part(QStringLiteral("R1"), mm(0, 0))};
        sheet.wires = {wire({mm(-5, 0), mm(-5, 5), mm(5, 5), mm(5, 0)})};
        schematic.sheets = {sheet};
        const QList<ErcProblem> problems = check(schematic);
        QCOMPARE(describe(problems), QStringList({QStringLiteral("ShortedComponent R1")}));
        QVERIFY(problems.first().object == sheet.components.first().id);
    }

    void noConnectMarksBelongOnFreePins()
    {
        Schematic schematic;
        Sheet sheet = cleanSheet();
        // On a joined pin, and on no pin at all.
        sheet.noConnects.append(noConnect(mm(5, 0)));
        sheet.noConnects.append(noConnect(mm(50, 50)));
        schematic.sheets = {sheet};
        const QList<ErcProblem> problems = check(schematic);
        QCOMPARE(describe(problems), QStringList({QStringLiteral("MisplacedNoConnect R1.2"),
                                                  QStringLiteral("MisplacedNoConnect")}));
        QVERIFY(problems.at(1).object == sheet.noConnects.last().id);
    }

    void looseLabels()
    {
        Schematic schematic;
        Sheet sheet = cleanSheet();
        sheet.labels = {label(QStringLiteral("A"), mm(10, 0)), label(QStringLiteral("B"), mm(10, 10))};
        schematic.sheets = {sheet};
        QCOMPARE(describe(check(schematic)), QStringList({QStringLiteral("LooseLabel B")}));
    }

    void aNetWithTwoNames()
    {
        Schematic schematic;
        Sheet sheet = cleanSheet();
        sheet.labels = {label(QStringLiteral("B"), mm(8, 0)), label(QStringLiteral("A"), mm(12, 0))};
        schematic.sheets = {sheet};
        const QList<ErcProblem> problems = check(schematic);
        QCOMPARE(describe(problems), QStringList({QStringLiteral("TwoNames A, B")}));
        QCOMPARE(problems.first().sheet, 0);
        QVERIFY(problems.first().object == sheet.labels.at(0).id);
    }

    void namesOfTheTableOfNets()
    {
        Schematic schematic;
        Sheet sheet = cleanSheet();
        // R3 to R4: another net with no name.
        sheet.components.append(part(QStringLiteral("R3"), mm(0, 20)));
        sheet.components.append(part(QStringLiteral("R4"), mm(20, 20)));
        sheet.wires.append(wire({mm(5, 20), mm(15, 20)}));
        sheet.noConnects.append(noConnect(mm(-5, 20)));
        sheet.noConnects.append(noConnect(mm(25, 20)));
        schematic.sheets = {sheet};

        Project project;
        // NN1 is held by R1.2 and R2.1; NN2 by pins of a component gone.
        project.netsByPins = {
            NetByPins{QStringLiteral("NN1"), {PinRef{sheet.components.at(0).id, QStringLiteral("2")}}},
            NetByPins{QStringLiteral("NN2"), {PinRef{newObjectId(), QStringLiteral("1")}}},
        };
        // A name with no class names a net for the board only: lost, it loses nothing.
        QVERIFY(check(schematic, project).isEmpty());
        project.classOfNet.insert(QStringLiteral("NN2"), QStringLiteral("Power"));
        QCOMPARE(describe(check(schematic, project)), QStringList({QStringLiteral("LostTableName NN2")}));

        // A label naming the other net NN1.
        schematic.sheets[0].labels = {label(QStringLiteral("NN1"), mm(10, 20))};
        const QList<ErcProblem> problems = check(schematic, project);
        QCOMPARE(describe(problems), QStringList({QStringLiteral("NameOfAnotherNet NN1"),
                                                  QStringLiteral("LostTableName NN2")}));
        QCOMPARE(problems.first().sheet, 0);
        QCOMPARE(problems.last().sheet, -1);
    }
};

QTEST_GUILESS_MAIN(TestErc)
#include "test_erc.moc"
