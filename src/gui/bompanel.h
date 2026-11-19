#pragma once

#include "bom.h"

#include <QWidget>

class QLabel;
class QTableWidget;
class QTableWidgetItem;

namespace ppcb {

class Document;

// The bill of materials of the project (bomRows()) as a table, built anew
// after every change: designators, value, footprint, rated power, comment
// and how many. The value and the comment are typed in the table, for every
// component of the row as one step. A double click on the designators shows
// those components. The table is written into the project's output folder
// as plain text, comma separated values or PDF.
class BomPanel : public QWidget
{
    Q_OBJECT

public:
    explicit BomPanel(Document *document, QWidget *parent = nullptr);

    // The name of the panel and of its tab.
    static QString bomTitle();

signals:
    // The designators of a row were double clicked: these components.
    void componentsChosen(const QSet<ObjectId> &ids);

private:
    enum Column { Designators, Value, Footprint, Power, Comment, Quantity, ColumnCount };

    void refresh();
    void cellEdited(QTableWidgetItem *item);
    // The heading and the rows as the table shows them.
    QList<QStringList> table() const;
    void exportText();
    void exportCsv();
    void exportPdf();
    // The file "<project>-bom.<extension>" in the output folder, which is
    // made; empty, with a word below the table, for a project not saved
    // yet or a folder that cannot be made.
    QString outputFile(const QString &extension);
    // Writes 'data' there and says where, or that it could not.
    void writeOutput(const QString &extension, const QByteArray &data);
    void tell(const QString &message);

    Document *m_document;
    QTableWidget *m_table = nullptr;
    QLabel *m_message = nullptr;
    QList<BomRow> m_rows;
};

} // namespace ppcb
