#pragma once

#include "netlist.h"
#include "project.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace ppcb {

// The properties of a project, edited on a copy: on the General tab the
// fields of the title block, the style of designators and the sides of
// the board with copper; on the Net
// classes tab the classes, added, renamed, changed and deleted, and which
// of them is the default class and the power class; on the Rules tab the
// rules of the board and the level of each check (Rules).
class ProjectPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectPropertiesDialog(const Design &design, QWidget *parent = nullptr);

    // The name of the dialog and of the step it makes.
    static QString title();

    // The design given with what was typed and chosen, the fields without
    // the spaces around them: a new designator style renames the components
    // (withDesignatorStyle()), the net classes go everywhere they are given
    // (withNetClasses()).
    Design design() const;

    // Closes the dialog only when the net classes are right, and when the
    // user agrees that what gave a class deleted gives another now.
    void accept() override;

private:
    enum ClassColumn { ClassName, TrackWidth, Clearance, ViaDrill, ViaSize, ClassColumnCount };

    QWidget *makeGeneralTab();
    QWidget *makeNetClassesTab();
    QWidget *makeRulesTab();
    // The rules tab shows the values of 'rules', its levels kept.
    void showRuleValues(const Rules &rules);
    // The default track width offers the widths typed, the one chosen kept
    // while it is among them.
    void updateDefaultWidths();
    // The rules as the tab has them; 'ok' false, with what is wrong in
    // 'problem', when they are not right.
    Rules typedRules(bool &ok, QString &problem) const;
    // A row of the table for 'netClass', which had the name 'original'
    // (empty for a class added here).
    void appendClassRow(const QString &original, const NetClass &netClass);
    // A new row with the values of the default class and a name no class has.
    void addClass();
    void deleteClass();
    // The lists of the default and the power class take the names in the
    // table, keeping what they show while it is there.
    void updateClassChoices();
    QList<NetClassEdit> netClassEdits() const;
    // What is wrong with the classes in the table, for the user; empty when
    // nothing is.
    QString classProblem() const;

    Design m_design;
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_authorEdit = nullptr;
    QLineEdit *m_organizationEdit = nullptr;
    QLineEdit *m_contactsEdit = nullptr;
    QLineEdit *m_revisionEdit = nullptr;
    QLineEdit *m_dateEdit = nullptr;
    QLineEdit *m_noteEdit = nullptr;
    QComboBox *m_styleBox = nullptr;
    QComboBox *m_sidesBox = nullptr;
    QTableWidget *m_classTable = nullptr;
    QPushButton *m_deleteClassButton = nullptr;
    QComboBox *m_defaultClassBox = nullptr;
    QComboBox *m_powerClassBox = nullptr;
    QComboBox *m_ruleSetBox = nullptr;
    QLineEdit *m_widthsEdit = nullptr;
    QComboBox *m_defaultWidthBox = nullptr;
    QDoubleSpinBox *m_clearanceBox = nullptr;
    QDoubleSpinBox *m_edgeClearanceBox = nullptr;
    QDoubleSpinBox *m_annularRingBox = nullptr;
    QDoubleSpinBox *m_viaDrillBox = nullptr;
    QDoubleSpinBox *m_viaSizeBox = nullptr;
    QDoubleSpinBox *m_linkDrillBox = nullptr;
    QDoubleSpinBox *m_linkSizeBox = nullptr;
    QDoubleSpinBox *m_maskBox = nullptr;
    QCheckBox *m_viasCoveredBox = nullptr;
    QLineEdit *m_drillsEdit = nullptr;
    QCheckBox *m_drillsOnlyBox = nullptr;
    QComboBox *m_clearanceLevelBox = nullptr;
    QComboBox *m_edgeClearanceLevelBox = nullptr;
    QComboBox *m_annularRingLevelBox = nullptr;
    QComboBox *m_minimumWidthLevelBox = nullptr;
    QComboBox *m_classWidthLevelBox = nullptr;
    // The key the next row of the table of classes gets (ROW_KEY_ROLE).
    int m_nextRowKey = 0;
};

} // namespace ppcb
