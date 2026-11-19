#include "projectpropertiesdialog.h"

#include "format.h"
#include "netspanel.h"
#include "sheetedit.h"
#include "sheeteditor.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace ppcb {

namespace {

// The least width of the dialog, in pixels.
const int MIN_DIALOG_WIDTH = 560;
// The sizes of a net class, in millimetres: the least and the largest
// typed, the step of the arrows, the decimals shown.
const double MIN_CLASS_MM = 0.01;
const double MAX_CLASS_MM = 50;
const double CLASS_MM_STEP = 0.05;
const int CLASS_MM_DECIMALS = 3;
// What a row of the table of classes keeps in its name: the name the class
// had, and a number that tells the row apart while names are typed.
const int ORIGINAL_NAME_ROLE = Qt::UserRole;
const int ROW_KEY_ROLE = Qt::UserRole + 1;
// What a size box keeps: the size it was given, in nanometres.
const char SIZE_PROPERTY[] = "nm";

QDoubleSpinBox *sizeBox(Coord nm, QWidget *parent)
{
    QDoubleSpinBox *box = new QDoubleSpinBox(parent);
    box->setDecimals(CLASS_MM_DECIMALS);
    box->setRange(MIN_CLASS_MM, MAX_CLASS_MM);
    box->setSingleStep(CLASS_MM_STEP);
    box->setValue(nmToMm(nm));
    box->setProperty(SIZE_PROPERTY, QVariant::fromValue(qint64(nm)));
    box->setFrame(false);
    return box;
}

// The size in 'box': the one it was given while what it shows is that
// one rounded, so that a size finer than the decimals is not lost unseen.
Coord sizeOf(const QDoubleSpinBox *box)
{
    const Coord given = box->property(SIZE_PROPERTY).toLongLong();
    const double scale = std::pow(10.0, CLASS_MM_DECIMALS);
    const double shown = std::round(nmToMm(given) * scale) / scale;
    if (std::abs(box->value() - shown) < 0.5 / scale)
        return given;
    Coord nm = given;
    tryMmToNm(box->value(), nm);
    return nm;
}

} // namespace

ProjectPropertiesDialog::ProjectPropertiesDialog(const Design &design, QWidget *parent)
    : QDialog(parent)
    , m_design(design)
{
    setWindowTitle(title());

    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(makeGeneralTab(), tr("General"));
    tabs->addTab(makeNetClassesTab(), tr("Net classes"));

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    resize(qMax(sizeHint().width(), MIN_DIALOG_WIDTH), sizeHint().height());
}

QWidget *ProjectPropertiesDialog::makeGeneralTab()
{
    QWidget *tab = new QWidget(this);
    const TitleBlock &block = m_design.project.titleBlock;
    const auto lineEdit = [tab](const QString &text, const char *name) {
        QLineEdit *edit = new QLineEdit(text, tab);
        edit->setObjectName(QLatin1String(name));
        return edit;
    };
    m_titleEdit = lineEdit(block.title, "projectTitleEdit");
    m_authorEdit = lineEdit(block.author, "projectAuthorEdit");
    m_organizationEdit = lineEdit(block.organization, "projectOrganizationEdit");
    m_contactsEdit = lineEdit(block.contacts, "projectContactsEdit");
    m_revisionEdit = lineEdit(block.revision, "projectRevisionEdit");
    m_dateEdit = lineEdit(block.date, "projectDateEdit");
    m_dateEdit->setPlaceholderText(tr("Date of the last save"));
    m_noteEdit = lineEdit(block.note, "projectNoteEdit");

    const QString colon = QStringLiteral(":");
    QGroupBox *blockBox = new QGroupBox(tr("Title block"), tab);
    QFormLayout *blockForm = new QFormLayout(blockBox);
    blockForm->addRow(tr("Title") + colon, m_titleEdit);
    blockForm->addRow(tr("Author") + colon, m_authorEdit);
    blockForm->addRow(tr("Organization") + colon, m_organizationEdit);
    blockForm->addRow(tr("Contacts") + colon, m_contactsEdit);
    blockForm->addRow(tr("Revision") + colon, m_revisionEdit);
    blockForm->addRow(tr("Date") + colon, m_dateEdit);
    blockForm->addRow(tr("Note") + colon, m_noteEdit);

    // In the order of DesignatorStyle.
    m_styleBox = new QComboBox(tab);
    m_styleBox->setObjectName(QStringLiteral("designatorStyleBox"));
    m_styleBox->addItem(tr("International") + QStringLiteral(" (R, D, Q, U)"));
    m_styleBox->addItem(tr("GOST") + QStringLiteral(" (R, VD, VT, DD)"));
    m_styleBox->setCurrentIndex(int(m_design.project.designatorStyle));
    QFormLayout *styleForm = new QFormLayout;
    styleForm->addRow(tr("Designator style") + colon, m_styleBox);

    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->addWidget(blockBox);
    layout->addLayout(styleForm);
    layout->addStretch(1);
    return tab;
}

QWidget *ProjectPropertiesDialog::makeNetClassesTab()
{
    QWidget *tab = new QWidget(this);
    const QString inMm = QStringLiteral(", ") + mmUnit();
    m_classTable = new QTableWidget(0, ClassColumnCount, tab);
    m_classTable->setObjectName(QStringLiteral("netClassTable"));
    m_classTable->setHorizontalHeaderLabels(
        {NetsPanel::classTitle(), tr("Track width") + inMm, tr("Clearance") + inMm,
         tr("Via hole") + inMm, tr("Via diameter") + inMm});
    m_classTable->verticalHeader()->hide();
    m_classTable->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_classTable->horizontalHeader()->setSectionResizeMode(ClassName, QHeaderView::Stretch);
    m_classTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_classTable->setSelectionMode(QAbstractItemView::SingleSelection);

    QPushButton *addButton = new QPushButton(tr("Add"), tab);
    addButton->setObjectName(QStringLiteral("addNetClassButton"));
    m_deleteClassButton =
        new QPushButton(SheetEditor::actionName(SheetEditor::Action::Delete), tab);
    m_deleteClassButton->setObjectName(QStringLiteral("deleteNetClassButton"));
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(m_deleteClassButton);
    buttons->addStretch(1);

    m_defaultClassBox = new QComboBox(tab);
    m_defaultClassBox->setObjectName(QStringLiteral("defaultClassBox"));
    m_powerClassBox = new QComboBox(tab);
    m_powerClassBox->setObjectName(QStringLiteral("powerClassBox"));
    const QString colon = QStringLiteral(":");
    QFormLayout *choices = new QFormLayout;
    choices->addRow(tr("Default class") + colon, m_defaultClassBox);
    choices->addRow(tr("Power class") + colon, m_powerClassBox);

    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->addWidget(m_classTable, 1);
    layout->addLayout(buttons);
    layout->addLayout(choices);

    const Project &project = m_design.project;
    for (const NetClass &netClass : project.netClasses)
        appendClassRow(netClass.name, netClass);
    updateClassChoices();
    // The rows have the keys 0, 1... in the order of the project's classes.
    const auto choose = [&project](QComboBox *box, const QString &name) {
        for (int i = 0; i < project.netClasses.size(); ++i) {
            if (project.netClasses.at(i).name == name)
                box->setCurrentIndex(box->findData(i));
        }
    };
    choose(m_defaultClassBox, project.defaultClass);
    choose(m_powerClassBox, project.powerClass);

    connect(addButton, &QPushButton::clicked, this, &ProjectPropertiesDialog::addClass);
    connect(m_deleteClassButton, &QPushButton::clicked, this,
            &ProjectPropertiesDialog::deleteClass);
    connect(m_classTable, &QTableWidget::itemChanged, this,
            &ProjectPropertiesDialog::updateClassChoices);
    return tab;
}

void ProjectPropertiesDialog::appendClassRow(const QString &original, const NetClass &netClass)
{
    const int row = m_classTable->rowCount();
    // Filling a row is not typing a name.
    const QSignalBlocker blocker(m_classTable);
    m_classTable->insertRow(row);
    QTableWidgetItem *name = new QTableWidgetItem(netClass.name);
    name->setData(ORIGINAL_NAME_ROLE, original);
    name->setData(ROW_KEY_ROLE, m_nextRowKey++);
    m_classTable->setItem(row, ClassName, name);
    m_classTable->setCellWidget(row, TrackWidth, sizeBox(netClass.trackWidth, m_classTable));
    m_classTable->setCellWidget(row, Clearance, sizeBox(netClass.clearance, m_classTable));
    m_classTable->setCellWidget(row, ViaDrill, sizeBox(netClass.viaDrill, m_classTable));
    m_classTable->setCellWidget(row, ViaSize, sizeBox(netClass.viaSize, m_classTable));
}

void ProjectPropertiesDialog::addClass()
{
    const QList<NetClassEdit> edits = netClassEdits();
    QSet<QString> taken;
    NetClass netClass = edits.isEmpty() ? NetClass() : edits.first().netClass;
    for (int row = 0; row < edits.size(); ++row) {
        taken.insert(edits.at(row).netClass.name);
        if (m_classTable->item(row, ClassName)->data(ROW_KEY_ROLE)
            == m_defaultClassBox->currentData())
            netClass = edits.at(row).netClass;
    }
    int number = 1;
    const QString base = NetsPanel::classTitle() + QLatin1Char(' ');
    while (taken.contains(base + QString::number(number)))
        ++number;
    netClass.name = base + QString::number(number);
    appendClassRow(QString(), netClass);
    updateClassChoices();
    const int row = m_classTable->rowCount() - 1;
    m_classTable->setCurrentCell(row, ClassName);
    m_classTable->editItem(m_classTable->item(row, ClassName));
}

void ProjectPropertiesDialog::deleteClass()
{
    const int row = m_classTable->currentRow();
    if (row < 0 || m_classTable->rowCount() <= 1)
        return;
    m_classTable->removeRow(row);
    updateClassChoices();
}

void ProjectPropertiesDialog::updateClassChoices()
{
    for (QComboBox *box : {m_defaultClassBox, m_powerClassBox}) {
        const QVariant shown = box->currentData();
        const QSignalBlocker blocker(box);
        box->clear();
        for (int row = 0; row < m_classTable->rowCount(); ++row) {
            const QTableWidgetItem *name = m_classTable->item(row, ClassName);
            box->addItem(name->text().trimmed(), name->data(ROW_KEY_ROLE));
        }
        box->setCurrentIndex(qMax(0, box->findData(shown)));
    }
    m_deleteClassButton->setEnabled(m_classTable->rowCount() > 1);
}

QList<NetClassEdit> ProjectPropertiesDialog::netClassEdits() const
{
    QList<NetClassEdit> edits;
    for (int row = 0; row < m_classTable->rowCount(); ++row) {
        const QTableWidgetItem *name = m_classTable->item(row, ClassName);
        const auto size = [this, row](int column) {
            return sizeOf(static_cast<const QDoubleSpinBox *>(m_classTable->cellWidget(row, column)));
        };
        NetClassEdit edit;
        edit.original = name->data(ORIGINAL_NAME_ROLE).toString();
        edit.netClass.name = name->text().trimmed();
        edit.netClass.trackWidth = size(TrackWidth);
        edit.netClass.clearance = size(Clearance);
        edit.netClass.viaDrill = size(ViaDrill);
        edit.netClass.viaSize = size(ViaSize);
        edits.append(edit);
    }
    return edits;
}

QString ProjectPropertiesDialog::classProblem() const
{
    QSet<QString> names;
    for (const NetClassEdit &edit : netClassEdits()) {
        const NetClass &netClass = edit.netClass;
        if (netClass.name.isEmpty())
            return tr("A net class needs a name");
        if (names.contains(netClass.name))
            return tr("Two net classes have the name") + QStringLiteral(" ") + netClass.name;
        names.insert(netClass.name);
        if (netClass.viaSize <= netClass.viaDrill) {
            return tr("The via diameter must be larger than the via hole") + QStringLiteral(": ")
                   + netClass.name;
        }
    }
    return QString();
}

void ProjectPropertiesDialog::accept()
{
    const QString problem = classProblem();
    if (!problem.isEmpty()) {
        QMessageBox::warning(this, title(), problem + QLatin1Char('.'));
        return;
    }
    QSet<QString> kept;
    for (const NetClassEdit &edit : netClassEdits())
        kept.insert(edit.original);
    QStringList deletedUsed;
    for (const NetClass &netClass : m_design.project.netClasses) {
        if (!kept.contains(netClass.name) && isNetClassUsed(m_design, netClass.name))
            deletedUsed.append(netClass.name);
    }
    if (!deletedUsed.isEmpty()) {
        const QString question =
            tr("Nets of the deleted classes will take the default or the power class")
            + QStringLiteral(": ") + deletedUsed.join(QStringLiteral(", ")) + QLatin1Char('.');
        if (QMessageBox::question(this, title(), question) != QMessageBox::Yes)
            return;
    }
    QDialog::accept();
}

QString ProjectPropertiesDialog::title()
{
    return tr("Project properties");
}

Design ProjectPropertiesDialog::design() const
{
    Design result = m_design;
    TitleBlock &block = result.project.titleBlock;
    block.title = m_titleEdit->text().trimmed();
    block.author = m_authorEdit->text().trimmed();
    block.organization = m_organizationEdit->text().trimmed();
    block.contacts = m_contactsEdit->text().trimmed();
    block.revision = m_revisionEdit->text().trimmed();
    block.date = m_dateEdit->text().trimmed();
    block.note = m_noteEdit->text().trimmed();

    const DesignatorStyle style = DesignatorStyle(m_styleBox->currentIndex());
    if (style != result.project.designatorStyle) {
        result.schematic = withDesignatorStyle(result.schematic, result.library,
                                               result.project.designatorStyle, style);
        result.project.designatorStyle = style;
    }

    const QList<NetClassEdit> edits = netClassEdits();
    // The name in the row the list shows.
    const auto chosen = [this, &edits](const QComboBox *box) {
        for (int row = 0; row < edits.size(); ++row) {
            if (m_classTable->item(row, ClassName)->data(ROW_KEY_ROLE) == box->currentData())
                return edits.at(row).netClass.name;
        }
        return edits.value(0).netClass.name;
    };
    return withNetClasses(result, edits, chosen(m_defaultClassBox), chosen(m_powerClassBox));
}

} // namespace ppcb
