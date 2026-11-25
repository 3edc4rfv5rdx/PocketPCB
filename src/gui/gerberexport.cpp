#include "gerberexport.h"

#include "document.h"
#include "format.h"
#include "gerber.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

namespace ppcb {

namespace {

const char GERBER_FOLDER[] = "gerber";

// Writes 'data' into 'path' whole, or leaves what was there.
bool writeFile(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

} // namespace

QString exportGerberTitle()
{
    return QCoreApplication::translate("ppcb::GerberExport", "Export Gerber and drill files");
}

QString gerberArchiveName(const QString &project, const QString &revision, const QDate &date)
{
    QString name = project;
    QString safe = revision.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (!safe.isEmpty())
        name += QStringLiteral("-rev") + safe;
    return name + QLatin1Char('-') + date.toString(QStringLiteral("yyyyMMdd"))
           + QStringLiteral("-gerber.zip");
}

bool exportGerber(const Document &document, QString &message)
{
    const Design &design = document.design();
    if (design.board.outline.isEmpty()) {
        message = QCoreApplication::translate("ppcb::GerberExport", "The board has no outline");
        return false;
    }
    // The output folder, made when missing.
    const QString anyOutput =
        projectOutputFile(document.projectPath(), QString(), QStringLiteral("zip"), message);
    if (anyOutput.isEmpty())
        return false;
    const QDir output = QFileInfo(anyOutput).dir();
    const QString folder = output.filePath(QLatin1String(GERBER_FOLDER));
    if (!QDir().mkpath(folder)) {
        message = cannotWriteMessage(folder);
        return false;
    }

    const QString name = projectName(document.projectPath());
    const QList<OutputFile> files = gerberFiles(design, name);
    for (const OutputFile &file : files) {
        const QString path = QDir(folder).filePath(file.name);
        if (!writeFile(path, file.data)) {
            message = cannotWriteMessage(path);
            return false;
        }
    }
    const QDateTime now = QDateTime::currentDateTime();
    const QString archive = output.filePath(
        gerberArchiveName(name, design.project.titleBlock.revision, now.date()));
    if (!writeFile(archive, zipArchive(files, now))) {
        message = cannotWriteMessage(archive);
        return false;
    }
    message = writtenMessage(archive);
    return true;
}

} // namespace ppcb
