#pragma once

#include <QDate>
#include <QString>

class QWidget;

namespace ppcb {

class Document;
struct Design;

// The name of the command that writes the factory's files, for its menu
// item.
QString exportGerberTitle();

// The name of the archive for the factory:
// "<project>-rev<revision>-<yyyymmdd>-gerber.zip", the revision of the title block left out when empty, its characters
// other than letters, digits, '.', '-' and '_' made '_'.
QString gerberArchiveName(const QString &project, const QString &revision, const QDate &date);

// What keeps the board's files from being made, to tell the user; empty
// when nothing does.
QString gerberExportProblem(const Design &design);

// Asks which revision the files are made for, starting with 'revision', a
// Next button putting nextRevision() in; 'revision' is what was typed,
// trimmed. False when cancelled.
bool askGerberRevision(QWidget *parent, QString &revision);

// Writes the board's Gerber and drill files (gerberFiles()) into the
// "gerber" folder of the project's output folder, and all of them into an
// archive beside it (gerberArchiveName(), today). False when they could not
// be written; 'message' says what to tell the user either way.
bool exportGerber(const Document &document, QString &message);

} // namespace ppcb
