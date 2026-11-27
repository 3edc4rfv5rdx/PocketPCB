#include "libraryeditor.h"

#include "canvasview.h"
#include "footprinteditor.h"
#include "format.h"
#include "librarypanel.h"
#include "symboleditor.h"
#include "toolicons.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace ppcb {

namespace {

// The least width of the drawing in Open…, in pixels.
const int OPEN_PREVIEW_SIZE = 200;
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
    QPushButton *startOpen = new QPushButton(tr("Open") + QStringLiteral("…"), m_start);
    startOpen->setObjectName(QStringLiteral("startOpenItem"));
    connect(startOpen, &QPushButton::clicked, this, &LibraryEditor::askOpen);
    QVBoxLayout *startLayout = new QVBoxLayout(m_start);
    startLayout->addStretch();
    for (QPushButton *button : {startFootprint, startSymbol, startOpen}) {
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
    // + offers a new footprint or a new symbol.
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
    connect(addMenu->addAction(FootprintEditor::newFootprintTitle()), &QAction::triggered, this,
            &LibraryEditor::openNewFootprint);
    connect(addMenu->addAction(SymbolEditor::newSymbolTitle()), &QAction::triggered, this,
            &LibraryEditor::openNewSymbol);
    addNewSymbolFromActions(addMenu->addMenu(SymbolEditor::newSymbolFromTitle()),
                            [this] { return this; });
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
    return openFootprint(QString(), Footprint(), false);
}

SymbolEditor *LibraryEditor::openNewSymbol()
{
    return openSymbol(QString(), Symbol(), false);
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

ItemEditor *LibraryEditor::openCopy()
{
    ItemEditor *copy = nullptr;
    if (const auto *footprint = qobject_cast<FootprintEditor *>(currentEditor()))
        copy = openFootprint(QString(), footprint->footprint(), false);
    else if (const auto *symbol = qobject_cast<SymbolEditor *>(currentEditor()))
        copy = openSymbol(QString(), symbol->symbol(), false);
    // Not saved anywhere yet: closing it asks.
    if (copy != nullptr)
        copy->undoStack()->resetClean();
    return copy;
}

ItemEditor *LibraryEditor::askOpen()
{
    if (m_library == nullptr)
        return nullptr;
    // What the tab shown edits, at first.
    if (qobject_cast<SymbolEditor *>(currentEditor()) != nullptr)
        m_openSymbols = true;
    else if (qobject_cast<FootprintEditor *>(currentEditor()) != nullptr)
        m_openSymbols = false;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Open"));
    QRadioButton *footprints = new QRadioButton(tr("Footprints"), &dialog);
    footprints->setObjectName(QStringLiteral("openFootprints"));
    QRadioButton *symbols = new QRadioButton(tr("Symbols"), &dialog);
    symbols->setObjectName(QStringLiteral("openSymbols"));
    QButtonGroup kinds(&dialog);
    kinds.addButton(footprints);
    kinds.addButton(symbols);
    (m_openSymbols ? symbols : footprints)->setChecked(true);
    QLineEdit *search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("openItemSearch"));
    search->setClearButtonEnabled(true);
    QListWidget *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("openItemList"));
    // The drawing of the one chosen, on the right.
    PartPreview *preview = new PartPreview(&dialog);
    preview->setObjectName(QStringLiteral("openItemPreview"));
    preview->setAlone(true);
    preview->setMinimumWidth(OPEN_PREVIEW_SIZE);
    const Library *library = m_library;
    const auto shown = [list] {
        const QListWidgetItem *item = list->currentItem();
        return item != nullptr && !item->isHidden() ? item->text() : QString();
    };
    const auto draw = [preview, library, symbols, shown] {
        const QString name = shown();
        if (symbols->isChecked())
            preview->setContent(library->findSymbol(name), nullptr);
        else
            preview->setContent(nullptr, library->findFootprint(name));
    };
    // The list holds the names with the text searched for.
    const auto filter = [list, search, draw] {
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            const bool match = item->text().contains(search->text().trimmed(), Qt::CaseInsensitive);
            item->setHidden(!match);
            if (match && first == nullptr)
                first = item;
        }
        list->setCurrentItem(first);
        draw();
    };
    const auto fill = [list, library, symbols, filter] {
        list->clear();
        list->addItems(symbols->isChecked() ? library->symbols.keys() : library->footprints.keys());
        filter();
    };
    connect(list, &QListWidget::currentItemChanged, &dialog, draw);
    connect(search, &QLineEdit::textChanged, &dialog, filter);
    connect(symbols, &QRadioButton::toggled, &dialog, fill);
    fill();
    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    QHBoxLayout *kindRow = new QHBoxLayout;
    kindRow->addWidget(footprints);
    kindRow->addWidget(symbols);
    kindRow->addStretch();
    QHBoxLayout *choice = new QHBoxLayout;
    choice->addWidget(list, 1);
    choice->addWidget(preview, 1);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addLayout(kindRow);
    layout->addWidget(search);
    layout->addLayout(choice, 1);
    layout->addWidget(buttons);
    search->setFocus();
    const bool accepted = dialog.exec() == QDialog::Accepted;
    m_openSymbols = symbols->isChecked();
    const QString name = shown();
    // The preview points into the library: nothing is drawn any more.
    preview->setContent(nullptr, nullptr);
    if (!accepted || name.isEmpty())
        return nullptr;
    if (m_openSymbols) {
        const Symbol *symbol = m_library->findSymbol(name);
        return symbol == nullptr
                   ? nullptr
                   : openSymbol(name, *symbol, SymbolEditor::isInUserLibrary(name));
    }
    const Footprint *footprint = m_library->findFootprint(name);
    return footprint == nullptr
               ? nullptr
               : openFootprint(name, *footprint, FootprintEditor::isInUserLibrary(name));
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
