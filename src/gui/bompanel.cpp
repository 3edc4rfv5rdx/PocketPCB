#include "bompanel.h"

#include "document.h"
#include "format.h"
#include "project.h"
#include "propertiespanel.h"
#include "sheetedit.h"
#include "sheeteditor.h"

#include <QDir>
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

} // namespace

BomPanel::BomPanel(Document *document, QWidget *parent)
    : QWidget(parent)
    , m_document(document)
{
    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("bomTable"));
    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("Designators"), PropertiesPanel::valueTitle(),
                                        PropertiesPanel::footprintTitle(),
                                        PropertiesPanel::powerTitle(),
                                        PropertiesPanel::commentTitle(), tr("Quantity")});
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                             | QAbstractItemView::AnyKeyPressed);

    QPushButton *textButton = new QPushButton(tr("Export TXT"), this);
    QPushButton *csvButton = new QPushButton(tr("Export CSV"), this);
    QPushButton *pdfButton = new QPushButton(tr("Export PDF"), this);
    m_message = new QLabel(this);
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);

    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(textButton);
    buttons->addWidget(csvButton);
    buttons->addWidget(pdfButton);
    buttons->addWidget(m_message, 1);
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addLayout(buttons);

    connect(textButton, &QPushButton::clicked, this, &BomPanel::exportText);
    connect(csvButton, &QPushButton::clicked, this, &BomPanel::exportCsv);
    connect(pdfButton, &QPushButton::clicked, this, &BomPanel::exportPdf);
    connect(m_table, &QTableWidget::itemChanged, this, &BomPanel::cellEdited);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if (column == Designators && row >= 0 && row < m_rows.size())
            emit componentsChosen(m_rows.at(row).ids);
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
    m_rows = bomRows(m_document->design().schematic);
    // Filling the table is not an edit.
    const QSignalBlocker blocker(m_table);
    m_table->setRowCount(int(m_rows.size()));
    for (int r = 0; r < m_rows.size(); ++r) {
        const BomRow &row = m_rows.at(r);
        const QStringList fields = {row.designators.join(QStringLiteral(", ")),
                                    row.value,
                                    row.footprint,
                                    formatPower(row.powerMw),
                                    row.comment,
                                    QString::number(row.designators.size())};
        for (int c = 0; c < ColumnCount; ++c) {
            QTableWidgetItem *item = m_table->item(r, c);
            if (item == nullptr) {
                item = new QTableWidgetItem;
                m_table->setItem(r, c, item);
            }
            item->setText(fields.at(c));
            Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
            if (c == Value || c == Comment)
                flags |= Qt::ItemIsEditable;
            item->setFlags(flags);
            if (c == Quantity)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
}

void BomPanel::cellEdited(QTableWidgetItem *item)
{
    const int r = item->row();
    if (r < 0 || r >= m_rows.size() || (item->column() != Value && item->column() != Comment))
        return;
    // A copy: the change below builds the rows anew.
    const BomRow row = m_rows.at(r);
    Design design = m_document->design();
    for (Sheet &sheet : design.schematic.sheets) {
        sheet = item->column() == Value ? withComponentValue(sheet, row.ids, item->text())
                                        : withComponentComment(sheet, row.ids, item->text());
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
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QStringList fields;
        for (int c = 0; c < ColumnCount; ++c)
            fields.append(m_table->item(r, c)->text());
        rows.append(fields);
    }
    return rows;
}

QString BomPanel::outputFile(const QString &extension)
{
    const QString project = m_document->projectPath();
    if (project.isEmpty()) {
        tell(tr("Save the project first"));
        return QString();
    }
    const QString folder = DesignFiles::outputPath(project);
    if (!QDir().mkpath(folder)) {
        tell(tr("Cannot make the folder") + QLatin1Char(' ') + QDir::toNativeSeparators(folder));
        return QString();
    }
    return QDir(folder).filePath(QFileInfo(project).completeBaseName()
                                 + QLatin1String(FILE_SUFFIX) + QLatin1Char('.') + extension);
}

void BomPanel::writeOutput(const QString &extension, const QByteArray &data)
{
    const QString path = outputFile(extension);
    if (path.isEmpty())
        return;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) < 0 || !file.commit()) {
        tell(tr("Cannot write") + QLatin1Char(' ') + QDir::toNativeSeparators(path));
        return;
    }
    tell(tr("Written") + QLatin1Char(' ') + QDir::toNativeSeparators(path));
}

void BomPanel::exportText()
{
    writeOutput(QLatin1String(TEXT_EXTENSION), plainTextOf(table()).toUtf8());
}

void BomPanel::exportCsv()
{
    writeOutput(QLatin1String(CSV_EXTENSION),
                QByteArray(UTF8_MARK) + csvOf(table()).toUtf8());
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
        const QString cell = r == 0 ? QStringLiteral("th") : QStringLiteral("td");
        html += QStringLiteral("<tr>");
        for (const QString &field : rows.at(r))
            html += QLatin1Char('<') + cell + QLatin1Char('>') + field.toHtmlEscaped()
                    + QStringLiteral("</") + cell + QLatin1Char('>');
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");

    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(PDF_MARGIN_MM, PDF_MARGIN_MM, PDF_MARGIN_MM, PDF_MARGIN_MM),
                          QPageLayout::Millimeter);
    writer.setTitle(bomTitle());
    QTextDocument text;
    text.setHtml(html);
    text.print(&writer);
    if (!QFileInfo(path).isFile()) {
        tell(tr("Cannot write") + QLatin1Char(' ') + QDir::toNativeSeparators(path));
        return;
    }
    tell(tr("Written") + QLatin1Char(' ') + QDir::toNativeSeparators(path));
}

void BomPanel::tell(const QString &message)
{
    m_message->setText(message);
}

} // namespace ppcb
