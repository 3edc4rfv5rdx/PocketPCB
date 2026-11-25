#pragma once

#include "erc.h"

#include <QWidget>

class QTreeWidget;

namespace ppcb {

class Document;
class RulesCheck;
class SheetEditor;

// The problems the rules check finds (see SheetEditor::problems()), found
// again after every change while the panel is open and a sheet shown
// (RulesCheck): under the name of each sheet, then under the whole
// project. Check finds them again. Choosing one reports it with
// problemChosen().
class ErcPanel : public QWidget
{
    Q_OBJECT

public:
    ErcPanel(Document *document, SheetEditor *editor, QWidget *parent = nullptr);

    // The panel's title.
    static QString ercTitle();
    // What 'problem' is, in words: "Pin R1.2 is not connected".
    static QString problemText(const ErcProblem &problem);

signals:
    // A problem on a sheet was chosen.
    void problemChosen(const ErcProblem &problem);

protected:
    // Shown: the check runs when the schematic changed meanwhile.
    void showEvent(QShowEvent *event) override;

private:
    void refresh();
    void itemChosen();
    // What is chosen on the sheet chooses its problem in the list, the one
    // chosen staying while it still fits; nothing that has one clears the
    // list's choice.
    void followSelection();

    Document *m_document;
    SheetEditor *m_editor;
    RulesCheck *m_check = nullptr;
    QTreeWidget *m_tree = nullptr;
    // The problems shown, by the index kept in each item.
    QList<ErcProblem> m_problems;
    // While a problem chosen in the list is being shown: the sheet's
    // selection passes through states that would choose another one.
    bool m_showing = false;
};

} // namespace ppcb
