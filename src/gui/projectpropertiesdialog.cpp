#include "projectpropertiesdialog.h"

#include "boardeditor.h"
#include "format.h"
#include "jsonio.h"
#include "netspanel.h"
#include "paint.h"
#include "sheetedit.h"
#include "sheeteditor.h"
#include "trackedit.h"

#include <QCheckBox>
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


namespace ppcb {

namespace {

// The least width of the dialog, in pixels.
const int MIN_DIALOG_WIDTH = 560;
// The sizes of a net class, in millimetres: the least and the largest
// typed, the step of the arrows.
const double MIN_CLASS_MM = 0.01;
const double MAX_CLASS_MM = 50;
const double CLASS_MM_STEP = 0.05;
// What a row of the table of classes keeps in its name: the name the class
// had, and a number that tells the row apart while names are typed.
const int ORIGINAL_NAME_ROLE = Qt::UserRole;
const int ROW_KEY_ROLE = Qt::UserRole + 1;

QDoubleSpinBox *sizeBox(Coord nm, QWidget *parent)
{
    QDoubleSpinBox *box = mmSizeBox(nm, MIN_CLASS_MM, MAX_CLASS_MM, CLASS_MM_STEP, parent);
    box->setFrame(false);
    return box;
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
    tabs->addTab(makeRulesTab(), tr("Rules"));

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
    blockForm->addRow(revisionWord() + colon, m_revisionEdit);
    blockForm->addRow(tr("Date") + colon, m_dateEdit);
    blockForm->addRow(tr("Note") + colon, m_noteEdit);

    // In the order of DesignatorStyle.
    m_styleBox = new QComboBox(tab);
    m_styleBox->setObjectName(QStringLiteral("designatorStyleBox"));
    m_styleBox->addItem(tr("International") + QStringLiteral(" (R, D, Q, U)"));
    m_styleBox->addItem(tr("GOST") + QStringLiteral(" (R, VD, VT, DD)"));
    m_styleBox->setCurrentIndex(int(m_design.project.designatorStyle));
    // In the order of CopperSides.
    m_sidesBox = new QComboBox(tab);
    m_sidesBox->setObjectName(QStringLiteral("copperSidesBox"));
    m_sidesBox->addItem(tr("Both"));
    m_sidesBox->addItem(tr("Top only"));
    m_sidesBox->addItem(tr("Bottom only"));
    m_sidesBox->setCurrentIndex(int(m_design.board.copper));
    QFormLayout *styleForm = new QFormLayout;
    styleForm->addRow(tr("Designator style") + colon, m_styleBox);
    styleForm->addRow(tr("Copper sides") + colon, m_sidesBox);

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
            return mmSizeOf(static_cast<const QDoubleSpinBox *>(m_classTable->cellWidget(row, column)));
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
    bool rulesOk = false;
    QString rulesProblem;
    typedRules(rulesOk, rulesProblem);
    if (!rulesOk) {
        QMessageBox::warning(this, title(), capitalized(rulesProblem) + QLatin1Char('.'));
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
    Board sided = m_design.board;
    sided.copper = CopperSides(m_sidesBox->currentIndex());
    const QSet<ObjectId> off = offSideCopper(sided);
    if (!off.isEmpty()) {
        int tracks = 0;
        for (const Track &track : sided.tracks)
            tracks += off.contains(track.id) ? 1 : 0;
        const int vias = int(off.size()) - tracks;
        QString question = tr("Tracks and vias on copper the board no longer has will be deleted")
                           + QLatin1Char('.');
        if (tracks > 0)
            question += QLatin1Char('\n') + BoardEditor::tracksCountName() + QStringLiteral(": ")
                        + QString::number(tracks);
        if (vias > 0)
            question += QLatin1Char('\n') + BoardEditor::viasCountName() + QStringLiteral(": ")
                        + QString::number(vias);
        question += QLatin1Char('\n') + tr("Delete them") + QLatin1Char('?');
        if (QMessageBox::question(this, title(), question) != QMessageBox::Yes)
            return;
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

QWidget *ProjectPropertiesDialog::makeRulesTab()
{
    QWidget *tab = new QWidget(this);
    const Rules &rules = m_design.project.rules;
    const QString colon = QStringLiteral(":");
    const QString inMm = QStringLiteral(", ") + mmUnit() + colon;
    const auto box = [tab](const char *name, double minMm) {
        QDoubleSpinBox *made = mmSizeBox(0, minMm, MAX_CLASS_MM, CLASS_MM_STEP, tab);
        made->setObjectName(QLatin1String(name));
        return made;
    };
    // In the order of CheckLevel.
    const auto levelBox = [tab](const char *name, CheckLevel level) {
        QComboBox *made = new QComboBox(tab);
        made->setObjectName(QLatin1String(name));
        made->addItems({tr("Error"), tr("Warning"), tr("Off")});
        made->setCurrentIndex(int(level));
        return made;
    };
    // A value and the level of its check on one row.
    const auto pair = [tab](QWidget *value, QWidget *level) {
        QWidget *row = new QWidget(tab);
        QHBoxLayout *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(value);
        layout->addWidget(level);
        layout->addStretch(1);
        return row;
    };

    // In the order of RuleSet.
    m_ruleSetBox = new QComboBox(tab);
    m_ruleSetBox->setObjectName(QStringLiteral("ruleSetBox"));
    m_ruleSetBox->addItems({tr("Manufacturing"), tr("Toner transfer")});
    m_ruleSetBox->setCurrentIndex(int(rules.set));
    m_widthsEdit = new QLineEdit(tab);
    m_widthsEdit->setObjectName(QStringLiteral("trackWidthsEdit"));
    m_defaultWidthBox = new QComboBox(tab);
    m_defaultWidthBox->setObjectName(QStringLiteral("defaultTrackWidthBox"));
    m_clearanceBox = box("clearanceBox", MIN_CLASS_MM);
    m_edgeClearanceBox = box("edgeClearanceBox", 0);
    m_annularRingBox = box("annularRingBox", 0);
    m_viaDrillBox = box("viaDrillBox", MIN_CLASS_MM);
    m_viaSizeBox = box("viaSizeBox", MIN_CLASS_MM);
    m_linkDrillBox = box("linkDrillBox", MIN_CLASS_MM);
    m_linkSizeBox = box("linkSizeBox", MIN_CLASS_MM);
    m_maskBox = box("maskExpansionBox", 0);
    m_viasCoveredBox = new QCheckBox(tr("Vias under the solder mask"), tab);
    m_viasCoveredBox->setObjectName(QStringLiteral("viasCoveredBox"));
    m_drillsEdit = new QLineEdit(tab);
    m_drillsEdit->setObjectName(QStringLiteral("drillsEdit"));
    m_drillsOnlyBox = new QCheckBox(tr("Only these drills"), tab);
    m_drillsOnlyBox->setObjectName(QStringLiteral("drillsOnlyBox"));
    m_clearanceLevelBox = levelBox("clearanceLevelBox", rules.clearanceLevel);
    m_edgeClearanceLevelBox = levelBox("edgeClearanceLevelBox", rules.edgeClearanceLevel);
    m_annularRingLevelBox = levelBox("annularRingLevelBox", rules.annularRingLevel);
    m_minimumWidthLevelBox = levelBox("minimumWidthLevelBox", rules.minimumWidthLevel);
    m_classWidthLevelBox = levelBox("classWidthLevelBox", rules.classWidthLevel);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("Rule set") + colon, m_ruleSetBox);
    form->addRow(tr("Track widths") + inMm, pair(m_widthsEdit, m_minimumWidthLevelBox));
    form->addRow(tr("Default track width") + colon, m_defaultWidthBox);
    form->addRow(tr("Track narrower than its class") + colon, m_classWidthLevelBox);
    form->addRow(tr("Clearance") + inMm, pair(m_clearanceBox, m_clearanceLevelBox));
    form->addRow(tr("Edge clearance") + inMm, pair(m_edgeClearanceBox, m_edgeClearanceLevelBox));
    form->addRow(tr("Annular ring") + inMm, pair(m_annularRingBox, m_annularRingLevelBox));
    form->addRow(tr("Via hole") + inMm, m_viaDrillBox);
    form->addRow(tr("Via diameter") + inMm, m_viaSizeBox);
    form->addRow(tr("Wire link hole") + inMm, m_linkDrillBox);
    form->addRow(tr("Wire link pad") + inMm, m_linkSizeBox);
    form->addRow(tr("Solder mask expansion") + inMm, m_maskBox);
    form->addRow(QString(), m_viasCoveredBox);
    form->addRow(tr("Drills") + inMm, m_drillsEdit);
    form->addRow(QString(), m_drillsOnlyBox);

    QVBoxLayout *layout = new QVBoxLayout(tab);
    layout->addLayout(form);
    layout->addStretch(1);

    showRuleValues(rules);
    // Another set gives its values, to be changed by hand after.
    connect(m_ruleSetBox, &QComboBox::activated, this, [this](int index) {
        showRuleValues(RuleSet(index) == RuleSet::Lut ? lutRules() : productionRules());
    });
    connect(m_widthsEdit, &QLineEdit::editingFinished, this,
            &ProjectPropertiesDialog::updateDefaultWidths);
    return tab;
}

void ProjectPropertiesDialog::showRuleValues(const Rules &rules)
{
    m_widthsEdit->setText(formatMmList(rules.trackWidths));
    m_defaultWidthBox->clear();
    for (const Coord width : rules.trackWidths)
        m_defaultWidthBox->addItem(formatMmShort(width), QVariant::fromValue(qint64(width)));
    m_defaultWidthBox->setCurrentIndex(
        m_defaultWidthBox->findData(QVariant::fromValue(qint64(rules.defaultTrackWidth))));
    setMmSize(m_clearanceBox, rules.clearance);
    setMmSize(m_edgeClearanceBox, rules.edgeClearance);
    setMmSize(m_annularRingBox, rules.annularRing);
    setMmSize(m_viaDrillBox, rules.viaDrill);
    setMmSize(m_viaSizeBox, rules.viaSize);
    setMmSize(m_linkDrillBox, rules.linkDrill);
    setMmSize(m_linkSizeBox, rules.linkSize);
    setMmSize(m_maskBox, rules.maskExpansion);
    m_viasCoveredBox->setChecked(rules.viasCovered);
    m_drillsEdit->setText(formatMmList(rules.drills));
    m_drillsOnlyBox->setChecked(rules.drillsOnly);
}

void ProjectPropertiesDialog::updateDefaultWidths()
{
    QList<Coord> widths;
    if (!parseMmList(m_widthsEdit->text(), widths) || widths.isEmpty())
        return;
    const QVariant chosen = m_defaultWidthBox->currentData();
    m_defaultWidthBox->clear();
    for (const Coord width : std::as_const(widths))
        m_defaultWidthBox->addItem(formatMmShort(width), QVariant::fromValue(qint64(width)));
    const int kept = m_defaultWidthBox->findData(chosen);
    m_defaultWidthBox->setCurrentIndex(kept >= 0 ? kept : 0);
}

Rules ProjectPropertiesDialog::typedRules(bool &ok, QString &problem) const
{
    Rules rules = m_design.project.rules;
    ok = false;
    rules.set = RuleSet(m_ruleSetBox->currentIndex());
    if (!parseMmList(m_widthsEdit->text(), rules.trackWidths) || rules.trackWidths.isEmpty()) {
        problem = tr("Track widths must be lengths in millimetres apart by slashes");
        return rules;
    }
    if (!parseMmList(m_drillsEdit->text(), rules.drills)) {
        problem = tr("Drills must be lengths in millimetres apart by slashes");
        return rules;
    }
    rules.drillsOnly = m_drillsOnlyBox->isChecked();
    if (rules.drillsOnly && rules.drills.isEmpty()) {
        problem = tr("Only these drills needs at least one drill");
        return rules;
    }
    rules.defaultTrackWidth = m_defaultWidthBox->currentData().toLongLong();
    if (!rules.trackWidths.contains(rules.defaultTrackWidth))
        rules.defaultTrackWidth = rules.trackWidths.first();
    rules.clearance = mmSizeOf(m_clearanceBox);
    rules.edgeClearance = mmSizeOf(m_edgeClearanceBox);
    rules.annularRing = mmSizeOf(m_annularRingBox);
    rules.viaDrill = mmSizeOf(m_viaDrillBox);
    rules.viaSize = mmSizeOf(m_viaSizeBox);
    rules.linkDrill = mmSizeOf(m_linkDrillBox);
    rules.linkSize = mmSizeOf(m_linkSizeBox);
    rules.maskExpansion = mmSizeOf(m_maskBox);
    rules.viasCovered = m_viasCoveredBox->isChecked();
    if (rules.viaDrill >= rules.viaSize || rules.linkDrill >= rules.linkSize) {
        problem = JsonReader::tr("the hole must be smaller than the pad");
        return rules;
    }
    rules.clearanceLevel = CheckLevel(m_clearanceLevelBox->currentIndex());
    rules.edgeClearanceLevel = CheckLevel(m_edgeClearanceLevelBox->currentIndex());
    rules.annularRingLevel = CheckLevel(m_annularRingLevelBox->currentIndex());
    rules.minimumWidthLevel = CheckLevel(m_minimumWidthLevelBox->currentIndex());
    rules.classWidthLevel = CheckLevel(m_classWidthLevelBox->currentIndex());
    ok = true;
    return rules;
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

    // Copper the board has no side for any more goes (asked in accept()).
    result.board.copper = CopperSides(m_sidesBox->currentIndex());
    result.board = withoutTracks(result.board, offSideCopper(result.board));

    const DesignatorStyle style = DesignatorStyle(m_styleBox->currentIndex());
    if (style != result.project.designatorStyle) {
        result.schematic = withDesignatorStyle(result.schematic, result.library,
                                               result.project.designatorStyle, style);
        result.board = withLinkDesignatorStyle(result.board, result.schematic,
                                               result.project.designatorStyle, style);
        result.project.designatorStyle = style;
    }

    // Checked in accept().
    bool rulesOk = false;
    QString rulesProblem;
    result.project.rules = typedRules(rulesOk, rulesProblem);

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
