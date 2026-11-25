#include "jsonio.h"
#include "objectid.h"
#include "projectversions.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

class TestProjectVersions : public QObject
{
    Q_OBJECT

private slots:
    void archiveIsNamedByNumberDayAndRevision()
    {
        ProjectVersion version;
        version.number = 3;
        version.made = QDateTime(QDate(2026, 11, 25), QTime(14, 30));
        QCOMPARE(versionArchiveName(version), QStringLiteral("0003-20261125.zip"));
        version.revision = QStringLiteral("B 2");
        QCOMPARE(versionArchiveName(version), QStringLiteral("0003-20261125-revB_2.zip"));
    }

    // Each version holds the project's files and library, not what is made
    // from it, and the numbers go on from the highest.
    void versionsHoldTheProjectAndGoOn()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectPath = QDir(dir.path()).filePath(QStringLiteral("amp.ppcb"));
        Design design = newDesign(QStringLiteral("amp"));
        QString error;
        QVERIFY2(DesignFiles::write(projectPath, design, error), qPrintable(error));
        const QString parts = QDir(DesignFiles::libraryPath(projectPath)).filePath(QStringLiteral("parts"));
        QVERIFY(QDir().mkpath(parts));
        QVERIFY(JsonFile::writeData(QDir(parts).filePath(QStringLiteral("diode.json")), "{}", error));
        const QString out = DesignFiles::outputPath(projectPath);
        QVERIFY(QDir().mkpath(out));
        QVERIFY(JsonFile::writeData(QDir(out).filePath(QStringLiteral("amp-bom.txt")), "bom", error));

        const QDateTime made(QDate(2026, 11, 25), QTime(14, 30));
        QString first;
        QVERIFY2(saveProjectVersion(projectPath, design.project, QStringLiteral("Start"), made,
                                    first, error),
                 qPrintable(error));
        QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("0001-20261125.zip"));
        QCOMPARE(QFileInfo(first).dir().absolutePath(),
                 QDir(DesignFiles::versionsPath(projectPath)).absolutePath());
        QByteArray zip;
        QVERIFY(JsonFile::readData(first, zip, error));
        // The names stand plain in the archive; what the files hold is packed.
        for (const char *name :
             {"amp.ppcb", "amp.ppsch", "amp.ppbrd", "library/parts/diode.json", "version.json"})
            QVERIFY2(zip.contains(name), name);
        QVERIFY(!zip.contains("amp-bom.txt"));

        // A stray file does not stop the numbering.
        QVERIFY(JsonFile::writeData(QDir(DesignFiles::versionsPath(projectPath))
                                        .filePath(QStringLiteral("notes.zip")),
                                    "", error));
        design.project.titleBlock.revision = QStringLiteral("B");
        QString second;
        QVERIFY(saveProjectVersion(projectPath, design.project, QString(), made, second, error));
        QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("0002-20261125-revB.zip"));
    }

    // A version only when the files changed since the newest one.
    void aChangedVersionOnlyWhenSomethingChanged()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectPath = QDir(dir.path()).filePath(QStringLiteral("amp.ppcb"));
        Design design = newDesign(QStringLiteral("amp"));
        QString error;
        QVERIFY(DesignFiles::write(projectPath, design, error));
        const QDateTime made(QDate(2026, 11, 25), QTime(14, 30));
        QString path;
        QVERIFY(saveChangedProjectVersion(projectPath, design.project, QStringLiteral("Gerber"),
                                          made, path, error));
        QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("0001-20261125.zip"));
        QVERIFY(saveChangedProjectVersion(projectPath, design.project, QStringLiteral("Gerber"),
                                          made, path, error));
        QVERIFY(path.isEmpty());

        design.project.titleBlock.revision = QStringLiteral("B");
        QVERIFY(DesignFiles::write(projectPath, design, error));
        QVERIFY(saveChangedProjectVersion(projectPath, design.project, QStringLiteral("Gerber"),
                                          made, path, error));
        QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("0002-20261125-revB.zip"));
    }

    // The versions are listed the newest first, with what each says; an
    // archive that cannot be read is listed by its number.
    void versionsAreListed()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectPath = QDir(dir.path()).filePath(QStringLiteral("amp.ppcb"));
        Design design = newDesign(QStringLiteral("amp"));
        QString error;
        QVERIFY(DesignFiles::write(projectPath, design, error));
        QCOMPARE(projectVersions(projectPath).size(), 0);
        const QDateTime first(QDate(2026, 11, 25), QTime(14, 30));
        QString path;
        QVERIFY(saveProjectVersion(projectPath, design.project, QStringLiteral("Start"), first,
                                   path, error));
        design.project.titleBlock.revision = QStringLiteral("B");
        const QDateTime second(QDate(2026, 11, 26), QTime(9, 0));
        QVERIFY(saveProjectVersion(projectPath, design.project, QStringLiteral("Gerber"), second,
                                   path, error));
        QVERIFY(JsonFile::writeData(QDir(DesignFiles::versionsPath(projectPath))
                                        .filePath(QStringLiteral("0003-20261126.zip")),
                                    "damaged", error));

        const QList<ProjectVersion> versions = projectVersions(projectPath);
        QCOMPARE(versions.size(), 3);
        QCOMPARE(versions.at(0).number, 3);
        QVERIFY(!versions.at(0).readable);
        QCOMPARE(versions.at(1).number, 2);
        QVERIFY(versions.at(1).readable);
        QCOMPARE(versions.at(1).made, second);
        QCOMPARE(versions.at(1).revision, QStringLiteral("B"));
        QCOMPARE(versions.at(1).description, QStringLiteral("Gerber"));
        QCOMPARE(versions.at(1).path, path);
        QCOMPARE(versions.at(2).number, 1);
        QCOMPARE(versions.at(2).made, first);
        QCOMPARE(versions.at(2).description, QStringLiteral("Start"));
    }

    // A version read back is the design it was made of, and the project
    // counts as changed since the newest version once its files change.
    void aVersionIsReadBack()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectPath = QDir(dir.path()).filePath(QStringLiteral("amp.ppcb"));
        Design design = newDesign(QStringLiteral("amp"));
        design.project.titleBlock.revision = QStringLiteral("A");
        QString error;
        QVERIFY(DesignFiles::write(projectPath, design, error));
        bool changed = false;
        QVERIFY(projectChangedSinceVersion(projectPath, design.project, changed, error));
        QVERIFY(changed); // no version yet
        const QDateTime made(QDate(2026, 11, 25), QTime(14, 30));
        QString path;
        QVERIFY(saveProjectVersion(projectPath, design.project, QStringLiteral("A"), made, path,
                                   error));
        QVERIFY(projectChangedSinceVersion(projectPath, design.project, changed, error));
        QVERIFY(!changed);

        Design later = design;
        later.project.titleBlock.revision = QStringLiteral("B");
        later.schematic.sheets.append(Sheet());
        later.schematic.sheets.last().id = newObjectId();
        QVERIFY(DesignFiles::write(projectPath, later, error));
        QVERIFY(projectChangedSinceVersion(projectPath, later.project, changed, error));
        QVERIFY(changed);

        Design restored;
        QVERIFY2(readProjectVersion(path, restored, error), qPrintable(error));
        Design expected;
        QVERIFY(DesignFiles::read(projectPath, expected, error)); // the reading of 'later'
        QVERIFY(restored != expected);
        QCOMPARE(restored.project.titleBlock.revision, QStringLiteral("A"));
        QCOMPARE(restored.schematic.sheets.size(), 1);

        QVERIFY(!readProjectVersion(QDir(dir.path()).filePath(QStringLiteral("none.zip")),
                                    restored, error));
    }

    // A file that cannot be read stops the version, naming the file.
    void aMissingFileIsReported()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString projectPath = QDir(dir.path()).filePath(QStringLiteral("amp.ppcb"));
        QString path;
        QString error;
        QVERIFY(!saveProjectVersion(projectPath, newDesign(QStringLiteral("amp")).project,
                                    QString(), QDateTime::currentDateTime(), path, error));
        QVERIFY(error.contains(QStringLiteral("amp.ppcb")));
        QVERIFY(!QDir(DesignFiles::versionsPath(projectPath)).exists());
    }
};

QTEST_GUILESS_MAIN(TestProjectVersions)
#include "test_projectversions.moc"
