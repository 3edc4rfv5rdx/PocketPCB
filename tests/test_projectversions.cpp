#include "jsonio.h"
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
