#include "netspanel.h"

#include "document.h"
#include "netlist.h"
#include "sheeteditor.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QHeaderView>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace ppcb {

namespace {

enum Column { NameColumn, ClassColumn, PinsColumn, ColumnCount };

// "R1.2, C3.1": the pins of a net.
QString pinsText(const Net &net)
{
    QStringList pins;
    for (const NetPin &pin : net.pins)
        pins.append(pin.designator + QLatin1Char('.') + pin.number);
    return pins.join(QStringLiteral(", "));
}

const Net *findNet(const QList<Net> &all, const QString &name)
{
    for (const Net &net : all) {
        if (net.name == name)
            return &net;
    }
    return nullptr;
}

} // namespace

NetsPanel::NetsPanel(Document *document, SheetEditor *editor, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
    , m_editor(editor)
{
    m_filter = new QLineEdit(this);
    m_filter->setObjectName(QStringLiteral("netsFilter"));
    m_filter->setPlaceholderText(tr("Filter"));
    m_filter->setClearButtonEnabled(true);
    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setObjectName(QStringLiteral("netsTable"));
    m_table->setHorizontalHeaderLabels({tr("Name"), tr("Class"), tr("Pins")});
    m_table->verticalHeader()->hide();
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(NameColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ClassColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(PinsColumn, QHeaderView::Stretch);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_filter);
    layout->addWidget(m_table);

    connect(m_filter, &QLineEdit::textChanged, this, &NetsPanel::applyFilter);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &NetsPanel::rowChosen);
    connect(m_editor, &SheetEditor::netsChanged, this, &NetsPanel::refresh);
    // Esc on the sheet drops the net chosen: so does the table.
    connect(m_editor, &SheetEditor::changed, this, [this] {
        if (m_editor->chosenNet().isEmpty() && !m_table->selectedItems().isEmpty()) {
            const QSignalBlocker blocker(m_table);
            m_table->clearSelection();
        }
    });
    refresh();
}

QString NetsPanel::netsTitle()
{
    return tr("Nets");
}

void NetsPanel::refresh()
{
    const QSignalBlocker blocker(m_table);
    const Design &design = m_document->design();
    const QList<Net> &all = m_editor->nets();
    QStringList classes;
    for (const NetClass &netClass : design.project.netClasses)
        classes.append(netClass.name);

    m_table->clearContents();
    m_table->setRowCount(int(all.size()));
    int chosenRow = -1;
    for (int row = 0; row < all.size(); ++row) {
        const Net &net = all.at(row);
        m_table->setItem(row, NameColumn, new QTableWidgetItem(net.name));
        m_table->setItem(row, PinsColumn, new QTableWidgetItem(pinsText(net)));
        QComboBox *box = new QComboBox(m_table);
        box->addItems(classes);
        box->installEventFilter(this);
        const QString netClass = netClassOf(net, design.project, design.schematic);
        box->setCurrentIndex(int(classes.indexOf(netClass)));
        // A net of wires alone has no name to keep a class by.
        if (!net.named && net.pins.isEmpty()) {
            box->setEnabled(false);
            box->setToolTip(tr("A net with no pins has no class"));
        }
        // Queued: the step fills the table again, and the box goes with it.
        const QString name = net.name;
        connect(
            box, &QComboBox::activated, this,
            [this, name, classes](int index) { chooseClass(name, classes.value(index)); },
            Qt::QueuedConnection);
        m_table->setCellWidget(row, ClassColumn, box);
        if (net.name == m_editor->chosenNet())
            chosenRow = row;
    }
    if (chosenRow >= 0)
        m_table->selectRow(chosenRow);
    else if (!m_editor->chosenNet().isEmpty())
        m_editor->setChosenNet(QString());
    applyFilter();
}

void NetsPanel::applyFilter()
{
    const QString text = m_filter->text().trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const bool kept =
            text.isEmpty()
            || m_table->item(row, NameColumn)->text().contains(text, Qt::CaseInsensitive)
            || m_table->item(row, PinsColumn)->text().contains(text, Qt::CaseInsensitive);
        m_table->setRowHidden(row, !kept);
    }
}

void NetsPanel::chooseClass(const QString &name, const QString &netClass)
{
    const QList<Net> &all = m_editor->nets();
    const Net *net = findNet(all, name);
    Design design = m_document->design();
    if (net == nullptr || netClassOf(*net, design.project, design.schematic) == netClass)
        return;
    const Project before = design.project;
    design.project = withNetClass(design.project, all, *net, netClass);
    if (m_editor->chosenNet() == name && !net->named) {
        // Named now by the table: the name that is new.
        for (const NetByPins &byPins : design.project.netsByPins) {
            const bool isNew = std::none_of(
                before.netsByPins.cbegin(), before.netsByPins.cend(),
                [&byPins](const NetByPins &old) { return old.name == byPins.name; });
            if (isNew)
                m_editor->setChosenNet(byPins.name);
        }
    }
    m_document->change(
        tr("Net class") + QLatin1Char(' ') + name + QStringLiteral(": ") + netClass, design);
}

bool NetsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Wheel && qobject_cast<QComboBox *>(watched) != nullptr) {
        QCoreApplication::sendEvent(m_table->viewport(), event);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void NetsPanel::rowChosen()
{
    const QList<QTableWidgetItem *> chosen = m_table->selectedItems();
    QString name;
    if (!chosen.isEmpty())
        name = m_table->item(chosen.first()->row(), NameColumn)->text();
    m_editor->setChosenNet(name);
    emit netChosen(name);
}

} // namespace ppcb
