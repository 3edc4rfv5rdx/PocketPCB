#include "libraryeditor.h"

#include "alerts.h"
#include "appsettings.h"
#include "canvasview.h"
#include "footprintedit.h"
#include "footprinteditor.h"
#include "format.h"
#include "graphicedit.h"
#include "itemchooser.h"
#include "jsonio.h"
#include "parteditor.h"
#include "sheeteditor.h"
#include "symboledit.h"
#include "symboleditor.h"
#include "toolicons.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace ppcb {

namespace {

// The "+" over the tabs: bold and this many times the usual size.
const double ADD_BUTTON_SCALE = 1.4;
// The buttons of an empty window, at least this wide, in pixels.
const int START_BUTTON_WIDTH = 220;
// The size of the window at first, in pixels.
const int EDITOR_WIDTH = 900;
const int EDITOR_HEIGHT = 650;

// The tab of 'tabs' where an editor of the kind 'Editor' has the item
// 'name' open; null when none has.
template <class Editor>
Editor *openTab(const QTabWidget *tabs, const QString &name)
{
    for (int i = 0; i < tabs->count(); ++i) {
        Editor *open = qobject_cast<Editor *>(tabs->widget(i));
        if (open != nullptr && open->name() == name)
            return open;
    }
    return nullptr;
}

} // namespace

LibraryEditor::LibraryEditor(QWidget *parent)
    : QMainWindow(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::Window);
    setWindowTitle(libraryEditorTitle());
    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("libraryEditorTabs"));
    m_tabs->setDocumentMode(true);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);

    // An empty window: the buttons the tabs would have, in its middle.
    m_start = new QWidget(this);
    m_start->setObjectName(QStringLiteral("libraryEditorStart"));
    m_start->setStyleSheet(panelButtonStyle(QString()));
    QPushButton *startFootprint = new QPushButton(FootprintEditor::newFootprintTitle(), m_start);
    startFootprint->setObjectName(QStringLiteral("startNewFootprint"));
    connect(startFootprint, &QPushButton::clicked, this, &LibraryEditor::openNewFootprint);
    QPushButton *startSymbol = new QPushButton(SymbolEditor::newSymbolTitle(), m_start);
    startSymbol->setObjectName(QStringLiteral("startNewSymbol"));
    connect(startSymbol, &QPushButton::clicked, this, &LibraryEditor::openNewSymbol);
    QPushButton *startPart = new QPushButton(PartEditor::newPartTitle(), m_start);
    startPart->setObjectName(QStringLiteral("startNewPart"));
    connect(startPart, &QPushButton::clicked, this, &LibraryEditor::openNewPart);
    QPushButton *startOpen = new QPushButton(tr("Open") + QStringLiteral("…"), m_start);
    startOpen->setObjectName(QStringLiteral("startOpenItem"));
    connect(startOpen, &QPushButton::clicked, this, &LibraryEditor::askOpen);
    QPushButton *startExport = new QPushButton(exportLibraryTitle() + QStringLiteral("…"), m_start);
    startExport->setObjectName(QStringLiteral("startExportLibrary"));
    connect(startExport, &QPushButton::clicked, this, [this] {
        Alerts::show(this, exportUserLibrary(this),
                     [this](const QString &text) { m_status->setText(text); });
    });
    QVBoxLayout *startLayout = new QVBoxLayout(m_start);
    startLayout->addStretch();
    // Symbols first, then footprints, then parts.
    for (QPushButton *button : {startSymbol, startFootprint, startPart, startOpen, startExport}) {
        button->setMinimumWidth(START_BUTTON_WIDTH);
        startLayout->addWidget(button, 0, Qt::AlignHCenter);
    }
    startLayout->addStretch();

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(m_start);
    m_pages->addWidget(m_tabs);
    setCentralWidget(m_pages);

    QWidget *corner = new QWidget(m_tabs);
    corner->setStyleSheet(panelButtonStyle(QString()));
    QHBoxLayout *cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(2, 2, 2, 2);
    // + offers a new symbol, footprint or part, in that order.
    QToolButton *add = new QToolButton(corner);
    add->setObjectName(QStringLiteral("newItemTab"));
    add->setText(QStringLiteral("+"));
    add->setAutoRaise(true);
    add->setPopupMode(QToolButton::InstantPopup);
    QFont bold = add->font();
    bold.setBold(true);
    if (bold.pointSizeF() > 0)
        bold.setPointSizeF(bold.pointSizeF() * ADD_BUTTON_SCALE);
    add->setFont(bold);
    QMenu *addMenu = new QMenu(add);
    connect(addMenu->addAction(SymbolEditor::newSymbolTitle()), &QAction::triggered, this,
            &LibraryEditor::openNewSymbol);
    addNewSymbolFromActions(addMenu->addMenu(SymbolEditor::newSymbolFromTitle()),
                            [this] { return this; });
    connect(addMenu->addAction(FootprintEditor::newFootprintTitle()), &QAction::triggered, this,
            &LibraryEditor::openNewFootprint);
    connect(addMenu->addAction(PartEditor::newPartTitle()), &QAction::triggered, this,
            &LibraryEditor::openNewPart);
    add->setMenu(addMenu);
    QPushButton *open = new QPushButton(tr("Open") + QStringLiteral("…"), corner);
    open->setObjectName(QStringLiteral("openItemTab"));
    connect(open, &QPushButton::clicked, this, &LibraryEditor::askOpen);
    QPushButton *copy = new QPushButton(tr("Copy"), corner);
    copy->setObjectName(QStringLiteral("copyItemTab"));
    connect(copy, &QPushButton::clicked, this, &LibraryEditor::openCopy);
    cornerLayout->addWidget(add);
    cornerLayout->addWidget(open);
    cornerLayout->addWidget(copy);
    m_tabs->setCornerWidget(corner, Qt::TopRightCorner);
    m_status = new QLabel(this);
    statusBar()->addWidget(m_status, 1);
    m_position = new QLabel(formatPosition(0, 0), this);
    statusBar()->addPermanentWidget(m_position);
    connect(m_tabs, &QTabWidget::currentChanged, this, &LibraryEditor::tabShown);
    showTabsOrStart();
    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &LibraryEditor::closeTab);
    resize(EDITOR_WIDTH, EDITOR_HEIGHT);
}

QString LibraryEditor::libraryEditorTitle()
{
    return tr("Library editor");
}

QString LibraryEditor::exportLibraryTitle()
{
    return tr("Export library");
}

Alert LibraryEditor::exportUserLibrary(QWidget *parent)
{
    QList<OutputFile> files;
    QString error;
    if (!LibraryFolder::archiveFiles(AppSettings::userLibraryFolder(),
                                     AppSettings::userLibraryArchiveFolder(), files, error))
        return Alert::error(error);
    if (files.isEmpty())
        return Alert::info(tr("Your library is empty"));
    const QString offered = QDir(AppSettings::projectsFolder())
                                .filePath(AppSettings::libraryArchiveName(QDate::currentDate()));
    const QString path = QFileDialog::getSaveFileName(
        parent, exportLibraryTitle(), offered, tr("Zip archives") + QStringLiteral(" (*.zip)"));
    if (path.isEmpty())
        return Alert::info(QString());
    if (!JsonFile::writeData(path, zipArchive(files, QDateTime::currentDateTime()), error))
        return Alert::error(error);
    return Alert::info(writtenMessage(path));
}

void LibraryEditor::addEditorTab(LibraryTab *editor)
{
    connect(editor, &LibraryTab::titleChanged, this, [this, editor] { updateTab(editor); });
    connect(editor, &LibraryTab::saved, this, &LibraryEditor::saved);
    connect(editor, &LibraryTab::statusChanged, this, [this, editor](const QString &text) {
        if (m_tabs->currentWidget() == editor)
            m_status->setText(text);
    });
    if (const ItemEditor *drawing = qobject_cast<ItemEditor *>(editor)) {
        connect(drawing->canvas(), &CanvasView::cursorMoved, this,
                [this, editor](Coord x, Coord y) {
                    if (m_tabs->currentWidget() == editor)
                        m_position->setText(formatPosition(x, y));
                });
    }
    m_tabs->setCurrentIndex(m_tabs->addTab(editor, editor->tabTitle()));
    updateTab(editor);
    showTabsOrStart();
}

FootprintEditor *LibraryEditor::openFootprint(const QString &name, const Footprint &footprint,
                                              bool inUserLibrary)
{
    if (!name.isEmpty()) {
        if (FootprintEditor *open = openTab<FootprintEditor>(m_tabs, name)) {
            m_tabs->setCurrentWidget(open);
            return open;
        }
    }
    FootprintEditor *editor = new FootprintEditor(name, footprint, inUserLibrary, m_tabs);
    editor->setClearance(m_clearance);
    addEditorTab(editor);
    return editor;
}

SymbolEditor *LibraryEditor::openSymbol(const QString &name, const Symbol &symbol,
                                        bool inUserLibrary)
{
    if (!name.isEmpty()) {
        if (SymbolEditor *open = openTab<SymbolEditor>(m_tabs, name)) {
            m_tabs->setCurrentWidget(open);
            return open;
        }
    }
    SymbolEditor *editor = new SymbolEditor(name, symbol, inUserLibrary, m_tabs);
    addEditorTab(editor);
    return editor;
}

PartEditor *LibraryEditor::openPart(const QString &name, const Part &part, bool inUserLibrary)
{
    if (!name.isEmpty()) {
        if (PartEditor *open = openTab<PartEditor>(m_tabs, name)) {
            m_tabs->setCurrentWidget(open);
            return open;
        }
    }
    PartEditor *editor = new PartEditor(name, part, inUserLibrary, m_library, m_tabs);
    addEditorTab(editor);
    return editor;
}

void LibraryEditor::setLibrary(const Library *library)
{
    m_library = library;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (PartEditor *editor = qobject_cast<PartEditor *>(m_tabs->widget(i)))
            editor->setLibrary(library);
    }
}

void LibraryEditor::setClearance(Coord clearance)
{
    m_clearance = clearance;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (FootprintEditor *editor = qobject_cast<FootprintEditor *>(m_tabs->widget(i)))
            editor->setClearance(clearance);
    }
}

FootprintEditor *LibraryEditor::openNewFootprint()
{
    Footprint footprint;
    footprint.refAt = Point{0, -NEW_ITEM_TEXT_OFFSET};
    footprint.valueAt = Point{0, NEW_ITEM_TEXT_OFFSET};
    return openFootprint(QString(), footprint, false);
}

SymbolEditor *LibraryEditor::openNewSymbol()
{
    Symbol symbol;
    symbol.refAt = Point{0, -NEW_ITEM_TEXT_OFFSET};
    symbol.valueAt = Point{0, NEW_ITEM_TEXT_OFFSET};
    return openSymbol(QString(), symbol, false);
}

PartEditor *LibraryEditor::openNewPart()
{
    return openPart(QString(), Part(), false);
}

SymbolEditor *LibraryEditor::openSymbolFrom(const Symbol &symbol)
{
    SymbolEditor *editor = openSymbol(QString(), symbol, false);
    // Not saved anywhere yet: closing it asks.
    editor->undoStack()->resetClean();
    return editor;
}

void LibraryEditor::addNewSymbolFromActions(QMenu *menu,
                                            const std::function<LibraryEditor *()> &window)
{
    SymbolEditor::addPieceActions(
        menu, [window](SymbolPiece piece) { window()->openSymbolFrom(pieceSymbol(piece)); },
        [window] { window()->openNewSymbol()->askChip(); });
}

LibraryTab *LibraryEditor::openCopy()
{
    LibraryTab *copy = nullptr;
    if (const auto *footprint = qobject_cast<FootprintEditor *>(m_tabs->currentWidget()))
        copy = openFootprint(QString(), footprint->footprint(), false);
    else if (const auto *symbol = qobject_cast<SymbolEditor *>(m_tabs->currentWidget()))
        copy = openSymbol(QString(), symbol->symbol(), false);
    else if (const auto *part = qobject_cast<PartEditor *>(m_tabs->currentWidget()))
        copy = openPart(QString(), part->part(), false);
    // Not saved anywhere yet: closing it asks.
    if (copy != nullptr)
        copy->undoStack()->resetClean();
    return copy;
}

LibraryTab *LibraryEditor::askOpen()
{
    if (m_library == nullptr)
        return nullptr;
    // What the tab shown edits, at first.
    if (qobject_cast<SymbolEditor *>(m_tabs->currentWidget()) != nullptr)
        m_openKind = LibraryKind::Symbol;
    else if (qobject_cast<FootprintEditor *>(m_tabs->currentWidget()) != nullptr)
        m_openKind = LibraryKind::Footprint;
    else if (qobject_cast<PartEditor *>(m_tabs->currentWidget()) != nullptr)
        m_openKind = LibraryKind::Part;
    QStringList problems;
    const Library builtIn = LibraryFolder::read(AppSettings::builtInLibraryFolder(), problems);
    Library user = LibraryFolder::read(AppSettings::userLibraryFolder(), problems);
    ItemChooser::Options options;
    options.builtIn = &builtIn;
    options.user = &user;
    options.source = m_openSource;
    options.remove = [this, &user](LibraryKind kind, const QString &name) {
        if (!deleteFromLibrary(kind, name))
            return false;
        QStringList ignored;
        user = LibraryFolder::read(AppSettings::userLibraryFolder(), ignored);
        return true;
    };
    const QString name = ItemChooser::choose(
        this, tr("Open"), *m_library,
        {LibraryKind::Symbol, LibraryKind::Footprint, LibraryKind::Part}, m_openKind,
        QDialogButtonBox::Open, QString(), &options);
    m_openSource = options.source;
    if (name.isEmpty())
        return nullptr;
    const Library &from = m_openSource == ItemChooser::Source::BuiltIn ? builtIn
                          : m_openSource == ItemChooser::Source::User  ? user
                                                                       : *m_library;
    switch (m_openKind) {
    case LibraryKind::Symbol: {
        const Symbol *symbol = from.findSymbol(name);
        return symbol == nullptr
                   ? nullptr
                   : openSymbol(name, *symbol, SymbolEditor::isInUserLibrary(name));
    }
    case LibraryKind::Footprint: {
        const Footprint *footprint = from.findFootprint(name);
        return footprint == nullptr
                   ? nullptr
                   : openFootprint(name, *footprint, FootprintEditor::isInUserLibrary(name));
    }
    case LibraryKind::Part: {
        const Part *part = from.findPart(name);
        return part == nullptr ? nullptr
                               : openPart(name, *part, PartEditor::isInUserLibrary(name));
    }
    }
    return nullptr;
}

bool LibraryEditor::deleteFromLibrary(LibraryKind kind, const QString &name)
{
    const char *subfolder = LibraryFolder::subfolder(kind);
    const QString path = LibraryFolder::itemPath(AppSettings::userLibraryFolder(), subfolder, name);
    if (name.isEmpty() || !QFileInfo::exists(path))
        return false;
    QStringList using_;
    if (m_library != nullptr) {
        if (kind == LibraryKind::Symbol)
            using_ = partsUsingSymbol(*m_library, name);
        else if (kind == LibraryKind::Footprint)
            using_ = partsUsingFootprint(*m_library, name);
    }
    QString question = tr("Delete %1 from your library").arg(name);
    if (!using_.isEmpty())
        question += QStringLiteral(". ")
                    + tr("These parts list it: %1").arg(using_.join(QStringLiteral(", ")));
    if (QFileInfo::exists(
            LibraryFolder::itemPath(AppSettings::builtInLibraryFolder(), subfolder, name)))
        question += QStringLiteral(". ") + tr("The built-in one of that name is used again");
    if (QMessageBox::warning(this, SheetEditor::actionName(SheetEditor::Action::Delete),
                             question + QLatin1Char('?'),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return false;
    // Its tab closes first; unsaved changes are asked about.
    for (int i = 0; i < m_tabs->count(); ++i) {
        LibraryTab *tab = qobject_cast<LibraryTab *>(m_tabs->widget(i));
        if (tab == nullptr || tab->name() != name || tabKind(tab) != kind)
            continue;
        if (!tab->confirmClose())
            return false;
        m_tabs->removeTab(i);
        tab->deleteLater();
        showTabsOrStart();
        break;
    }
    if (!QFile::remove(path)) {
        const Alert error = Alert::error(tr("Could not delete %1").arg(QDir::toNativeSeparators(path)));
        Alerts::show(this, error, [this](const QString &text) { m_status->setText(text); });
        return false;
    }
    m_status->setText(tr("%1 deleted from your library").arg(name));
    emit saved();
    return true;
}

LibraryKind LibraryEditor::tabKind(const QWidget *tab)
{
    if (qobject_cast<const SymbolEditor *>(tab) != nullptr)
        return LibraryKind::Symbol;
    if (qobject_cast<const FootprintEditor *>(tab) != nullptr)
        return LibraryKind::Footprint;
    return LibraryKind::Part;
}

ItemEditor *LibraryEditor::currentEditor() const
{
    return qobject_cast<ItemEditor *>(m_tabs->currentWidget());
}

FootprintEditor *LibraryEditor::currentFootprintEditor() const
{
    return qobject_cast<FootprintEditor *>(m_tabs->currentWidget());
}

bool LibraryEditor::closeTab(int index)
{
    LibraryTab *editor = qobject_cast<LibraryTab *>(m_tabs->widget(index));
    if (editor != nullptr && !editor->confirmClose())
        return false;
    QWidget *tab = m_tabs->widget(index);
    m_tabs->removeTab(index);
    tab->deleteLater();
    if (m_tabs->count() == 0)
        close();
    return true;
}

void LibraryEditor::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        LibraryTab *editor = qobject_cast<LibraryTab *>(m_tabs->widget(i));
        if (editor == nullptr)
            continue;
        m_tabs->setCurrentIndex(i);
        if (!editor->confirmClose()) {
            event->ignore();
            return;
        }
    }
    QMainWindow::closeEvent(event);
}

void LibraryEditor::showTabsOrStart()
{
    m_pages->setCurrentWidget(m_tabs->count() > 0 ? static_cast<QWidget *>(m_tabs) : m_start);
}

void LibraryEditor::tabShown(int index)
{
    LibraryTab *editor = qobject_cast<LibraryTab *>(m_tabs->widget(index));
    m_status->setText(editor != nullptr ? editor->status() : QString());
    // The keys of a tab work while the keyboard is in it.
    if (editor != nullptr)
        editor->takeFocus();
}

void LibraryEditor::updateTab(QWidget *tab)
{
    const int index = m_tabs->indexOf(tab);
    LibraryTab *editor = qobject_cast<LibraryTab *>(tab);
    if (index < 0 || editor == nullptr)
        return;
    m_tabs->setTabText(index, editor->tabTitle());
    m_tabs->setTabToolTip(index, editor->tabTip());
}

} // namespace ppcb
