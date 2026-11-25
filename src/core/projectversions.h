#pragma once

#include "project.h"

#include <QDateTime>
#include <QString>

namespace ppcb {

// The versions of a project: each a zip archive in its versions folder
// (DesignFiles::versionsPath()) holding the project as it lay on disk when
// the version was made (the project file, the schematic, the board and the
// library folder, under their names in the project's folder) and
// VERSION_INFO_FILE saying which version it is.
extern const char VERSION_INFO_FILE[];

// What a version is, as VERSION_INFO_FILE keeps it; for a version read
// back, also its archive, and whether that could be read.
struct ProjectVersion
{
    int number = 0;
    QDateTime made;
    QString revision;
    QString description;
    QString path;
    bool readable = true;
};

// "0003-20261125-revB.zip": the version's number, the day it was made and
// its revision (revisionInFileName()).
QString versionArchiveName(const ProjectVersion &version);

// Makes the next version of the project saved at 'projectPath', whose
// files 'project' names, from its files on disk: numbered after the highest
// in the folder (made when missing), made at 'made', with the revision of
// the title block and 'description'. 'path' is the archive written. False,
// with 'error' naming the file, when a file cannot be read or written.
bool saveProjectVersion(const QString &projectPath, const Project &project,
                        const QString &description, const QDateTime &made, QString &path,
                        QString &error);

// The same, but only when the files are not those of the newest version
// (by their names, sizes and CRC-32s); else 'path' is left empty and the
// result is true.
bool saveChangedProjectVersion(const QString &projectPath, const Project &project,
                               const QString &description, const QDateTime &made,
                               QString &path, QString &error);

// The versions of the project at 'projectPath', the newest first. One whose
// archive or VERSION_INFO_FILE cannot be read is listed by its archive and
// the number in its name, not 'readable'.
QList<ProjectVersion> projectVersions(const QString &projectPath);

} // namespace ppcb
