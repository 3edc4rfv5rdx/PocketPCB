#pragma once

#include "project.h"

#include <QDate>
#include <QObject>
#include <QUndoStack>

namespace ppcb {

// The project that is open: its content, its place on disk and the history
// of its changes. One Undo/Redo history serves the project, the schematic
// and the board together.
//
// A change is made by handing over the whole changed content; a step of the
// history keeps the content before and after it. Unchanged lists are shared
// between the copies, so a step costs about as much as what it changed.
//
// The names of the schematic and the board files belong to the document, not
// to the history: change() and Undo/Redo never alter them, only startNew(),
// open() and saveAs() do.
class Document : public QObject
{
    Q_OBJECT

public:
    // The history keeps this many last steps.
    static constexpr int UNDO_LIMIT = 200;
    // How many earlier versions of each file a save keeps, unless told.
    static constexpr int DEFAULT_BACKUP_COUNT = 3;

    // Starts as a new project named "untitled".
    explicit Document(QObject *parent = nullptr);

    const Design &design() const { return m_design; }
    // Empty until the project is opened from a file or saved into one.
    QString projectPath() const { return m_projectPath; }
    // The content differs from what the files hold. A new project is not
    // modified until it is changed.
    bool isModified() const;
    // The day a save last wrote a file of the project: when it was opened,
    // the newest of its project, schematic and board files; null for a
    // project never saved.
    QDate savedDate() const { return m_savedDate; }
    // The title block as printed: an empty date is savedDate(), as
    // "2026-11-19".
    TitleBlock titleBlockShown() const;

    // Before a save replaces the project, schematic or board file, the file
    // as it was is kept in the backup folder (DesignFiles::backupPath()) as
    // "<file name>.1.bak", the earlier ones moving to .2.bak and on; this
    // many are kept. 0 keeps none.
    int backupCount() const { return m_backupCount; }
    void setBackupCount(int count) { m_backupCount = qMax(0, count); }

    // For the Undo and Redo actions. Push nothing here: use change().
    QUndoStack *undoStack() { return &m_undoStack; }

    // Replaces the content as one step of the history, named 'text' for the
    // menu ("Undo <text>"), its wires that just go on from each other made
    // one first (withWiresMerged()). Does nothing when 'changed' brings no
    // change.
    // 'page' (the sheets, then the board, as on the tabs; -1 for none) and
    // 'ids' tell where the step was made and what it touched: undoing and
    // redoing it reports them through stepApplied().
    void change(const QString &text, const Design &changed, int page = -1,
                const QSet<ObjectId> &ids = QSet<ObjectId>());

    // Drops the content and the history for a new project; without a name
    // it is "untitled".
    void startNew();
    void startNew(const QString &baseName);
    // Reads a project. On failure returns false, puts a message for the user
    // into 'error' and leaves the document as it was. What is wrong with the
    // project library does not stop it: see DesignFiles::read().
    bool open(const QString &projectPath, QString &error, QStringList *problems = nullptr);
    // Writes the files that changed since they were read or written, and
    // those that are missing. On failure returns false with a message; what
    // was written before the failure counts as saved.
    bool save(QString &error);
    // Writes all three files and the library at a new place and names the
    // schematic and the board after the project file. The history is kept.
    // After a failure the document still belongs to the old place.
    bool saveAs(const QString &projectPath, QString &error);

    // A project lives in a folder of its own, named like the project:
    // <parentFolder>/<name>/<name>.ppcb.
    static QString projectPathInFolder(const QString &parentFolder, const QString &name);
    // Why 'name' cannot name a project folder and its files, on Linux and on
    // Windows; empty when it can.
    static QString nameProblem(const QString &name);
    // Why the project folder 'name' cannot be made inside 'parentFolder':
    // a bad name, no parent folder or one that is not a full path, or
    // something in the place other than an empty folder. Empty when it can
    // be made.
    static QString newFolderProblem(const QString &parentFolder, const QString &name);
    // saveAs() into the new folder 'name' inside 'parentFolder', made with
    // the folders above it when missing. Refuses whatever newFolderProblem()
    // names. After a failure of the writing the new folder is left with what
    // was written into it.
    bool saveAsNewFolder(const QString &parentFolder, const QString &name, QString &error);

    // Autosave: a whole copy of the project as it is now, written to
    // DesignFiles::autosaveProjectPath() while there are unsaved changes; the
    // project's own files are not touched. Nothing is written for a project
    // that has no file yet, without changes, or when the copy is up to date.
    bool autosave(QString &error);
    // Removes the autosaved copy of the open project, if there is one: only
    // the files an autosave writes, the autosave folder itself when that
    // leaves it empty. A successful save() or saveAs() does it too.
    void dropAutosave();
    // Whether 'projectPath' has an autosaved copy no older than its file:
    // changes of a session that did not end with a save or a "don't save".
    static bool hasAutosave(const QString &projectPath);
    // Takes the autosaved copy of the open project as its content. It counts
    // as unsaved changes, with no history. On failure returns false with a
    // message and leaves the content as it was.
    bool restoreAutosave(QString &error, QStringList *problems = nullptr);

signals:
    // The content was replaced: by change(), Undo, Redo, startNew() or open().
    void designChanged();
    // A project was opened or started: sent just before its designChanged(),
    // so that what is slow to find again (the rules checks) waits.
    void designReset();
    void modifiedChanged(bool modified);
    // A step was undone or redone: where it was made and what it touched,
    // so the window can show it.
    void stepApplied(int page, const QSet<ObjectId> &ids);
    // A new step was made by change(), not undone or redone: its name, its
    // page and the content before it; the content after it is design().
    void stepMade(const QString &text, int page, const Design &before);
    // savedDate() changed.
    void savedDateChanged();

private:
    friend class DesignCommand;

    // Makes 'design' the content, keeping the file names (see above).
    void install(const Design &design);
    // Makes the content, taken as it is, the one of a project just read or
    // just started: no history, not modified.
    void reset(const Design &design, const QString &projectPath);
    bool write(const QString &projectPath, bool everything, QString &error);
    // Emits modifiedChanged() when the answer of isModified() has changed.
    void noteModified();

    Design m_design;
    // The content of the files, or of the new project as it was started.
    Design m_saved;
    QString m_projectPath;
    QUndoStack m_undoStack;
    bool m_wasModified = false;
    int m_backupCount = DEFAULT_BACKUP_COUNT;
    // The content of the autosaved copy; empty when there is none.
    Design m_autosaved;
    QDate m_savedDate;
};

} // namespace ppcb
