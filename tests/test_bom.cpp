#include "bom.h"

#include <QtTest>

using namespace ppcb;

namespace {

Component component(quint64 id, const QString &designator, const QString &value,
                    int powerMw = 0, const QString &part = QStringLiteral("resistor"))
{
    Component result;
    result.id = ObjectId{id};
    result.part = part;
    result.footprint = QStringLiteral("resistor-10.16");
    result.designator = designator;
    result.value = value;
    result.powerMw = powerMw;
    return result;
}

} // namespace

class TestBom : public QObject
{
    Q_OBJECT

private slots:
    // R2 comes before R10, and C before R.
    void designatorsSortByTheirNumbers()
    {
        QVERIFY(designatorLess(QStringLiteral("R2"), QStringLiteral("R10")));
        QVERIFY(!designatorLess(QStringLiteral("R10"), QStringLiteral("R2")));
        QVERIFY(designatorLess(QStringLiteral("C9"), QStringLiteral("R1")));
    }

    // Parts alike on every sheet make one row; another value or power
    // makes another; rows go by their first designators.
    void alikePartsMakeOneRow()
    {
        Schematic schematic;
        Sheet first;
        first.components = {component(1, QStringLiteral("R10"), QStringLiteral("10k")),
                            component(2, QStringLiteral("R2"), QStringLiteral("10k")),
                            component(3, QStringLiteral("R3"), QStringLiteral("1k"))};
        Sheet second;
        second.components = {component(4, QStringLiteral("R1"), QStringLiteral("10k")),
                             component(5, QStringLiteral("R4"), QStringLiteral("10k"), 2000)};
        schematic.sheets = {first, second};
        const QList<BomRow> rows = bomRows(schematic, Library());
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).designators,
                 QStringList({QStringLiteral("R1"), QStringLiteral("R2"), QStringLiteral("R10")}));
        QCOMPARE(rows.at(0).ids, QSet<ObjectId>({ObjectId{1}, ObjectId{2}, ObjectId{4}}));
        QCOMPARE(rows.at(1).designators, QStringList({QStringLiteral("R3")}));
        QCOMPARE(rows.at(2).designators, QStringList({QStringLiteral("R4")}));
        QCOMPARE(rows.at(2).powerMw, 2000);
    }

    // The rows of a group stay together, the groups in the order of their
    // first designators, rows without a group last.
    void rowsGoByGroups()
    {
        Library library;
        Part resistor;
        resistor.group = QStringLiteral("resistors");
        Part capacitor;
        capacitor.group = QStringLiteral("capacitors");
        library.parts.insert(QStringLiteral("resistor"), resistor);
        library.parts.insert(QStringLiteral("trimmer"), resistor);
        library.parts.insert(QStringLiteral("capacitor"), capacitor);
        Schematic schematic;
        Sheet sheet;
        sheet.components = {
            component(1, QStringLiteral("RV1"), QStringLiteral("10k"), 0, QStringLiteral("trimmer")),
            component(2, QStringLiteral("R1"), QStringLiteral("1k")),
            component(3, QStringLiteral("C1"), QStringLiteral("1u"), 0, QStringLiteral("capacitor")),
            component(4, QStringLiteral("B1"), QString(), 0, QStringLiteral("unknown"))};
        schematic.sheets = {sheet};
        const QList<BomRow> rows = bomRows(schematic, library);
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows.at(0).designators, QStringList({QStringLiteral("C1")}));
        QCOMPARE(rows.at(1).designators, QStringList({QStringLiteral("R1")}));
        QCOMPARE(rows.at(2).designators, QStringList({QStringLiteral("RV1")}));
        QCOMPARE(rows.at(2).group, QStringLiteral("resistors"));
        QCOMPARE(rows.at(3).designators, QStringList({QStringLiteral("B1")}));
        QVERIFY(rows.at(3).group.isEmpty());
    }

    // The power most components have, the lower one on a tie; none
    // without powers.
    void commonPowerCountsComponents()
    {
        BomRow single;
        single.designators = {QStringLiteral("R1")};
        single.powerMw = 500;
        BomRow pair;
        pair.designators = {QStringLiteral("R2"), QStringLiteral("R3")};
        pair.powerMw = 250;
        BomRow none;
        none.designators = {QStringLiteral("C1"), QStringLiteral("C2"), QStringLiteral("C3")};
        QCOMPARE(commonPowerMw({single, pair, none}), 250);
        single.designators.append(QStringLiteral("R4"));
        QCOMPARE(commonPowerMw({single, pair, none}), 250);
        single.designators.append(QStringLiteral("R5"));
        QCOMPARE(commonPowerMw({single, pair, none}), 500);
        QCOMPARE(commonPowerMw({none}), 0);
    }

    // A field with a comma or a quote is quoted, its quotes doubled.
    void csvQuotesWhatNeedsIt()
    {
        const QList<QStringList> table = {{QStringLiteral("A"), QStringLiteral("B")},
                                          {QStringLiteral("R1, R2"), QStringLiteral("say \"hi\"")}};
        QCOMPARE(csvOf(table),
                 QStringLiteral("A,B\r\n\"R1, R2\",\"say \"\"hi\"\"\"\r\n"));
    }

    // Columns as wide as their widest field, a rule under the heading.
    void plainTextLinesUpColumns()
    {
        const QList<QStringList> table = {{QStringLiteral("Ref"), QStringLiteral("Qty")},
                                          {QStringLiteral("R1, R2"), QStringLiteral("2")}};
        QCOMPARE(plainTextOf(table), QStringLiteral("Ref     Qty\n"
                                                    "-----------\n"
                                                    "R1, R2  2\n"));
    }

    // A group heading is not measured and gets an empty line before it
    // unless it follows the rule.
    void plainTextSetsGroupHeadingsApart()
    {
        const QList<QStringList> table = {{QStringLiteral("Ref"), QStringLiteral("Qty")},
                                          {QStringLiteral("Capacitors")},
                                          {QStringLiteral("C1"), QStringLiteral("1")},
                                          {QStringLiteral("Resistors")},
                                          {QStringLiteral("R1"), QStringLiteral("1")}};
        QCOMPARE(plainTextOf(table), QStringLiteral("Ref  Qty\n"
                                                    "--------\n"
                                                    "Capacitors\n"
                                                    "C1   1\n"
                                                    "\n"
                                                    "Resistors\n"
                                                    "R1   1\n"));
    }
};

QTEST_GUILESS_MAIN(TestBom)
#include "test_bom.moc"
