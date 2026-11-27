#include "librarytab.h"

#include "appsettings.h"
#include "format.h"
#include "mainwindow.h"
#include "sheeteditor.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QToolBar>
#include <QToolButton>

namespace ppcb {

namespace {

// What an item's name may hold: it is the name of its file.
const QRegularExpression &nameRule()
{
    static const QRegularExpression rule(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._+-]*$"));
    return rule;
}

// The file of the item 'name' of the kind kept in 'subfolder'.
QString itemPath(const QString &libraryFolder, const char *subfolder, const QString &name)
{
    return QDir(QDir(libraryFolder).filePath(QLatin1String(subfolder)))
        .filePath(name + QLatin1String(LIBRARY_FILE_EXT));
}

} // namespace

LibraryTab::LibraryTab(const QString &name, bool inUserLibrary, QWidget *parent)
    : QWidget(parent)
    , m_name(name)
    , m_inUserLibrary(inUserLibrary)
{
    connect(&m_undoStack, &QUndoStack::cleanChanged, this, &LibraryTab::titleChanged);
}

LibraryTab::~LibraryTab()
{
    // The stack going last says it is clean no more, or again: nobody is to
    // hear it, the tab is gone.
    m_undoStack.disconnect();
}

bool LibraryTab::userLibraryHas(const char *folder, const QString &name)
{
    return QFileInfo::exists(itemPath(AppSettings::userLibraryFolder(), folder, name));
}

QAction *LibraryTab::addTool(QToolBar *bar, const QString &text, const char *name,
                             const QKeySequence &key, std::optional<ToolIcon> icon,
                             const std::function<void()> &slot)
{
    QAction *result = new QAction(text, this);
    result->setObjectName(QLatin1String(name));
    QString tip = text;
    tip.remove(QLatin1Char('&'));
    if (!key.isEmpty()) {
        result->setShortcut(key);
        tip += QStringLiteral(" (") + key.toString(QKeySequence::NativeText) + QLatin1Char(')');
    }
    result->setToolTip(tip);
    // The keys of the tab shown only: the others are hidden.
    result->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(result, &QAction::triggered, this, slot);
    addAction(result);
    if (icon) {
        result->setIcon(toolIcon(*icon, bar->palette().color(QPalette::WindowText)));
        bar->addAction(result);
    }
    return result;
}

void LibraryTab::addMenuButton(QToolBar *bar, QAction *main, QMenu *menu, const char *name)
{
    QToolButton *button = new QToolButton(bar);
    button->setObjectName(QLatin1String(name));
    button->setDefaultAction(main);
    button->setMenu(menu);
    button->setPopupMode(QToolButton::MenuButtonPopup);
    bar->addWidget(button);
}

void LibraryTab::addFileTools(QToolBar *bar)
{
    const QColor color = bar->palette().color(QPalette::WindowText);
    // Save, its arrow offering to save under a new name.
    QAction *saveAction = addTool(bar, MainWindow::saveTitle(), "actionSaveItem",
                                  QKeySequence::Save, std::nullopt, [this] { save(); });
    saveAction->setIcon(toolIcon(ToolIcon::Save, color));
    QAction *saveAs = addTool(bar, MainWindow::saveAsTitle() + QStringLiteral("…"),
                              "actionSaveItemAs", QKeySequence::SaveAs, std::nullopt,
                              [this] { saveUnderNewName(); });
    m_renameAction =
        addTool(bar, SheetEditor::actionName(SheetEditor::Action::Rename) + QStringLiteral("…"),
                "actionRenameItem", QKeySequence(), std::nullopt, [this] { renameItem(); });
    m_renameAction->setEnabled(canRename());
    connect(this, &LibraryTab::titleChanged, m_renameAction,
            [this] { m_renameAction->setEnabled(canRename()); });
    QMenu *saveMenu = new QMenu(this);
    saveMenu->addAction(saveAs);
    saveMenu->addAction(m_renameAction);
    addMenuButton(bar, saveAction, saveMenu, "saveItemButton");
    QWidget *spacer = new QWidget(bar);
    spacer->setFixedWidth(TOOL_ICON_SIZE / 2);
    bar->addWidget(spacer);
    QAction *undo = m_undoStack.createUndoAction(this, MainWindow::undoTitle());
    undo->setObjectName(QStringLiteral("actionUndoItem"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    undo->setIcon(toolIcon(ToolIcon::Undo, color));
    addAction(undo);
    bar->addAction(undo);
    QAction *redo = m_undoStack.createRedoAction(this, MainWindow::redoTitle());
    redo->setObjectName(QStringLiteral("actionRedoItem"));
    redo->setShortcut(QKeySequence::Redo);
    redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    redo->setIcon(toolIcon(ToolIcon::Redo, color));
    addAction(redo);
    bar->addAction(redo);
}

QString LibraryTab::tabTitle() const
{
    const QString title = m_name.isEmpty() ? newItemTitle() : m_name;
    return m_undoStack.isClean() ? title : title + QLatin1Char('*');
}

QString LibraryTab::tabTip() const
{
    QString tip = editorTitle() + QStringLiteral(": ") + (m_name.isEmpty() ? newItemTitle() : m_name);
    if (!m_name.isEmpty() && !m_inUserLibrary)
        tip += QStringLiteral(" (") + tr("from the built-in library") + QLatin1Char(')');
    return tip;
}

void LibraryTab::showStatus(const QString &text)
{
    m_status = text;
    emit statusChanged(text);
}

void LibraryTab::alert(const Alert &alert)
{
    Alerts::show(this, alert, [this](const QString &text) { showStatus(text); });
}

bool LibraryTab::save()
{
    if (!m_name.isEmpty())
        return saveAs(m_name, true);
    const QString name = askName(MainWindow::saveTitle().remove(QLatin1Char('&')));
    return !name.isEmpty() && saveAs(name, true);
}

bool LibraryTab::saveUnderNewName()
{
    const QString name = askName(MainWindow::saveAsTitle().remove(QLatin1Char('&')));
    return !name.isEmpty() && saveAs(name, true);
}

QString LibraryTab::askName(const QString &title)
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, title, nameLabel() + QLatin1Char(':'),
                                               QLineEdit::Normal, m_name, &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return QString();
    if (!nameRule().match(name).hasMatch()) {
        alert(Alert::warning(tr("A name may hold Latin letters, digits and these signs")
                                 + QStringLiteral(": . _ + -"),
                             QStringLiteral("footprintName")));
        return QString();
    }
    // Its own name it keeps without asking.
    const char *folder = libraryFolder();
    const bool taken =
        name != m_name
        && (QFileInfo::exists(itemPath(AppSettings::userLibraryFolder(), folder, name))
            || QFileInfo::exists(itemPath(AppSettings::builtInLibraryFolder(), folder, name)));
    if (taken
        && QMessageBox::question(this, title, takenQuestion(name) + QLatin1Char('?'))
               != QMessageBox::Yes)
        return QString();
    return name;
}

bool LibraryTab::saveAs(const QString &name, bool replace)
{
    takeTyping();
    const QString path = itemPath(AppSettings::userLibraryFolder(), libraryFolder(), name);
    if (!replace && QFileInfo::exists(path))
        return false;
    const QString problem = saveProblem();
    if (!problem.isEmpty()) {
        alert(Alert::error(capitalized(problem)));
        return false;
    }
    QString error;
    if (!AppSettings::makeUserLibraryFolders(error)) {
        alert(Alert::error(error));
        return false;
    }
    if (!LibraryFile::write(path, libraryItem(), error)) {
        alert(Alert::error(error));
        return false;
    }
    m_name = name;
    m_inUserLibrary = true;
    m_undoStack.setClean();
    emit titleChanged();
    showStatus(writtenMessage(path));
    emit saved(name);
    return true;
}

bool LibraryTab::renameItem()
{
    if (!canRename())
        return false;
    const QString title = SheetEditor::actionName(SheetEditor::Action::Rename);
    const QString name = askName(title);
    if (name.isEmpty() || name == m_name)
        return false;
    // Built-in parts listing it, not replaced by parts of the user's own,
    // keep the old name.
    QStringList problems;
    const Library builtIn = LibraryFolder::read(AppSettings::builtInLibraryFolder(), problems);
    const Library user = LibraryFolder::read(AppSettings::userLibraryFolder(), problems);
    QStringList stranded;
    for (const QString &part : partsUsing(builtIn, m_name)) {
        if (!user.parts.contains(part))
            stranded.append(part);
    }
    if (!stranded.isEmpty()
        && QMessageBox::question(this, title,
                                 strandedQuestion(m_name, stranded) + QLatin1Char('?'))
               != QMessageBox::Yes)
        return false;
    return renameTo(name);
}

bool LibraryTab::renameTo(const QString &name)
{
    if (!canRename() || name.isEmpty() || name == m_name)
        return false;
    const QString old = m_name;
    const QString userFolder = AppSettings::userLibraryFolder();
    QStringList problems;
    const Library user = LibraryFolder::read(userFolder, problems);
    if (!saveAs(name, true))
        return false;
    // The user's parts follow it.
    for (const QString &partName : partsUsing(user, old)) {
        LibraryItem item;
        item.kind = LibraryKind::Part;
        item.part = partRenamed(user.parts.value(partName), old, name);
        QString error;
        if (!LibraryFile::write(itemPath(userFolder, LIBRARY_PARTS_FOLDER, partName), item, error)) {
            alert(Alert::error(error));
            emit saved(name);
            return false;
        }
    }
    QFile::remove(itemPath(userFolder, libraryFolder(), old));
    showStatus(renamedMessage(old, name));
    emit saved(name);
    return true;
}

bool LibraryTab::confirmClose()
{
    takeTyping();
    if (m_undoStack.isClean())
        return true;
    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, editorTitle(), unsavedQuestion() + QLatin1Char('?'),
                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && save());
}

} // namespace ppcb
