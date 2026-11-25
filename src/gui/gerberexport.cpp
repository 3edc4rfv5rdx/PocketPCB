#include "gerberexport.h"

#include "document.h"
#include "format.h"
#include "gerber.h"
#include "jsonio.h"
#include "paint.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QLineEdit>

namespace ppcb {

namespace {

const char GERBER_FOLDER[] = "gerber";

// Writes 'data' into 'path' whole, or leaves what was there.
bool writeFile(const QString &path, const QByteArray &data)
{
    QString unused;
    return JsonFile::writeData(path, data, unused);
}

} // namespace

QString exportGerberTitle()
{
    return QCoreApplication::translate("ppcb::GerberExport", "Export Gerber and drill files");
}

QString gerberArchiveName(const QString &project, const QString &revision, const QDate &date)
{
    return project + revisionInFileName(revision) + QLatin1Char('-') + date.toString(QStringLiteral("yyyyMMdd"))
           + QStringLiteral("-gerber.zip");
}

QString gerberExportProblem(const Design &design)
{
    if (design.board.outline.isEmpty())
        return QCoreApplication::translate("ppcb::GerberExport", "The board has no outline");
    return QString();
}

bool askGerberRevision(QWidget *parent, QString &revision)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(exportGerberTitle());
    auto *edit = new QLineEdit(revision, &dialog);
    edit->setObjectName(QStringLiteral("gerberRevisionEdit"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QFormLayout(&dialog);
    layout->addRow(revisionWord() + QLatin1Char(':'), revisionRow(edit));
    layout->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted)
        return false;
    revision = edit->text().trimmed();
    return true;
}

bool exportGerber(const Document &document, QString &message)
{
    const Design &design = document.design();
    message = gerberExportProblem(design);
    if (!message.isEmpty())
        return false;
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
