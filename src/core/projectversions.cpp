#include "projectversions.h"

#include "jsonio.h"
#include "zipfile.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>

#include <algorithm>
#include <climits>

namespace ppcb {

const char VERSION_INFO_FILE[] = "version.json";

namespace {

// Digits of the number a version's name starts with, so that the names
// sort by it; the names of versions, and what ends the number in them.
const int NUMBER_DIGITS = 4;
const char VERSION_FILTER[] = "*.zip";
const char NUMBER_END = '-';

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

int highestVersion(const QString &folder)
{
    return highestFileNumber(folder, QLatin1String(VERSION_FILTER), QLatin1Char(NUMBER_END));
}

// The files of the project at 'projectPath' on disk, named as in its
// folder: the project file, the schematic, the board, and the library
// folder's files in the order of their names.
bool projectFiles(const QString &projectPath, const Project &project, QList<OutputFile> &files,
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
    files.clear();
    for (const QString &source : sources) {
        OutputFile file;
        file.name = projectFolder.relativeFilePath(source);
        if (!JsonFile::readData(source, file.data, error))
            return false;
        files.append(file);
    }
    return true;
}

// The archive of the newest version in 'folder'; empty when there is none.
QString newestVersion(const QString &folder)
{
    const int number = highestVersion(folder);
    if (number == 0)
        return QString();
    const QDir dir(folder);
    for (const QString &name : dir.entryList({QLatin1String(VERSION_FILTER)}, QDir::Files)) {
        if (name.section(QLatin1Char(NUMBER_END), 0, 0).toInt() == number)
            return dir.filePath(name);
    }
    return QString();
}

// True when the archive 'path' holds 'files' and nothing else beside its
// VERSION_INFO_FILE, judged by their names, sizes and CRC-32s.
bool versionHolds(const QString &path, const QList<OutputFile> &files)
{
    QByteArray archive;
    QString unused;
    QList<ZipEntry> entries;
    if (!JsonFile::readData(path, archive, unused) || !zipEntries(archive, entries))
        return false;
    QMap<QString, ZipEntry> held;
    for (const ZipEntry &entry : entries) {
        if (entry.name != QLatin1String(VERSION_INFO_FILE))
            held.insert(entry.name, entry);
    }
    if (held.size() != files.size())
        return false;
    for (const OutputFile &file : files) {
        const auto found = held.constFind(file.name);
        if (found == held.cend() || found->size != quint32(file.data.size())
            || found->crc != crc32(file.data))
            return false;
    }
    return true;
}

// The version in the archive 'path', from its VERSION_INFO_FILE.
bool readVersion(const QString &path, ProjectVersion &version)
{
    QByteArray archive;
    QString unused;
    QList<ZipEntry> entries;
    if (!JsonFile::readData(path, archive, unused) || !zipEntries(archive, entries))
        return false;
    for (const ZipEntry &entry : entries) {
        if (entry.name != QLatin1String(VERSION_INFO_FILE))
            continue;
        QByteArray data;
        if (!zipFileData(archive, entry, data))
            return false;
        const QJsonDocument document = QJsonDocument::fromJson(data);
        if (!document.isObject())
            return false;
        const QJsonObject root = document.object();
        JsonReader reader;
        reader.header(root, FILE_TYPE_VERSION);
        version.number = reader.integer(root, KEY_NUMBER, QString(), 1, INT_MAX);
        version.made = QDateTime::fromString(reader.string(root, KEY_MADE, QString()),
                                             Qt::ISODate);
        version.revision = reader.string(root, KEY_REVISION, QString());
        version.description = reader.string(root, KEY_DESCRIPTION, QString());
        return reader.ok() && version.made.isValid();
    }
    return false;
}

// Writes 'files' of the project at 'projectPath' as its next version.
bool writeVersion(const QString &projectPath, QList<OutputFile> files, const QString &revision,
                  const QString &description, const QDateTime &made, QString &path,
                  QString &error)
{
    const QString folder = DesignFiles::versionsPath(projectPath);
    if (!JsonFile::makeFolder(folder, error))
        return false;
    ProjectVersion version;
    version.number = highestVersion(folder) + 1;
    version.made = made;
    version.revision = revision.trimmed();
    version.description = description;
    files.append({QLatin1String(VERSION_INFO_FILE), versionInfo(version)});

    path = QDir(folder).filePath(versionArchiveName(version));
    return JsonFile::writeData(path, zipArchive(files, made), error);
}

} // namespace

QString versionArchiveName(const ProjectVersion &version)
{
    return QStringLiteral("%1").arg(version.number, NUMBER_DIGITS, 10, QLatin1Char('0'))
           + QLatin1Char(NUMBER_END) + version.made.date().toString(QStringLiteral("yyyyMMdd"))
           + revisionInFileName(version.revision) + QStringLiteral(".zip");
}

bool saveProjectVersion(const QString &projectPath, const Project &project,
                        const QString &description, const QDateTime &made, QString &path,
                        QString &error)
{
    QList<OutputFile> files;
    return projectFiles(projectPath, project, files, error)
           && writeVersion(projectPath, files, project.titleBlock.revision, description, made,
                           path, error);
}

QList<ProjectVersion> projectVersions(const QString &projectPath)
{
    QList<ProjectVersion> versions;
    const QDir dir(DesignFiles::versionsPath(projectPath));
    for (const QString &name : dir.entryList({QLatin1String(VERSION_FILTER)}, QDir::Files)) {
        ProjectVersion version;
        const QString path = dir.filePath(name);
        if (!readVersion(path, version)) {
            version = ProjectVersion();
            version.number = name.section(QLatin1Char(NUMBER_END), 0, 0).toInt();
            version.readable = false;
        }
        version.path = path;
        versions.append(version);
    }
    std::sort(versions.begin(), versions.end(),
              [](const ProjectVersion &a, const ProjectVersion &b) { return a.number > b.number; });
    return versions;
}

bool saveChangedProjectVersion(const QString &projectPath, const Project &project,
                               const QString &description, const QDateTime &made,
                               QString &path, QString &error)
{
    path.clear();
    QList<OutputFile> files;
    if (!projectFiles(projectPath, project, files, error))
        return false;
    const QString newest = newestVersion(DesignFiles::versionsPath(projectPath));
    if (!newest.isEmpty() && versionHolds(newest, files))
        return true;
    return writeVersion(projectPath, files, project.titleBlock.revision, description, made, path,
                        error);
}

} // namespace ppcb
