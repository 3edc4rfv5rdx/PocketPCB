#pragma once

#include "erc.h"

#include <QWidget>

class QTreeWidget;

namespace ppcb {

class Document;
class SheetEditor;

// The problems the rules check finds (see SheetEditor::problems()), found
// again after every change: under the name of each sheet, then under the
// whole project. Choosing one reports it with problemChosen().
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

private:
    void refresh();
    void itemChosen();

    Document *m_document;
    SheetEditor *m_editor;
    QTreeWidget *m_tree = nullptr;
    // The problems shown, by the index kept in each item.
    QList<ErcProblem> m_problems;
};

} // namespace ppcb
