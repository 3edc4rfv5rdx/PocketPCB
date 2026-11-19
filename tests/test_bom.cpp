#include "bom.h"

#include <QtTest>

using namespace ppcb;

namespace {

Component component(quint64 id, const QString &designator, const QString &value,
                    int powerMw = 0)
{
    Component result;
    result.id = ObjectId{id};
    result.part = QStringLiteral("resistor");
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
        const QList<BomRow> rows = bomRows(schematic);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).designators,
                 QStringList({QStringLiteral("R1"), QStringLiteral("R2"), QStringLiteral("R10")}));
        QCOMPARE(rows.at(0).ids, QSet<ObjectId>({ObjectId{1}, ObjectId{2}, ObjectId{4}}));
        QCOMPARE(rows.at(1).designators, QStringList({QStringLiteral("R3")}));
        QCOMPARE(rows.at(2).designators, QStringList({QStringLiteral("R4")}));
        QCOMPARE(rows.at(2).powerMw, 2000);
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
};

QTEST_GUILESS_MAIN(TestBom)
#include "test_bom.moc"
