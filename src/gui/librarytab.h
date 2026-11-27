#pragma once

#include "alerts.h"
#include "library.h"
#include "libraryfile.h"
#include "toolicons.h"

#include <QUndoStack>
#include <QWidget>

#include <functional>
#include <optional>

class QAction;
class QKeySequence;
class QMenu;
class QToolBar;

namespace ppcb {

// A tab of the library editor window editing one item of the library — a
// footprint, a symbol or a part — with its own Undo. It saves into the
// user's library, where an item of the built-in one's name replaces it
// (see mergeLibraries()); it may save under a new name and, once in the
// user's library, be renamed. Its keys work while the keyboard is in it.
//
// This class holds what does not depend on how the item is edited;
// ItemEditor the editors on a canvas, PartEditor the form of a part.
class LibraryTab : public QWidget
{
    Q_OBJECT

public:
    ~LibraryTab() override;

    // "Footprint editor", "Symbol editor", "Part editor".
    virtual QString editorTitle() const = 0;
    // "New footprint", "New symbol", "New part".
    virtual QString newItemTitle() const = 0;
    // Gives the keyboard to the tab, once it is shown.
    virtual void takeFocus() = 0;

    QString name() const { return m_name; }
    QUndoStack *undoStack() { return &m_undoStack; }
    // True when there is nothing unsaved.
    bool isSaved() const { return m_undoStack.isClean(); }
    // The name on its tab: the item's, or newItemTitle(), with a star
    // while something is not saved.
    QString tabTitle() const;
    // What its tab says when the mouse rests on it: the kind, the name, and
    // whether it is still the built-in item.
    QString tabTip() const;
    // The last message it put into the status bar.
    QString status() const { return m_status; }
    // Asks what to do with unsaved changes before it closes: save them, drop
    // them or stay. True when it may close; always true without changes.
    bool confirmClose();

    // Writes the item into the user's library, asking for its name first
    // when it has none. False, with the reason told, when it is not written.
    bool save();
    // The same under a name asked for: from then on the item of that name
    // is edited; the one it was opened as stays as it was.
    bool saveUnderNewName();
    // The same under 'name', asking nothing; for a name already taken
    // only with 'replace'.
    bool saveAs(const QString &name, bool replace);
    // An item of the user's library under a new name asked for: saved so,
    // its old file gone and the parts of the user's library listing it
    // given the new name. Built-in parts cannot follow: when some list it,
    // the user is asked first. False when it is not renamed.
    bool renameItem();
    // The same to 'name', asking nothing.
    bool renameTo(const QString &name);
    // Only one of the user's library can be renamed.
    bool canRename() const { return m_inUserLibrary && !m_name.isEmpty(); }

    // Puts 'text' into the status bar of the window.
    void showStatus(const QString &text);
    // An error or a warning in a box as well (Alerts::show()).
    void alert(const Alert &alert);

signals:
    // The item was written into the user's library as 'name'.
    void saved(const QString &name);
    // What its tab shows changed: its name or whether all is saved.
    void titleChanged();
    // A message for the status bar of the window.
    void statusChanged(const QString &text);

protected:
    // 'name' is the item's name in the library; empty for a new one, which
    // is named when it is first saved. 'inUserLibrary' tells whether the
    // user's library has it already, or only the built-in one or the
    // project does.
    LibraryTab(const QString &name, bool inUserLibrary, QWidget *parent);

    // An action of the tab with its key in its tip; on the strip when it
    // has a picture.
    QAction *addTool(QToolBar *bar, const QString &text, const char *name, const QKeySequence &key,
                     std::optional<ToolIcon> icon, const std::function<void()> &slot);
    // A button of the strip with the action 'main' and 'menu' under its
    // arrow.
    void addMenuButton(QToolBar *bar, QAction *main, QMenu *menu, const char *name);
    // Save, its arrow offering Save as and Rename, then Undo and Redo, on
    // 'bar'.
    void addFileTools(QToolBar *bar);

    // What the file of the item is, in the user's library folder.
    virtual const char *libraryFolder() const = 0;
    virtual LibraryItem libraryItem() const = 0;
    // Why the item cannot be written; empty when it can.
    virtual QString saveProblem() const = 0;
    // The parts of 'library' that list the item 'name', and one of them
    // listing 'to' in its place; none for a part.
    virtual QStringList partsUsing(const Library &library, const QString &name) const = 0;
    virtual Part partRenamed(const Part &part, const QString &from, const QString &to) const = 0;
    // The words of the kind: the label the name is asked with, the
    // question before a name taken is replaced, before built-in parts are
    // left with the old name, before unsaved changes are lost, and what
    // is said once it is renamed.
    virtual QString nameLabel() const = 0;
    virtual QString takenQuestion(const QString &name) const = 0;
    virtual QString strandedQuestion(const QString &name, const QStringList &parts) const = 0;
    virtual QString unsavedQuestion() const = 0;
    virtual QString renamedMessage(const QString &from, const QString &to) const = 0;
    // True when the user's library has an item 'name' in 'folder'.
    static bool userLibraryHas(const char *folder, const QString &name);

    QString m_name;
    bool m_inUserLibrary = false;
    QUndoStack m_undoStack;

private:
    // A name typed for the item, asked with 'title'; empty when none is or
    // the user keeps it from replacing another.
    QString askName(const QString &title);

    QString m_status;
    QAction *m_renameAction = nullptr;
};

} // namespace ppcb
