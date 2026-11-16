#pragma once

#include <QWidget>

class QLineEdit;
class QTableWidget;

namespace ppcb {

class Document;
class SheetEditor;
struct Net;

// The table of nets: each net's name, its class and its pins, in the order
// of nets(). The class is chosen from a list, each choice one step; a net of
// wires alone has none to choose. A line above keeps the nets whose name or
// pins hold what is typed there. A row chosen lights its net (see
// SheetEditor::setChosenNet()) and reports it with netChosen().
class NetsPanel : public QWidget
{
    Q_OBJECT

public:
    NetsPanel(Document *document, SheetEditor *editor, QWidget *parent = nullptr);

    // The panel's title.
    static QString netsTitle();

signals:
    // A row was chosen: the name of its net; empty when none is.
    void netChosen(const QString &name);

protected:
    // The wheel over a class list scrolls the table instead of changing it.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // Fills the table from the nets again, the row of the net chosen
    // chosen again; drops the net chosen when it is gone.
    void refresh();
    // Hides the rows the filter leaves out.
    void applyFilter();
    // Sets 'netClass' for the net named 'name' as one step; nothing happens
    // when it is its class already. A net chosen that gets a table name
    // stays chosen under it.
    void chooseClass(const QString &name, const QString &netClass);
    void rowChosen();

    Document *m_document;
    SheetEditor *m_editor;
    QLineEdit *m_filter = nullptr;
    QTableWidget *m_table = nullptr;
};

} // namespace ppcb
