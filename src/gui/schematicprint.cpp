#include "schematicprint.h"

#include "document.h"
#include "format.h"
#include "paint.h"
#include "sheetedit.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSaveFile>

namespace ppcb {

namespace {

const char FILE_SUFFIX[] = "-schematic";
const char PDF_EXTENSION[] = "pdf";
const double MM_PER_INCH = 25.4;

// The page a sheet is laid out for: its paper, upright or lying.
QPageLayout sheetLayout(const Sheet &sheet)
{
    const QPageSize size(sheet.paper == PaperSize::A3 ? QPageSize::A3 : QPageSize::A4);
    return QPageLayout(size, sheet.portrait ? QPageLayout::Portrait : QPageLayout::Landscape,
                       QMarginsF());
}

// Sheet 'index' of 'document' drawn on the page of 'layout', whose
// device has 'resolution' dots per inch and its origin at the paper's
// corner.
void paintPage(QPainter &painter, const Document &document, int index, const QPageLayout &layout,
               int resolution)
{
    const Design &design = document.design();
    const Sheet &sheet = design.schematic.sheets.at(index);
    const double dotsPerMm = resolution / MM_PER_INCH;
    const QSizeF page = layout.fullRect(QPageLayout::Millimeter).size();
    painter.save();
    painter.setTransform(paperOnPage(paperRect(sheet), page)
                         * QTransform::fromScale(dotsPerMm, dotsPerMm));
    const MonochromeInk ink;
    paintSheet(painter, design.schematic, design.library, index, document.titleBlockShown(),
               powerShown(design.project, design.schematic));
    painter.restore();
}

} // namespace

QString printSchematicTitle()
{
    return QCoreApplication::translate("ppcb::SchematicPrint", "Print schematic");
}

QString exportSchematicPdfTitle()
{
    return QCoreApplication::translate("ppcb::SchematicPrint", "Export schematic to PDF");
}

QString printSchematic(QWidget *parent, const Document &document, int current)
{
    const QList<Sheet> &sheets = document.design().schematic.sheets;
    if (sheets.isEmpty())
        return QString();
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(projectName(document.projectPath()));
    printer.setFullPage(true);
    QPrintDialog dialog(&printer, parent);
    dialog.setWindowTitle(printSchematicTitle());
    dialog.setMinMax(1, int(sheets.size()));
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, true);
    dialog.setOption(QAbstractPrintDialog::PrintCurrentPage, current >= 0);
    dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
    if (dialog.exec() != QDialog::Accepted)
        return QString();

    int from = 0;
    int to = int(sheets.size()) - 1;
    if (printer.printRange() == QPrinter::CurrentPage && current >= 0) {
        from = current;
        to = current;
    } else if (printer.printRange() == QPrinter::PageRange && printer.fromPage() > 0) {
        from = qBound(0, printer.fromPage() - 1, to);
        to = qBound(from, printer.toPage() - 1, to);
    }
    if (!printSheets(printer, document, from, to))
        return cannotPrintMessage();
    return sentToPrinterMessage();
}

bool printSheets(QPrinter &printer, const Document &document, int from, int to)
{
    // Upright pages whatever the dialog says: a driver may print a page
    // asked to lie as an upright one without turning it, cutting its sides
    // off. A sheet that lies is turned on the page here instead.
    printer.setFullPage(true);
    printer.setPageOrientation(QPageLayout::Portrait);
    const QPageLayout layout = printer.pageLayout();
    QPainter painter;
    if (!painter.begin(&printer))
        return false;
    for (int index = from; index <= to; ++index) {
        if (index > from)
            printer.newPage();
        paintPage(painter, document, index, layout, printer.resolution());
    }
    return painter.end();
}

QString exportSchematicPdf(QWidget *parent, const Document &document, int current)
{
    const QList<Sheet> &sheets = document.design().schematic.sheets;
    if (sheets.isEmpty())
        return QString();
    int from = 0;
    int to = int(sheets.size()) - 1;
    if (sheets.size() > 1 && current >= 0) {
        QMessageBox ask(QMessageBox::Question, exportSchematicPdfTitle(),
                        QCoreApplication::translate("ppcb::SchematicPrint",
                                                    "Which sheets to export")
                            + QLatin1Char('?'),
                        QMessageBox::Cancel, parent);
        QPushButton *one = ask.addButton(
            QCoreApplication::translate("ppcb::SchematicPrint", "Current sheet"),
            QMessageBox::AcceptRole);
        QPushButton *all = ask.addButton(
            QCoreApplication::translate("ppcb::SchematicPrint", "All sheets"),
            QMessageBox::AcceptRole);
        ask.setDefaultButton(all);
        ask.exec();
        if (ask.clickedButton() == one) {
            from = current;
            to = current;
        } else if (ask.clickedButton() != all) {
            return QString();
        }
    }

    QString problem;
    const QString path = projectOutputFile(document.projectPath(), QLatin1String(FILE_SUFFIX),
                                           QLatin1String(PDF_EXTENSION), problem);
    if (path.isEmpty())
        return problem;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return cannotWriteMessage(path);
    {
        QPdfWriter writer(&file);
        writer.setTitle(projectName(document.projectPath()));
        writer.setCreator(QCoreApplication::applicationName());
        QPageLayout layout = sheetLayout(sheets.at(from));
        writer.setPageLayout(layout);
        QPainter painter;
        if (!painter.begin(&writer))
            return cannotWriteMessage(path);
        for (int index = from; index <= to; ++index) {
            if (index > from) {
                // Each page has the paper of its sheet.
                layout = sheetLayout(sheets.at(index));
                writer.setPageLayout(layout);
                writer.newPage();
            }
            paintPage(painter, document, index, layout, writer.resolution());
        }
        painter.end();
    }
    if (!file.commit())
        return cannotWriteMessage(path);
    return writtenMessage(path);
}

} // namespace ppcb
