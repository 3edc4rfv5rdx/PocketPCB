#include "document.h"

#include <QDir>
#include <QFileInfo>

namespace ppcb {

namespace {

const char NEW_PROJECT_NAME[] = "untitled";

// Characters no file name may hold on Windows; '/' also on Linux.
const char FORBIDDEN_NAME_CHARS[] = "/\\:*?\"<>|";

// Names Windows keeps for devices.
const char *const RESERVED_NAMES[] = {
    "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
    "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9",
};

} // namespace

// One step of the history: the whole content before and after it.
class DesignCommand : public QUndoCommand
{
public:
    DesignCommand(Document *document, const QString &text, const Design &before,
                  const Design &after)
        : QUndoCommand(text)
        , m_document(document)
        , m_before(before)
        , m_after(after)
    {
    }

    void undo() override { m_document->install(m_before); }
    // Also called by QUndoStack::push(), which is what applies a change.
    void redo() override { m_document->install(m_after); }

private:
    Document *m_document;
    Design m_before;
    Design m_after;
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

void Document::change(const QString &text, const Design &changed)
{
    // The file names are not the caller's to change.
    Design after = changed;
    after.project.schematicFile = m_design.project.schematicFile;
    after.project.boardFile = m_design.project.boardFile;
    if (after == m_design)
        return;
    m_undoStack.push(new DesignCommand(this, text, m_design, after));
}

void Document::startNew()
{
    startNew(QLatin1String(NEW_PROJECT_NAME));
}

void Document::startNew(const QString &baseName)
{
    reset(newDesign(baseName), QString());
}

bool Document::open(const QString &projectPath, QString &error)
{
    Design design;
    if (!DesignFiles::read(projectPath, design, error))
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
    return write(m_projectPath, false, error);
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
        noteModified();
        return false;
    }
    m_projectPath = projectPath;
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
    const QString folder = QDir(parentFolder).filePath(name);
    if (!QDir().mkpath(folder)) {
        error = withPlace(folder, tr("cannot make the folder"));
        return false;
    }
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
    emit designChanged();
    noteModified();
}

bool Document::write(const QString &projectPath, bool everything, QString &error)
{
    const QString schematicPath = DesignFiles::schematicPath(projectPath, m_design.project);
    const QString boardPath = DesignFiles::boardPath(projectPath, m_design.project);

    // The project goes last, so that it never names a file that is not
    // there. Each file counts as saved as soon as it is written, so after a
    // failure the next save writes only the rest.
    bool ok = true;
    if (everything || m_design.schematic != m_saved.schematic
        || !QFileInfo::exists(schematicPath)) {
        ok = SchematicFile::write(schematicPath, m_design.schematic, error);
        if (ok)
            m_saved.schematic = m_design.schematic;
    }
    if (ok && (everything || m_design.board != m_saved.board || !QFileInfo::exists(boardPath))) {
        ok = BoardFile::write(boardPath, m_design.board, error);
        if (ok)
            m_saved.board = m_design.board;
    }
    if (ok && (everything || m_design.project != m_saved.project
               || !QFileInfo::exists(projectPath))) {
        ok = ProjectFile::write(projectPath, m_design.project, error);
        if (ok)
            m_saved.project = m_design.project;
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
