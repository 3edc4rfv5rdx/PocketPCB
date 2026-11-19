#include "stephistory.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace ppcb {

namespace {

// Digits of the number a file name starts with, so that the names sort by it.
const int NUMBER_DIGITS = 6;
// Letters of the step name kept in a file name.
const int NAME_LETTERS = 40;

// 'name' fit for a file name: letters, digits, spaces, '-' and '_' kept,
// anything else made '_'.
QString fileNamePart(const QString &name)
{
    QString part;
    for (const QChar c : name.left(NAME_LETTERS)) {
        const bool kept = c.isLetterOrNumber() || c == QLatin1Char(' ') || c == QLatin1Char('-')
                          || c == QLatin1Char('_');
        part += kept ? c : QLatin1Char('_');
    }
    return part.trimmed();
}

} // namespace

bool writeStep(const QString &folder, const QString &name, int page, const QString &projectPath,
               WireRouting routing, const Schematic &before, const Schematic &after)
{
    QDir dir(folder);
    if (!dir.mkpath(QStringLiteral(".")))
        return false;
    const QStringList nameFilter = {QStringLiteral("*.json")};
    int last = 0;
    for (const QString &file : dir.entryList(nameFilter, QDir::Files))
        last = qMax(last, file.section(QLatin1Char(' '), 0, 0).toInt());

    QJsonObject root;
    root.insert(QStringLiteral("step"), name);
    root.insert(QStringLiteral("page"), page);
    root.insert(QStringLiteral("project"), projectPath);
    root.insert(QStringLiteral("routing"), routing == WireRouting::Hand
                                               ? QStringLiteral("hand")
                                               : QStringLiteral("grid"));
    root.insert(QStringLiteral("before"), schematicToJson(before));
    root.insert(QStringLiteral("after"), schematicToJson(after));

    const QString fileName = QStringLiteral("%1 %2.json")
                                 .arg(last + 1, NUMBER_DIGITS, 10, QLatin1Char('0'))
                                 .arg(fileNamePart(name));
    QSaveFile file(dir.filePath(fileName));
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
        return false;

    // The oldest go, by their numbers.
    const QStringList files = dir.entryList(nameFilter, QDir::Files, QDir::Name);
    for (int i = 0; i + STEP_HISTORY_KEPT < files.size(); ++i)
        dir.remove(files.at(i));
    return true;
}

} // namespace ppcb
