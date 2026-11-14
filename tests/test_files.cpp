#include "libraryfile.h"
#include "project.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtTest>

using namespace ppcb;

namespace {

// Set by CMake: the root of the source tree.
QString sourcePath(const QString &relative)
{
    return QDir(QStringLiteral(PPCB_SOURCE_DIR)).filePath(relative);
}

} // namespace

// The files kept in the repository are read by the program as they are.
class TestFiles : public QObject
{
    Q_OBJECT

private slots:
    void everyLibraryFileIsRead()
    {
        int count = 0;
        QDirIterator it(sourcePath(QStringLiteral("library")), {QStringLiteral("*.json")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            LibraryItem item;
            QString error;
            QVERIFY2(LibraryFile::read(path, item, error), qPrintable(error));

            // The folder says what the file is.
            const QString folder = QFileInfo(path).dir().dirName();
            if (folder == QLatin1String("symbols"))
                QVERIFY2(item.kind == LibraryKind::Symbol, qPrintable(path));
            else if (folder == QLatin1String("footprints"))
                QVERIFY2(item.kind == LibraryKind::Footprint, qPrintable(path));
            else if (folder == QLatin1String("parts"))
                QVERIFY2(item.kind == LibraryKind::Part, qPrintable(path));
            else
                QFAIL(qPrintable(QStringLiteral("a library file in an unknown folder: ") + path));
            ++count;
        }
        QVERIFY(count >= 2);
    }

    void resistorFootprintsHaveTheirPitch()
    {
        LibraryItem item;
        QString error;
        QVERIFY2(LibraryFile::read(sourcePath(QStringLiteral("library/footprints/resistor-10.16.json")),
                                   item, error),
                 qPrintable(error));
        QCOMPARE(item.footprint.pads.size(), 2);
        QCOMPARE(item.footprint.pads.at(1).at.x - item.footprint.pads.at(0).at.x, Coord(10160000));
        QCOMPARE(item.footprint.pads.at(0).number, QStringLiteral("1"));
        QCOMPARE(item.footprint.pads.at(1).number, QStringLiteral("2"));

        QVERIFY2(LibraryFile::read(sourcePath(QStringLiteral("library/footprints/resistor-vertical.json")),
                                   item, error),
                 qPrintable(error));
        QCOMPARE(item.footprint.pads.size(), 2);
        QCOMPARE(item.footprint.pads.at(1).at.x - item.footprint.pads.at(0).at.x, Coord(2540000));
    }

    void demoProjectIsReadAndHangsTogether()
    {
        Design design;
        QString error;
        QStringList problems;
        QVERIFY2(DesignFiles::read(sourcePath(QStringLiteral("examples/demo/demo.ppcb")), design,
                                   error, &problems),
                 qPrintable(error));
        // Its library has everything it uses, and nothing else.
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        QCOMPARE(design.library.parts.keys(), QStringList({QStringLiteral("resistor")}));
        QCOMPARE(design.library.symbols.keys(), QStringList({QStringLiteral("resistor")}));
        QCOMPARE(design.library.footprints.keys(),
                 QStringList({QStringLiteral("resistor-10.16"), QStringLiteral("resistor-vertical")}));
        QCOMPARE(design.schematic.sheets.size(), 1);
        const Sheet &sheet = design.schematic.sheets.at(0);
        QCOMPARE(sheet.components.size(), 2);
        QCOMPARE(design.board.placements.size(), 2);

        // Every placement stands for a component and uses its footprint.
        for (const Placement &placement : design.board.placements) {
            bool found = false;
            for (const Component &component : sheet.components) {
                if (component.id != placement.component)
                    continue;
                found = true;
                QCOMPARE(placement.footprint, component.footprint);
                QCOMPARE(placement.designator, component.designator);
                QCOMPARE(placement.value, component.value);
            }
            QVERIFY2(found, qPrintable(placement.designator));
        }

        // The footprints of the project are the ones of the library.
        for (auto it = design.library.footprints.constBegin();
             it != design.library.footprints.constEnd(); ++it) {
            const QString path = sourcePath(QStringLiteral("library/footprints/") + it.key()
                                            + QStringLiteral(".json"));
            LibraryItem item;
            QVERIFY2(LibraryFile::read(path, item, error), qPrintable(error));
            QVERIFY2(item.footprint == it.value(), qPrintable(it.key()));
        }

        // The nets given a class by hand are nets of the board.
        QStringList boardNets;
        for (const Placement &placement : design.board.placements)
            boardNets += placement.padNets.values();
        for (auto it = design.project.classOfNet.constBegin();
             it != design.project.classOfNet.constEnd(); ++it) {
            QVERIFY2(boardNets.contains(it.key()), qPrintable(it.key()));
        }
    }
};

QTEST_GUILESS_MAIN(TestFiles)
#include "test_files.moc"
