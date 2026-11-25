#include "projectversions.h"

#include "jsonio.h"
#include "zipfile.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace ppcb {

const char VERSION_INFO_FILE[] = "version.json";

namespace {

// Digits of the number a version's name starts with, so that the names
// sort by it.
const int NUMBER_DIGITS = 4;

const char KEY_NUMBER[] = "number";
const char KEY_MADE[] = "made";
const char KEY_REVISION[] = "revision";
const char KEY_DESCRIPTION[] = "description";

QByteArray versionInfo(const ProjectVersion &version)
{
    QJsonObject root;
    writeHeader(root, FILE_TYPE_VERSION);
    root.insert(QLatin1String(KEY_NUMBER), version.number);
    root.insert(QLatin1String(KEY_MADE), version.made.toString(Qt::ISODate));
    root.insert(QLatin1String(KEY_REVISION), version.revision);
    root.insert(QLatin1String(KEY_DESCRIPTION), version.description);
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace

QString versionArchiveName(const ProjectVersion &version)
{
    return QStringLiteral("%1-").arg(version.number, NUMBER_DIGITS, 10, QLatin1Char('0'))
           + version.made.date().toString(QStringLiteral("yyyyMMdd"))
           + revisionInFileName(version.revision) + QStringLiteral(".zip");
}

bool saveProjectVersion(const QString &projectPath, const Project &project,
                        const QString &description, const QDateTime &made, QString &path,
                        QString &error)
{
    QStringList sources = {projectPath, DesignFiles::schematicPath(projectPath, project),
                           DesignFiles::boardPath(projectPath, project)};
    QStringList library;
    QDirIterator it(DesignFiles::libraryPath(projectPath), QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        library.append(it.next());
    library.sort();
    sources += library;

    const QDir projectFolder = QFileInfo(projectPath).dir();
    QList<OutputFile> files;
    for (const QString &source : sources) {
        OutputFile file;
        file.name = projectFolder.relativeFilePath(source);
        if (!JsonFile::readData(source, file.data, error))
            return false;
        files.append(file);
    }

    const QString folder = DesignFiles::versionsPath(projectPath);
    if (!JsonFile::makeFolder(folder, error))
        return false;
    ProjectVersion version;
    version.number = highestFileNumber(folder, QStringLiteral("*.zip"), QLatin1Char('-')) + 1;
    version.made = made;
    version.revision = project.titleBlock.revision.trimmed();
    version.description = description;
    files.append({QLatin1String(VERSION_INFO_FILE), versionInfo(version)});

    path = QDir(folder).filePath(versionArchiveName(version));
    return JsonFile::writeData(path, zipArchive(files, made), error);
}

} // namespace ppcb
