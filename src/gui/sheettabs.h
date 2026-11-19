#pragma once

#include "objectid.h"
#include "schematic.h"

#include <QWidget>

class QLineEdit;
class QMenu;
class QTabBar;

namespace ppcb {

class Document;
class SheetEditor;

// The pages of the project as tabs over the canvas: the sheets, then the
// board, then the BOM, which is no page of the canvas; the board and the BOM
// stay last. A sheet is added with the "+" button beside the
// tabs or from the menu of a tab, renamed with a double click on its tab or
// from that menu, where its paper is chosen and it is deleted too (the same
// menu is View > Sheet for the sheet shown); dragging its tab moves it. Each
// of these is one step of the history.
//
// A tab knows its page by the sheet's id, not by its place, so that a tab
// dragged halfway still stands for its sheet.
class SheetTabs : public QWidget
{
    Q_OBJECT

public:
    SheetTabs(Document *document, SheetEditor *editor, QWidget *parent = nullptr);

    // Fills 'menu', emptied first, with what can be done to page 'page': a
    // sheet is added after it, renamed, given its paper or deleted; for the
    // board or no page, a sheet is added at the end.
    void fillMenu(QMenu *menu, int page);

    // Brings the tabs in line with the sheets of the project.
    void updatePages();
    // Makes the tab of page 'page' (the sheets, then the board) the current
    // one, without pageChosen().
    void setCurrentPage(int page);
    // Makes the tab of the BOM the current one, without bomChosen().
    void setBomCurrent();

signals:
    // A tab was chosen or clicked, or a sheet added: 'page' is to be shown.
    void pageChosen(int page);
    // The tab of the BOM was chosen or clicked.
    void bomChosen();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The page a tab stands for; -1 for a tab of a sheet that is gone and
    // for the tab of the BOM.
    int pageOfTab(int tab) const;
    bool isBomTab(int tab) const;
    int tabOfPage(int page) const;
    // The place of the sheet 'id' among the sheets; -1 when it is gone.
    int sheetIndex(const ObjectId &id) const;

    void showMenu(const QPoint &pos);
    // Adds an empty sheet at 'index' among the sheets and shows it.
    void addSheet(int index);
    void deleteSheet(const ObjectId &id);
    void setPaper(const ObjectId &id, PaperSize paper);
    // A line over tab 'tab' for the sheet's name: Enter or a click elsewhere
    // keeps it, Esc drops it. An empty name gives the sheet back its number.
    void startRename(int tab);
    void finishRename(bool keep);
    // After a drag of a tab: the sheets take the order of their tabs, and
    // the board's tab goes back to the end.
    void applyTabOrder();
    // Ends what the sheet editor has in hand, such as a text being typed,
    // while the sheets are still where it began it.
    void settleEditor();
    // "<action> (<sheet>)": the name of a step.
    static QString stepName(const QString &action, const QString &sheet);

    Document *m_document;
    SheetEditor *m_editor;
    QTabBar *m_bar;
    // The line of a sheet being renamed, and the sheet; null when none is.
    QLineEdit *m_renameEdit = nullptr;
    ObjectId m_renamedSheet;
};

} // namespace ppcb
