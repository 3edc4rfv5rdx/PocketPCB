#include "sheettabs.h"

#include "document.h"
#include "sheeteditor.h"

#include <QActionGroup>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>

namespace ppcb {

namespace {

// The id a tab keeps; the board's tab keeps the null id.
ObjectId tabId(const QTabBar *bar, int tab)
{
    return ObjectId{bar->tabData(tab).toULongLong()};
}

} // namespace

SheetTabs::SheetTabs(Document *document, SheetEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_editor(editor)
{
    m_bar = new QTabBar(this);
    m_bar->setExpanding(false);
    m_bar->setDrawBase(false);
    m_bar->setMovable(true);
    m_bar->setContextMenuPolicy(Qt::CustomContextMenu);
    m_bar->installEventFilter(this);

    QToolButton *addButton = new QToolButton(this);
    addButton->setText(QStringLiteral("+"));
    addButton->setToolTip(tr("Add sheet"));
    addButton->setAutoRaise(true);

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_bar);
    layout->addWidget(addButton);
    layout->addStretch();

    connect(addButton, &QToolButton::clicked, this,
            [this] { addSheet(int(m_document->design().schematic.sheets.size())); });
    // 'currentChanged' covers the keyboard and a tab passing its place while
    // dragged; 'tabBarClicked' adds the click on the current tab, which
    // brings the project back from a library file.
    const auto choose = [this](int tab) {
        const int page = tab >= 0 ? pageOfTab(tab) : -1;
        if (page >= 0)
            emit pageChosen(page);
    };
    connect(m_bar, &QTabBar::currentChanged, this, choose);
    connect(m_bar, &QTabBar::tabBarClicked, this, choose);
    // A tab may take its new place while the button is still held, or after
    // it is let go; the order is taken once it is let go (see eventFilter()).
    connect(m_bar, &QTabBar::tabMoved, this, [this] {
        if (QGuiApplication::mouseButtons() == Qt::NoButton)
            applyTabOrder();
    });
    connect(m_bar, &QTabBar::tabBarDoubleClicked, this, &SheetTabs::startRename);
    connect(m_bar, &QTabBar::customContextMenuRequested, this, &SheetTabs::showMenu);
}

void SheetTabs::updatePages()
{
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    const int count = int(sheets.size()) + 1;
    // The tabs are relabelled in place, not made anew, so that a drag just
    // let go is not cut short.
    const QSignalBlocker blocker(m_bar);
    while (m_bar->count() > count)
        m_bar->removeTab(m_bar->count() - 1);
    while (m_bar->count() < count)
        m_bar->addTab(QString());
    for (int i = 0; i < sheets.size(); ++i) {
        m_bar->setTabText(i, SheetEditor::sheetName(sheets.at(i), i));
        m_bar->setTabData(i, QVariant::fromValue(sheets.at(i).id.value));
    }
    m_bar->setTabText(count - 1, tr("Board"));
    m_bar->setTabData(count - 1, QVariant::fromValue(ObjectId().value));
}

void SheetTabs::setCurrentPage(int page)
{
    const int tab = tabOfPage(page);
    if (tab < 0)
        return;
    const QSignalBlocker blocker(m_bar);
    m_bar->setCurrentIndex(tab);
}

bool SheetTabs::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_bar && event->type() == QEvent::MouseButtonRelease) {
        // Once the tab bar has dealt with the release.
        QTimer::singleShot(0, this, &SheetTabs::applyTabOrder);
    } else if (watched == m_renameEdit && event->type() == QEvent::ShortcutOverride) {
        // Esc and Enter belong to the line, not to the window's actions.
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Return || key == Qt::Key_Enter) {
            event->accept();
            return true;
        }
    } else if (watched == m_renameEdit && event->type() == QEvent::KeyPress
               && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        finishRename(false);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

int SheetTabs::pageOfTab(int tab) const
{
    if (tab < 0 || tab >= m_bar->count())
        return -1;
    const ObjectId id = tabId(m_bar, tab);
    if (id.isNull())
        return int(m_document->design().schematic.sheets.size());
    return sheetIndex(id);
}

int SheetTabs::tabOfPage(int page) const
{
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    if (page < 0 || page > sheets.size())
        return -1;
    const ObjectId id = page < sheets.size() ? sheets.at(page).id : ObjectId();
    for (int tab = 0; tab < m_bar->count(); ++tab) {
        if (tabId(m_bar, tab) == id)
            return tab;
    }
    return -1;
}

int SheetTabs::sheetIndex(const ObjectId &id) const
{
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    for (int i = 0; i < sheets.size(); ++i) {
        if (sheets.at(i).id == id)
            return i;
    }
    return -1;
}

void SheetTabs::showMenu(const QPoint &pos)
{
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    const int tab = m_bar->tabAt(pos);
    const int page = pageOfTab(tab);
    const bool onSheet = page >= 0 && page < sheets.size();
    // Read before the menu runs: a step may change the sheets meanwhile.
    const ObjectId id = onSheet ? sheets.at(page).id : ObjectId();
    const PaperSize paper = onSheet ? sheets.at(page).paper : PaperSize::A4;
    const bool lastSheet = sheets.size() <= 1;

    QMenu menu(this);
    // A new sheet goes after the one clicked, else at the end.
    QAction *addAction = menu.addAction(tr("Add sheet"));
    QAction *renameAction = nullptr;
    QAction *a4Action = nullptr;
    QAction *a3Action = nullptr;
    QAction *deleteAction = nullptr;
    if (onSheet) {
        renameAction = menu.addAction(tr("Rename sheet"));
        QMenu *paperMenu = menu.addMenu(tr("Paper"));
        QActionGroup *paperGroup = new QActionGroup(paperMenu);
        a4Action = paperMenu->addAction(paperSizeName(PaperSize::A4));
        a3Action = paperMenu->addAction(paperSizeName(PaperSize::A3));
        for (QAction *action : {a4Action, a3Action}) {
            action->setCheckable(true);
            paperGroup->addAction(action);
        }
        (paper == PaperSize::A3 ? a3Action : a4Action)->setChecked(true);
        menu.addSeparator();
        deleteAction = menu.addAction(tr("Delete sheet"));
        deleteAction->setEnabled(!lastSheet);
    }

    QAction *chosen = menu.exec(m_bar->mapToGlobal(pos));
    if (chosen == nullptr)
        return;
    if (chosen == addAction) {
        const int at = onSheet ? sheetIndex(id) + 1 : int(sheets.size());
        addSheet(at > 0 ? at : int(m_document->design().schematic.sheets.size()));
    } else if (chosen == renameAction) {
        startRename(tabOfPage(sheetIndex(id)));
    } else if (chosen == a4Action) {
        setPaper(id, PaperSize::A4);
    } else if (chosen == a3Action) {
        setPaper(id, PaperSize::A3);
    } else if (chosen == deleteAction) {
        deleteSheet(id);
    }
}

void SheetTabs::addSheet(int index)
{
    settleEditor();
    Design design = m_document->design();
    QList<Sheet> &sheets = design.schematic.sheets;
    index = qBound(0, index, int(sheets.size()));
    Sheet sheet;
    sheet.id = newObjectId();
    sheets.insert(index, sheet);
    // No page: undone, the sheet is gone, and its place may be the board's.
    m_document->change(stepName(tr("Add sheet"), SheetEditor::sheetName(sheet, index)), design);
    emit pageChosen(index);
}

void SheetTabs::deleteSheet(const ObjectId &id)
{
    int index = sheetIndex(id);
    if (index < 0 || m_document->design().schematic.sheets.size() <= 1)
        return;
    const Sheet &sheet = m_document->design().schematic.sheets.at(index);
    const QString name = SheetEditor::sheetName(sheet, index);
    if (!isSheetEmpty(sheet)) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, tr("Delete sheet"),
            tr("Delete %1 with everything on it").arg(name) + QLatin1Char('?'));
        if (answer != QMessageBox::Yes)
            return;
    }
    settleEditor();
    index = sheetIndex(id);
    if (index < 0)
        return;
    Design design = m_document->design();
    design.schematic.sheets.removeAt(index);
    // No page: redone, the sheet is gone again.
    m_document->change(stepName(tr("Delete sheet"), name), design);
}

void SheetTabs::setPaper(const ObjectId &id, PaperSize paper)
{
    const int index = sheetIndex(id);
    if (index < 0)
        return;
    Design design = m_document->design();
    Sheet &sheet = design.schematic.sheets[index];
    if (sheet.paper == paper)
        return;
    sheet.paper = paper;
    m_document->change(stepName(tr("Paper") + QLatin1Char(' ') + paperSizeName(paper),
                                SheetEditor::sheetName(sheet, index)),
                       design, index);
}

void SheetTabs::startRename(int tab)
{
    const int page = pageOfTab(tab);
    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    if (page < 0 || page >= sheets.size())
        return;
    finishRename(true);
    m_renamedSheet = sheets.at(page).id;
    m_renameEdit = new QLineEdit(m_bar);
    m_renameEdit->setText(SheetEditor::sheetName(sheets.at(page), page));
    m_renameEdit->setGeometry(m_bar->tabRect(tab));
    m_renameEdit->installEventFilter(this);
    connect(m_renameEdit, &QLineEdit::editingFinished, this, [this] { finishRename(true); });
    m_renameEdit->show();
    m_renameEdit->selectAll();
    m_renameEdit->setFocus();
}

void SheetTabs::finishRename(bool keep)
{
    if (m_renameEdit == nullptr)
        return;
    // Cleared first: hiding the line ends its editing once more.
    QLineEdit *edit = m_renameEdit;
    m_renameEdit = nullptr;
    const QString text = edit->text().trimmed();
    edit->hide();
    edit->deleteLater();
    const int index = sheetIndex(m_renamedSheet);
    if (!keep || index < 0)
        return;
    Design design = m_document->design();
    Sheet &sheet = design.schematic.sheets[index];
    // The name shown, kept as it is, changes nothing; a sheet with no title
    // keeps having none.
    if (text == SheetEditor::sheetName(sheet, index))
        return;
    sheet.title = text;
    m_document->change(stepName(tr("Rename sheet"), SheetEditor::sheetName(sheet, index)),
                       design, index);
}

void SheetTabs::applyTabOrder()
{
    // The sheets in the order of their tabs, read before anything changes
    // the tabs again.
    QList<ObjectId> order;
    for (int tab = 0; tab < m_bar->count(); ++tab) {
        const ObjectId id = tabId(m_bar, tab);
        if (!id.isNull())
            order.append(id);
    }
    const ObjectId dragged = tabId(m_bar, m_bar->currentIndex());

    const QList<Sheet> &sheets = m_document->design().schematic.sheets;
    bool moved = false;
    for (int i = 0; i < order.size() && i < sheets.size(); ++i)
        moved = moved || order.at(i) != sheets.at(i).id;
    if (!moved || order.size() != sheets.size()) {
        // Only the board's tab moved, or nothing did: it goes back to the end.
        const int page = pageOfTab(m_bar->currentIndex());
        updatePages();
        setCurrentPage(page);
        return;
    }

    settleEditor();
    Design design = m_document->design();
    QList<Sheet> ordered;
    for (const ObjectId &id : order) {
        const int index = sheetIndex(id);
        if (index < 0)
            return;
        ordered.append(design.schematic.sheets.at(index));
    }
    if (ordered.size() != design.schematic.sheets.size())
        return;
    design.schematic.sheets = ordered;
    const int index = int(order.indexOf(dragged));
    const QString name =
        index >= 0 ? SheetEditor::sheetName(ordered.at(index), index) : QString();
    // No page: undone or redone, the sheet shown stays shown.
    m_document->change(stepName(tr("Move sheet"), name), design);
}

void SheetTabs::settleEditor()
{
    m_editor->setSheet(m_editor->sheet());
}

QString SheetTabs::stepName(const QString &action, const QString &sheet)
{
    if (sheet.isEmpty())
        return action;
    return action + QStringLiteral(" (") + sheet + QLatin1Char(')');
}

} // namespace ppcb
