#include "bompanel.h"

#include "document.h"
#include "format.h"
#include "project.h"
#include "propertiespanel.h"
#include "sheetedit.h"
#include "sheeteditor.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPdfWriter>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTextDocument>
#include <QVBoxLayout>

namespace ppcb {

namespace {

const char FILE_SUFFIX[] = "-bom";
const char TEXT_EXTENSION[] = "txt";
const char CSV_EXTENSION[] = "csv";
const char PDF_EXTENSION[] = "pdf";
// Spreadsheets take a file starting with it for UTF-8.
const char UTF8_MARK[] = "\xEF\xBB\xBF";
// The margins of a PDF page, in millimetres.
const double PDF_MARGIN_MM = 15;

// The value of a row, shown with the rated power after it when it is not
// the common one; edited without it.
class ValueItem : public QTableWidgetItem
{
public:
    QString power;

    QVariant data(int role) const override
    {
        const QVariant value = QTableWidgetItem::data(role);
        if (role != Qt::DisplayRole || power.isEmpty())
            return value;
        return value.toString() + QLatin1Char(' ') + power;
    }
};

} // namespace

BomPanel::BomPanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
{
    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("bomTable"));
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("Designators"), PropertiesPanel::valueTitle(),
                                        tr("Quantity"), PropertiesPanel::footprintTitle(),
                                        PropertiesPanel::commentTitle()});
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);

    QPushButton *textButton = new QPushButton(tr("Export TXT"), this);
    QPushButton *csvButton = new QPushButton(tr("Export CSV"), this);
    QPushButton *pdfButton = new QPushButton(tr("Export PDF"), this);
    m_note = new QLabel(this);
    m_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_message = new QLabel(this);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(textButton);
    buttons->addWidget(csvButton);
    buttons->addWidget(pdfButton);
    buttons->addWidget(m_message, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addWidget(m_note);
    layout->addLayout(buttons);

    connect(textButton, &QPushButton::clicked, this, &BomPanel::exportText);
    connect(csvButton, &QPushButton::clicked, this, &BomPanel::exportCsv);
    connect(pdfButton, &QPushButton::clicked, this, &BomPanel::exportPdf);
    connect(m_table, &QTableWidget::itemChanged, this, &BomPanel::cellEdited);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int line, int column) {
        const BomRow *row = rowOfLine(line);
        // The board's wire links are on no sheet.
        if (column == Designators && row != nullptr && !row->link)
            emit componentsChosen(row->ids);
    });
    connect(m_document, &Document::designChanged, this, &BomPanel::refresh);
    refresh();
}

QString BomPanel::bomTitle()
{
    return tr("BOM");
}

void BomPanel::refresh()
{
    m_rows = bomRows(m_document->design().schematic, m_document->design().library);
    m_rows.append(linkRows(m_document->design().board));
    m_lineRows.clear();
    for (int r = 0; r < m_rows.size(); ++r) {
        if (r == 0 || m_rows.at(r).group != m_rows.at(r - 1).group)
            m_lineRows.append(NO_ROW);
        m_lineRows.append(r);
    }
    // Filling the table is not an edit.
    const QSignalBlocker blocker(m_table);
    m_table->clearSpans();
    m_table->setRowCount(int(m_lineRows.size()));
    const int commonPower = commonPowerMw(m_document->design().schematic);
    QFont headingFont = m_table->font();
    headingFont.setBold(true);
    for (int line = 0; line < m_lineRows.size(); ++line) {
        const int r = m_lineRows.at(line);
        QStringList fields;
        if (r == NO_ROW) {
            // The heading names the group of the row below it.
            fields.append(partGroupName(m_rows.at(m_lineRows.at(line + 1)).group));
            while (fields.size() < ColumnCount)
                fields.append(QString());
            m_table->setSpan(line, 0, 1, ColumnCount);
        } else {
            const BomRow &row = m_rows.at(r);
            fields = {row.designators.join(QStringLiteral(", ")),
                      row.link ? linkValue(row) : row.value,
                      QString::number(row.designators.size()),
                      row.footprint,
                      row.comment};
        }
        for (int c = 0; c < ColumnCount; ++c) {
            QTableWidgetItem *item = m_table->item(line, c);
            if (item == nullptr) {
                item = c == Value ? new ValueItem : new QTableWidgetItem;
                m_table->setItem(line, c, item);
            }
            if (c == Value) {
                const int power = r == NO_ROW ? 0 : m_rows.at(r).powerMw;
                static_cast<ValueItem *>(item)->power =
                    power != commonPower ? formatPower(power) : QString();
            }
            item->setText(fields.at(c));
            item->setFont(r == NO_ROW ? headingFont : m_table->font());
            Qt::ItemFlags flags = Qt::ItemIsEnabled;
            if (r != NO_ROW) {
                flags |= Qt::ItemIsSelectable;
                // A wire link has no value or comment of its own.
                if ((c == Value || c == Comment) && !m_rows.at(r).link)
                    flags |= Qt::ItemIsEditable;
            }
            item->setFlags(flags);
            item->setTextAlignment(r != NO_ROW && c == Quantity
                                       ? Qt::AlignRight | Qt::AlignVCenter
                                       : Qt::AlignLeft | Qt::AlignVCenter);
        }
    }
    m_note->setText(powerNote());
    m_note->setVisible(!m_note->text().isEmpty());
}

QString BomPanel::linkValue(const BomRow &row)
{
    return (row.insulated ? tr("Insulated wire") : tr("Bare wire")) + QLatin1Char(' ')
           + formatMmShortUnit(row.length);
}

QString BomPanel::powerNote() const
{
    const int common = commonPowerMw(m_document->design().schematic);
    if (common <= 0)
        return QString();
    return tr("Resistor power unless stated") + QStringLiteral(": ") + formatPower(common);
}

const BomRow *BomPanel::rowOfLine(int line) const
{
    if (line < 0 || line >= m_lineRows.size() || m_lineRows.at(line) == NO_ROW)
        return nullptr;
    return &m_rows.at(m_lineRows.at(line));
}

void BomPanel::cellEdited(QTableWidgetItem *item)
{
    const BomRow *shown = rowOfLine(item->row());
    if (shown == nullptr || shown->link
        || (item->column() != Value && item->column() != Comment))
        return;
    // A copy: the change below builds the rows anew.
    const BomRow row = *shown;
    // Without the power the value is shown with.
    const QString text = item->data(Qt::EditRole).toString();
    Design design = m_document->design();
    for (Sheet &sheet : design.schematic.sheets) {
        sheet = item->column() == Value ? withComponentValue(sheet, row.ids, text)
                                        : withComponentComment(sheet, row.ids, text);
    }
    const SheetEditor::Action action =
        item->column() == Value ? SheetEditor::Action::Value : SheetEditor::Action::Comment;
    const QString name = SheetEditor::actionName(action) + QLatin1Char(' ')
                         + row.designators.join(QStringLiteral(", "));
    // No page: undoing it leaves this tab shown.
    m_document->change(name, design, -1, row.ids);
    // Nothing changed: the table shows the row again as it is.
    refresh();
}

QList<QStringList> BomPanel::table() const
{
    QList<QStringList> rows;
    QStringList heading;
    for (int c = 0; c < ColumnCount; ++c)
        heading.append(m_table->horizontalHeaderItem(c)->text());
    rows.append(heading);
    for (int line = 0; line < m_table->rowCount(); ++line) {
        QStringList fields;
        const int columns = rowOfLine(line) == nullptr ? 1 : ColumnCount;
        for (int c = 0; c < columns; ++c)
            fields.append(m_table->item(line, c)->text());
        rows.append(fields);
    }
    return rows;
}

QString BomPanel::outputFile(const QString &extension)
{
    QString problem;
    const QString path = projectOutputFile(*m_document,
                                           QLatin1String(FILE_SUFFIX), extension, problem);
    if (path.isEmpty())
        tell(Alert::error(problem));
    return path;
}

void BomPanel::writeOutput(const QString &extension, const QByteArray &data)
{
    const QString path = outputFile(extension);
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) < 0 || !file.commit()) {
        tell(Alert::error(cannotWriteMessage(path)));
        return;
    }
    tell(Alert::info(writtenMessage(path)));
}

void BomPanel::exportText()
{
    QString text = plainTextOf(table());
    if (!powerNote().isEmpty())
        text += QLatin1Char('\n') + powerNote() + QLatin1Char('\n');
    writeOutput(QLatin1String(TEXT_EXTENSION), text.toUtf8());
}

void BomPanel::exportCsv()
{
    QList<QStringList> rows = table();
    if (!powerNote().isEmpty())
        rows << QStringList() << QStringList(powerNote());
    writeOutput(QLatin1String(CSV_EXTENSION), QByteArray(UTF8_MARK) + csvOf(rows).toUtf8());
}

void BomPanel::exportPdf()
{
    const QString path = outputFile(QLatin1String(PDF_EXTENSION));
    if (path.isEmpty())
        return;
    const QList<QStringList> rows = table();
    QString html = QStringLiteral("<h3>") + bomTitle().toHtmlEscaped() + QStringLiteral(": ")
                   + QFileInfo(m_document->projectPath()).completeBaseName().toHtmlEscaped()
                   + QStringLiteral("</h3><table border=\"1\" cellspacing=\"0\" cellpadding=\"3\">");
    for (int r = 0; r < rows.size(); ++r) {
        html += QStringLiteral("<tr>");
        if (r > 0 && rows.at(r).size() == 1) {
            // A group heading across the table.
            html += QStringLiteral("<td colspan=\"%1\"><b>").arg(ColumnCount)
                    + rows.at(r).first().toHtmlEscaped() + QStringLiteral("</b></td>");
        } else {
            const QString cell = r == 0 ? QStringLiteral("th") : QStringLiteral("td");
            for (const QString &field : rows.at(r))
                html += QLatin1Char('<') + cell + QLatin1Char('>') + field.toHtmlEscaped()
                        + QStringLiteral("</") + cell + QLatin1Char('>');
        }
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
    if (!powerNote().isEmpty())
        html += QStringLiteral("<p>") + powerNote().toHtmlEscaped() + QStringLiteral("</p>");

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(PDF_MARGIN_MM, PDF_MARGIN_MM, PDF_MARGIN_MM, PDF_MARGIN_MM),
                          QPageLayout::Millimeter);
    writer.setTitle(bomTitle());
    QTextDocument text;
    text.setHtml(html);
    text.print(&writer);
    if (!QFileInfo(path).isFile()) {
        tell(Alert::error(cannotWriteMessage(path)));
        return;
    }
    tell(Alert::info(writtenMessage(path)));
}

void BomPanel::tell(const Alert &alert)
{
    Alerts::show(this, alert, [this](const QString &text) { m_message->setText(text); });
}

} // namespace ppcb
