#include "document.h"

#include "wiring.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QFileInfo>

#include <algorithm>

namespace ppcb {

namespace {

const char NEW_PROJECT_NAME[] = "untitled";
const char BACKUP_EXT[] = ".bak";

// Characters no file name may hold on Windows; '/' also on Linux.
const char FORBIDDEN_NAME_CHARS[] = "/\\:*?\"<>|";

// Names Windows keeps for devices.
const char *const RESERVED_NAMES[] = {
    "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
    "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

// Keeps 'path' as it is now as the newest of 'count' backup copies in
// 'folder', before it is replaced. Nothing to do for a file not there yet.
bool backUp(const QString &path, const QString &folder, int count, QString &error)
{
    if (count <= 0 || !QFileInfo::exists(path))
        return true;
    if (!JsonFile::makeFolder(folder, error))
        return false;
    const QString name = QFileInfo(path).fileName();
    const auto copyPath = [&folder, &name](int number) {
        return QDir(folder).filePath(name + QLatin1Char('.') + QString::number(number)
                                     + QLatin1String(BACKUP_EXT));
    };
    // The oldest goes, with any left from a time more were kept.
    for (int number = count; QFileInfo::exists(copyPath(number)); ++number)
        QFile::remove(copyPath(number));
    for (int number = count - 1; number >= 1; --number) {
        if (QFileInfo::exists(copyPath(number)))
            QFile::rename(copyPath(number), copyPath(number + 1));
    }
    if (!QFile::copy(path, copyPath(1))) {
        error = withPlace(copyPath(1), Document::tr("cannot make the backup copy"));
        return false;
    }
    return true;
}

// Removes the file 'name' from 'folder'; a name with a folder in it is left
// alone, since the names come from a file.
void removeFileIn(QDir folder, const QString &name)
{
    if (!name.isEmpty() && QFileInfo(name).fileName() == name)
        folder.remove(name);
}

// Removes from the library folder 'library', of the kind 'subfolder', the
// files of 'onDisk' whose content is an item of 'owned' of that name: what
// an autosave wrote. A file with other content is not the autosave's.
template <typename Item>
void removeOwnedItems(const QDir &library, const char *subfolder,
                      const QMap<QString, Item> &onDisk,
                      const QList<const QMap<QString, Item> *> &owned)
{
    QDir items(library.filePath(QLatin1String(subfolder)));
    for (auto it = onDisk.constBegin(); it != onDisk.constEnd(); ++it) {
        const bool ours = std::any_of(owned.cbegin(), owned.cend(), [&it](const auto *map) {
            const auto found = map->constFind(it.key());
            return found != map->constEnd() && *found == it.value();
        });
        if (ours)
            items.remove(it.key() + QLatin1String(LIBRARY_FILE_EXT));
    }
    library.rmdir(QLatin1String(subfolder));
}

// Removes what an autosave wrote for the project whose copy is 'copyPath':
// the copy's project file and the schematic and board files it names (else
// 'fallback' names them), and, when no other project's copy is left beside
// it, the library files that hold an item of one of 'owned' as it is there:
// those the copies wrote. Folders go only when left empty, so whatever else
// lies in them stays.
void removeAutosaveCopy(const QString &copyPath, const Project &fallback,
                        const QList<const Library *> &owned)
{
    const QFileInfo copy(copyPath);
    const QDir folder = copy.dir();
    if (!folder.exists())
        return;
    Project names = fallback;
    QString error;
    ProjectFile::read(copyPath, names, error);
    removeFileIn(folder, copy.fileName());
    removeFileIn(folder, names.schematicFile);
    removeFileIn(folder, names.boardFile);

    // The library folder is one for every project in the folder.
    const QStringList otherCopies = folder.entryList(
        {QStringLiteral("*") + QLatin1String(FILE_EXT_PROJECT)}, QDir::Files);
    if (otherCopies.isEmpty()) {
        const QDir library(folder.filePath(QLatin1String(PROJECT_LIBRARY_FOLDER)));
        QStringList problems;
        const Library onDisk = LibraryFolder::read(library.path(), problems);
        QList<const QMap<QString, Symbol> *> symbols;
        QList<const QMap<QString, Footprint> *> footprints;
        QList<const QMap<QString, Part> *> parts;
        for (const Library *items : owned) {
            symbols.append(&items->symbols);
            footprints.append(&items->footprints);
            parts.append(&items->parts);
        }
        removeOwnedItems(library, LIBRARY_SYMBOLS_FOLDER, onDisk.symbols, symbols);
        removeOwnedItems(library, LIBRARY_FOOTPRINTS_FOLDER, onDisk.footprints, footprints);
        removeOwnedItems(library, LIBRARY_PARTS_FOLDER, onDisk.parts, parts);
        folder.rmdir(QLatin1String(PROJECT_LIBRARY_FOLDER));
    }
    QDir parent = folder;
    if (parent.cdUp())
        parent.rmdir(folder.dirName());
}

} // namespace

// One step of the history: the whole content before and after it.
class DesignCommand : public QUndoCommand
{
public:
    DesignCommand(Document *document, const QString &text, const Design &before,
                  const Design &after, int page, const QSet<ObjectId> &ids)
        : QUndoCommand(text)
        , m_document(document)
        , m_before(before)
        , m_after(after)
        , m_page(page)
        , m_ids(ids)
    {
    }

    void undo() override
    {
        m_document->install(m_before);
        emit m_document->stepApplied(m_page, m_ids);
    }
    // Also called by QUndoStack::push(), which is what applies a change:
    // the change is in front of the user then, so nothing is reported.
    void redo() override
    {
        m_document->install(m_after);
        if (m_pushed)
            emit m_document->stepApplied(m_page, m_ids);
        m_pushed = true;
    }

private:
    Document *m_document;
    Design m_before;
    Design m_after;
    int m_page;
    QSet<ObjectId> m_ids;
    bool m_pushed = false;
};

Document::Document(QObject *parent)
    : QObject(parent)
{
    m_undoStack.setUndoLimit(UNDO_LIMIT);
    startNew();
}

bool Document::isModified() const
{
    return m_design != m_saved;
}

void Document::change(const QString &text, const Design &changed, int page,
                      const QSet<ObjectId> &ids)
{
    // The file names are not the caller's to change.
    Design after = changed;
    after.project.schematicFile = m_design.project.schematicFile;
    after.project.boardFile = m_design.project.boardFile;
    for (Sheet &sheet : after.schematic.sheets)
        sheet = withWiresMerged(sheet);
    if (after == m_design)
        return;
    const Design before = m_design;
    m_undoStack.push(new DesignCommand(this, text, m_design, after, page, ids));
    emit stepMade(text, page, before);
}

void Document::startNew()
{
    startNew(QLatin1String(NEW_PROJECT_NAME));
}

void Document::startNew(const QString &baseName)
{
    reset(newDesign(baseName), QString());
}

TitleBlock Document::titleBlockShown() const
{
    TitleBlock block = m_design.project.titleBlock;
    if (block.date.isEmpty() && m_savedDate.isValid())
        block.date = m_savedDate.toString(Qt::ISODate);
    return block;
}

bool Document::open(const QString &projectPath, QString &error, QStringList *problems)
{
    Design design;
    if (!DesignFiles::read(projectPath, design, error, problems))
        return false;
    reset(design, projectPath);
    return true;
}

bool Document::save(QString &error)
{
    if (m_projectPath.isEmpty()) {
        error = tr("the project has no file yet");
        return false;
    }
    if (!write(m_projectPath, false, error))
        return false;
    dropAutosave();
    return true;
}

bool Document::saveAs(const QString &projectPath, QString &error)
{
    // The schematic and the board take their names from it.
    const QString baseName = QFileInfo(projectPath).completeBaseName();
    if (baseName.isEmpty()) {
        error = withPlace(projectPath, tr("the project file needs a name"));
        return false;
    }
    const Design oldSaved = m_saved;
    const QDate oldSavedDate = m_savedDate;
    const QString oldSchematicFile = m_design.project.schematicFile;
    const QString oldBoardFile = m_design.project.boardFile;
    m_design.project.schematicFile = baseName + QLatin1String(FILE_EXT_SCHEMATIC);
    m_design.project.boardFile = baseName + QLatin1String(FILE_EXT_BOARD);

    if (!write(projectPath, true, error)) {
        // Still the project of the old place: what reached the new one
        // before the failure does not count as saved.
        m_saved = oldSaved;
        m_design.project.schematicFile = oldSchematicFile;
        m_design.project.boardFile = oldBoardFile;
        if (m_savedDate != oldSavedDate) {
            m_savedDate = oldSavedDate;
            emit savedDateChanged();
        }
        noteModified();
        return false;
    }
    // The changes are saved, if elsewhere: the old place has none to restore.
    dropAutosave();
    m_projectPath = projectPath;
    return true;
}

bool Document::autosave(QString &error)
{
    if (m_projectPath.isEmpty() || !isModified() || m_design == m_autosaved)
        return true;
    const QString path = DesignFiles::autosaveProjectPath(m_projectPath);
    if (!JsonFile::makeFolder(QFileInfo(path).absolutePath(), error))
        return false;
    if (!DesignFiles::write(path, m_design, error))
        return false;
    m_autosaved = m_design;
    return true;
}

void Document::dropAutosave()
{
    // The library of the copy is the one last autosaved, or, for a copy left
    // by an earlier session, most likely the project's own.
    const Design autosaved = m_autosaved;
    m_autosaved = Design();
    if (m_projectPath.isEmpty())
        return;
    removeAutosaveCopy(DesignFiles::autosaveProjectPath(m_projectPath), m_design.project,
                       {&autosaved.library, &m_design.library});
}

bool Document::hasAutosave(const QString &projectPath)
{
    if (projectPath.isEmpty())
        return false;
    const QFileInfo copy(DesignFiles::autosaveProjectPath(projectPath));
    return copy.exists() && copy.lastModified() >= QFileInfo(projectPath).lastModified();
}

bool Document::restoreAutosave(QString &error, QStringList *problems)
{
    if (m_projectPath.isEmpty()) {
        error = tr("the project has no file yet");
        return false;
    }
    Design design;
    if (!DesignFiles::read(DesignFiles::autosaveProjectPath(m_projectPath), design, error,
                           problems)) {
        return false;
    }
    m_autosaved = design;
    install(design);
    return true;
}

QString Document::projectPathInFolder(const QString &parentFolder, const QString &name)
{
    return QDir(QDir(parentFolder).filePath(name)).filePath(name + QLatin1String(FILE_EXT_PROJECT));
}

QString Document::nameProblem(const QString &name)
{
    if (name.isEmpty())
        return tr("the project needs a name");
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || QLatin1String(FORBIDDEN_NAME_CHARS).contains(c))
            return tr("the name must not contain %1").arg(QLatin1String(FORBIDDEN_NAME_CHARS));
    }
    if (name.startsWith(QLatin1Char('.')))
        return tr("the name must not start with a dot");
    if (name.startsWith(QLatin1Char(' ')))
        return tr("the name must not start with a space");
    if (name.endsWith(QLatin1Char(' ')) || name.endsWith(QLatin1Char('.')))
        return tr("the name must not end with a space or a dot");
    // Windows takes these for devices, whatever follows the first dot.
    const QString device = name.section(QLatin1Char('.'), 0, 0).toUpper();
    for (const char *reserved : RESERVED_NAMES) {
        if (device == QLatin1String(reserved))
            return tr("the name is reserved by Windows");
    }
    return QString();
}

QString Document::newFolderProblem(const QString &parentFolder, const QString &name)
{
    const QString problem = nameProblem(name);
    if (!problem.isEmpty())
        return problem;
    if (parentFolder.isEmpty())
        return tr("the project needs a folder");
    if (QDir::isRelativePath(parentFolder))
        return withPlace(parentFolder, tr("the folder must be a full path"));
    const QString folder = QDir(parentFolder).filePath(name);
    const QFileInfo info(folder);
    if (!info.exists())
        return QString();
    if (!info.isDir())
        return withPlace(folder, tr("a file with this name is already there"));
    if (!QDir(folder).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                              | QDir::System))
        return withPlace(folder, tr("the folder already exists and is not empty"));
    return QString();
}

bool Document::saveAsNewFolder(const QString &parentFolder, const QString &name, QString &error)
{
    error = newFolderProblem(parentFolder, name);
    if (!error.isEmpty())
        return false;
    if (!JsonFile::makeFolder(QDir(parentFolder).filePath(name), error))
        return false;
    return saveAs(projectPathInFolder(parentFolder, name), error);
}

void Document::install(const Design &design)
{
    const QString schematicFile = m_design.project.schematicFile;
    const QString boardFile = m_design.project.boardFile;
    m_design = design;
    m_design.project.schematicFile = schematicFile;
    m_design.project.boardFile = boardFile;
    emit designChanged();
    noteModified();
}

void Document::reset(const Design &design, const QString &projectPath)
{
    m_undoStack.clear();
    m_design = design;
    m_saved = design;
    m_projectPath = projectPath;
    m_autosaved = Design();
    m_savedDate = QDate();
    if (!projectPath.isEmpty()) {
        for (const QString &path : {projectPath, DesignFiles::schematicPath(projectPath, design.project),
                                    DesignFiles::boardPath(projectPath, design.project)}) {
            const QDate date = QFileInfo(path).lastModified().date();
            if (date.isValid() && (!m_savedDate.isValid() || date > m_savedDate))
                m_savedDate = date;
        }
    }
    emit designChanged();
    emit savedDateChanged();
    noteModified();
}

bool Document::write(const QString &projectPath, bool everything, QString &error)
{
    const QString schematicPath = DesignFiles::schematicPath(projectPath, m_design.project);
    const QString boardPath = DesignFiles::boardPath(projectPath, m_design.project);
    const QString backupFolder = DesignFiles::backupPath(projectPath);

    // The library goes first and the project last, so that no file names
    // one that is not there. Each file counts as saved as soon as it is
    // written, so after a failure the next save writes only the rest.
    // Whether one of the three files got written.
    bool wrote = false;
    bool ok = LibraryFolder::write(DesignFiles::libraryPath(projectPath), m_design.library,
                                         m_saved.library, everything, error);
    // Items dropped from the library stay on disk, unused: files are never
    // removed. They no longer count as part of the saved content.
    if (ok)
        m_saved.library = m_design.library;
    if (ok && (everything || m_design.schematic != m_saved.schematic
               || !QFileInfo::exists(schematicPath))) {
        ok = backUp(schematicPath, backupFolder, m_backupCount, error)
             && SchematicFile::write(schematicPath, m_design.schematic, error);
        if (ok)
            m_saved.schematic = m_design.schematic;
        wrote = wrote || ok;
    }
    if (ok && (everything || m_design.board != m_saved.board || !QFileInfo::exists(boardPath))) {
        ok = backUp(boardPath, backupFolder, m_backupCount, error)
             && BoardFile::write(boardPath, m_design.board, error);
        if (ok)
            m_saved.board = m_design.board;
        wrote = wrote || ok;
    }
    if (ok && (everything || m_design.project != m_saved.project
               || !QFileInfo::exists(projectPath))) {
        ok = backUp(projectPath, backupFolder, m_backupCount, error)
             && ProjectFile::write(projectPath, m_design.project, error);
        if (ok)
            m_saved.project = m_design.project;
        wrote = wrote || ok;
    }
    if (wrote && m_savedDate != QDate::currentDate()) {
        m_savedDate = QDate::currentDate();
        emit savedDateChanged();
    }
    noteModified();
    return ok;
}

void Document::noteModified()
{
    const bool modified = isModified();
    if (modified == m_wasModified)
        return;
    m_wasModified = modified;
    emit modifiedChanged(modified);
}

} // namespace ppcb
