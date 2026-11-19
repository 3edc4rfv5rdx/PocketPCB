#include "stephistory.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

namespace {

// A schematic of one sheet titled 'title'.
Schematic titled(const QString &title)
{
    Schematic schematic;
    Sheet sheet;
    sheet.title = title;
    schematic.sheets = {sheet};
    return schematic;
}

QStringList stepFiles(const QString &folder)
{
    return QDir(folder).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
}

} // namespace

class TestStepHistory : public QObject
{
    Q_OBJECT

private slots:
    // A step goes into a numbered file named after it, made fit for a file
    // name, with the schematic before and after it.
    void aStepIsWrittenWithItsSchematics()
    {
        QTemporaryDir temp;
        const QString folder = QDir(temp.path()).filePath(QStringLiteral("history"));
        QVERIFY(writeStep(folder, QStringLiteral("Move R1/R2"), 1, QStringLiteral("/p/amp.ppcb"),
                          WireRouting::Grid, titled(QStringLiteral("a")),
                          titled(QStringLiteral("b"))));
        QCOMPARE(stepFiles(folder), QStringList({QStringLiteral("000001 Move R1_R2.json")}));

        QFile file(QDir(folder).filePath(stepFiles(folder).first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("step")).toString(), QStringLiteral("Move R1/R2"));
        QCOMPARE(root.value(QStringLiteral("page")).toInt(), 1);
        QCOMPARE(root.value(QStringLiteral("project")).toString(), QStringLiteral("/p/amp.ppcb"));
        QCOMPARE(root.value(QStringLiteral("routing")).toString(), QStringLiteral("grid"));
        QCOMPARE(root.value(QStringLiteral("before")).toObject(),
                 schematicToJson(titled(QStringLiteral("a"))));
        QCOMPARE(root.value(QStringLiteral("after")).toObject(),
                 schematicToJson(titled(QStringLiteral("b"))));
    }

    // The numbers go on from the highest in the folder, and only the newest
    // files are kept.
    void theNewestStepsAreKept()
    {
        QTemporaryDir temp;
        const QString folder = temp.path();
        for (int i = 0; i < STEP_HISTORY_KEPT + 2; ++i) {
            QVERIFY(writeStep(folder, QStringLiteral("Wire"), 0, QString(), WireRouting::Hand,
                              Schematic(), Schematic()));
        }
        const QStringList files = stepFiles(folder);
        QCOMPARE(files.size(), STEP_HISTORY_KEPT);
        QCOMPARE(files.first(), QStringLiteral("000003 Wire.json"));
        QCOMPARE(files.last(), QStringLiteral("000102 Wire.json"));
    }
};

QTEST_GUILESS_MAIN(TestStepHistory)
#include "test_stephistory.moc"
