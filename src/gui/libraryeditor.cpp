#include "libraryeditor.h"

#include "canvasview.h"
#include "footprinteditor.h"
#include "format.h"
#include "librarypanel.h"
#include "propertiespanel.h"
#include "toolicons.h"

#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
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
    QPushButton *startNew = new QPushButton(FootprintEditor::newFootprintTitle(), m_start);
    startNew->setObjectName(QStringLiteral("startNewFootprint"));
    connect(startNew, &QPushButton::clicked, this, &LibraryEditor::openNewFootprint);
    QPushButton *startOpen = new QPushButton(tr("Open") + QStringLiteral("…"), m_start);
    startOpen->setObjectName(QStringLiteral("startOpenFootprint"));
    connect(startOpen, &QPushButton::clicked, this, &LibraryEditor::askOpen);
    QVBoxLayout *startLayout = new QVBoxLayout(m_start);
    startLayout->addStretch();
    for (QPushButton *button : {startNew, startOpen}) {
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
    QToolButton *add = new QToolButton(corner);
    add->setObjectName(QStringLiteral("newFootprintTab"));
    add->setText(QStringLiteral("+"));
    add->setToolTip(FootprintEditor::newFootprintTitle());
    add->setAutoRaise(true);
    QFont bold = add->font();
    bold.setBold(true);
    if (bold.pointSizeF() > 0)
        bold.setPointSizeF(bold.pointSizeF() * ADD_BUTTON_SCALE);
    add->setFont(bold);
    connect(add, &QToolButton::clicked, this, &LibraryEditor::openNewFootprint);
    QPushButton *open = new QPushButton(tr("Open") + QStringLiteral("…"), corner);
    open->setObjectName(QStringLiteral("openFootprintTab"));
    connect(open, &QPushButton::clicked, this, &LibraryEditor::askOpen);
    QPushButton *copy = new QPushButton(tr("Copy"), corner);
    copy->setObjectName(QStringLiteral("copyFootprintTab"));
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

FootprintEditor *LibraryEditor::openFootprint(const QString &name, const Footprint &footprint,
                                              bool inUserLibrary)
{
    if (!name.isEmpty()) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            FootprintEditor *open = qobject_cast<FootprintEditor *>(m_tabs->widget(i));
            if (open != nullptr && open->name() == name) {
                m_tabs->setCurrentIndex(i);
                return open;
            }
        }
    }
    FootprintEditor *editor = new FootprintEditor(name, footprint, inUserLibrary, m_tabs);
    connect(editor, &FootprintEditor::titleChanged, this, [this, editor] { updateTab(editor); });
    connect(editor, &FootprintEditor::saved, this, &LibraryEditor::saved);
    connect(editor, &FootprintEditor::statusChanged, this, [this, editor](const QString &text) {
        if (m_tabs->currentWidget() == editor)
            m_status->setText(text);
    });
    connect(editor->canvas(), &CanvasView::cursorMoved, this, [this, editor](Coord x, Coord y) {
        if (m_tabs->currentWidget() == editor)
            m_position->setText(formatPosition(x, y));
    });
    m_tabs->setCurrentIndex(m_tabs->addTab(editor, editor->tabTitle()));
    updateTab(editor);
    showTabsOrStart();
    return editor;
}

FootprintEditor *LibraryEditor::openNewFootprint()
{
    return openFootprint(QString(), Footprint(), false);
}

FootprintEditor *LibraryEditor::openCopy()
{
    const FootprintEditor *shown = currentFootprintEditor();
    if (shown == nullptr)
        return nullptr;
    FootprintEditor *copy = openFootprint(QString(), shown->footprint(), false);
    // Not saved anywhere yet: closing it asks.
    copy->undoStack()->resetClean();
    return copy;
}

FootprintEditor *LibraryEditor::askOpen()
{
    if (m_library == nullptr)
        return nullptr;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Open") + QStringLiteral(" — ") + PropertiesPanel::footprintTitle());
    QLineEdit *search = new QLineEdit(&dialog);
    search->setObjectName(QStringLiteral("openFootprintSearch"));
    search->setClearButtonEnabled(true);
    QListWidget *list = new QListWidget(&dialog);
    list->setObjectName(QStringLiteral("openFootprintList"));
    list->addItems(m_library->footprints.keys());
    // The drawing of the one chosen, on the right.
    PartPreview *preview = new PartPreview(&dialog);
    preview->setObjectName(QStringLiteral("openFootprintPreview"));
    preview->setFootprintAlone(true);
    preview->setMinimumWidth(OPEN_PREVIEW_SIZE);
    const Library *library = m_library;
    connect(list, &QListWidget::currentItemChanged, &dialog,
            [preview, library](const QListWidgetItem *item) {
                preview->setContent(nullptr, item != nullptr && !item->isHidden()
                                                 ? library->findFootprint(item->text())
                                                 : nullptr);
            });
    if (list->count() > 0)
        list->setCurrentRow(0);
    // The list holds the names with the text searched for.
    connect(search, &QLineEdit::textChanged, &dialog, [list, preview](const QString &text) {
        QListWidgetItem *first = nullptr;
        for (int i = 0; i < list->count(); ++i) {
            QListWidgetItem *item = list->item(i);
            const bool shown = item->text().contains(text.trimmed(), Qt::CaseInsensitive);
            item->setHidden(!shown);
            if (shown && first == nullptr)
                first = item;
        }
        list->setCurrentItem(first);
        // None left: nothing drawn.
        if (first == nullptr)
            preview->setContent(nullptr, nullptr);
    });
    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::itemActivated, &dialog, &QDialog::accept);
    QHBoxLayout *choice = new QHBoxLayout;
    choice->addWidget(list, 1);
    choice->addWidget(preview, 1);
    QVBoxLayout *layout = new QVBoxLayout(&dialog);
    layout->addWidget(search);
    layout->addLayout(choice, 1);
    layout->addWidget(buttons);
    search->setFocus();
    if (dialog.exec() != QDialog::Accepted || list->currentItem() == nullptr
        || list->currentItem()->isHidden())
        return nullptr;
    const QString name = list->currentItem()->text();
    const Footprint *footprint = m_library->findFootprint(name);
    if (footprint == nullptr)
        return nullptr;
    return openFootprint(name, *footprint, FootprintEditor::isInUserLibrary(name));
}

FootprintEditor *LibraryEditor::currentFootprintEditor() const
{
    return qobject_cast<FootprintEditor *>(m_tabs->currentWidget());
}

bool LibraryEditor::closeTab(int index)
{
    FootprintEditor *editor = qobject_cast<FootprintEditor *>(m_tabs->widget(index));
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
        FootprintEditor *editor = qobject_cast<FootprintEditor *>(m_tabs->widget(i));
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
    FootprintEditor *editor = qobject_cast<FootprintEditor *>(m_tabs->widget(index));
    m_status->setText(editor != nullptr ? editor->status() : QString());
    // The keys of a tab work while the keyboard is in it.
    if (editor != nullptr)
        editor->canvas()->setFocus();
}

void LibraryEditor::updateTab(QWidget *tab)
{
    const int index = m_tabs->indexOf(tab);
    FootprintEditor *editor = qobject_cast<FootprintEditor *>(tab);
    if (index < 0 || editor == nullptr)
        return;
    m_tabs->setTabText(index, editor->tabTitle());
    m_tabs->setTabToolTip(index, editor->tabTip());
}

} // namespace ppcb
