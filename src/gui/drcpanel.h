#pragma once

#include "drc.h"

#include <QSet>
#include <QWidget>

class QTreeWidget;

namespace ppcb {

class BoardEditor;
class Document;
class RulesCheck;

// The problems the board's rules check finds (BoardEditor::problems()),
// found again after every change while the panel is open and the board
// shown (RulesCheck): the errors, then the warnings. Check finds them
// again and shows the board; choosing one reports it with problemChosen().
class DrcPanel : public QWidget
{
    Q_OBJECT

public:
    DrcPanel(Document *document, BoardEditor *editor, QWidget *parent = nullptr);

    // The panel's title.
    static QString drcTitle();
    // What 'problem' is, in words: "Clearance between track of GND and
    // R1.2: 0.2 mm < 0.3 mm".
    static QString problemText(const DrcProblem &problem);
    // What a problem is about on the board, to choose there: the tracks,
    // vias, wire links, holes, and the placements of pads.
    static QSet<ObjectId> problemObjects(const DrcProblem &problem);

signals:
    void problemChosen(const DrcProblem &problem);
    // Check was pressed.
    void checkRequested();

protected:
    // Shown: the check runs when the board changed meanwhile.
    void showEvent(QShowEvent *event) override;

private:
    void refresh();
    void itemChosen();
    // An item in words, starting low: "track of GND", "R1.2".
    static QString itemText(const DrcItem &item);

    BoardEditor *m_editor;
    RulesCheck *m_check = nullptr;
    QTreeWidget *m_tree = nullptr;
    // The problems shown, by the index kept in each item.
    QList<DrcProblem> m_problems;
};

} // namespace ppcb
