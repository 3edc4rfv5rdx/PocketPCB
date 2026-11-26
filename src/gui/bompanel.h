#pragma once

#include "alerts.h"

#include "bom.h"

#include <QWidget>

class QLabel;
class QTableWidget;
class QTableWidgetItem;

namespace ppcb {

class Document;

// The bill of materials of the project (bomRows(), then the board's wire
// links, linkRows()) as a table, built anew
// after every change: designators, value, how many, footprint and comment,
// a heading line over each group of parts. The rated power most components
// have is said once below the table; another one follows the value. The
// value and the comment are typed in the table, for every component of the
// row as one step. A double click on the designators shows
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
    enum Column { Designators, Value, Quantity, Footprint, Comment, ColumnCount };
    // What a line of the table shows: no row for a group heading.
    static constexpr int NO_ROW = -1;

    void refresh();
    void cellEdited(QTableWidgetItem *item);
    // The row shown on 'line' of the table; null for a group heading.
    const BomRow *rowOfLine(int line) const;
    // The line below the table naming the rated power most components
    // have (commonPowerMw()); empty when none has one.
    QString powerNote() const;
    // The value of a row of wire links: the kind of wire and its length.
    static QString linkValue(const BomRow &row);
    // The heading and the lines as the table shows them, a group heading
    // as a line of one field (see csvOf()).
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
    void tell(const Alert &alert);

    Document *m_document;
    QTableWidget *m_table = nullptr;
    QLabel *m_note = nullptr;
    QLabel *m_message = nullptr;
    QList<BomRow> m_rows;
    // For each line of the table, the index of its row in 'm_rows', or
    // NO_ROW.
    QList<int> m_lineRows;
};

} // namespace ppcb
