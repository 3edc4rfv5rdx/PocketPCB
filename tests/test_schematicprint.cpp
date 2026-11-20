#include "document.h"
#include "format.h"
#include "objectid.h"
#include "schematicprint.h"

#include <QPrinter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

class TestSchematicPrint : public QObject
{
    Q_OBJECT

private slots:
    // Each sheet on a page of its own paper: A4 lying, then A3 upright.
    void pdfHasAPageOfItsPaperForEachSheet()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString project = dir.filePath(QStringLiteral("amp.ppcb"));
        Document document;
        Design design = document.design();
        Sheet tall;
        tall.id = newObjectId();
        tall.paper = PaperSize::A3;
        tall.portrait = true;
        design.schematic.sheets.append(tall);
        document.change(QStringLiteral("Sheet"), design);
        QString error;
        QVERIFY2(document.saveAs(project, error), qPrintable(error));

        const QString pdf = DesignFiles::outputPath(project) + QStringLiteral("/amp-schematic.pdf");
        QCOMPARE(exportSchematicPdf(nullptr, document, -1), writtenMessage(pdf));
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromLatin1(file.readAll());
        QVERIFY(text.startsWith(QStringLiteral("%PDF")));
        // The page boxes, in points: 297 x 210 mm, then 297 x 420 mm.
        const QRegularExpression box(QStringLiteral("/MediaBox \\[0 0 ([0-9.]+) ([0-9.]+)\\]"));
        QList<QSizeF> pages;
        for (auto it = box.globalMatch(text); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            pages.append(QSizeF(m.captured(1).toDouble(), m.captured(2).toDouble()));
        }
        QCOMPARE(pages.size(), 2);
        const double pointsPerMm = 72 / 25.4;
        QVERIFY(qAbs(pages.at(0).width() - 297 * pointsPerMm) < 1);
        QVERIFY(qAbs(pages.at(0).height() - 210 * pointsPerMm) < 1);
        QVERIFY(qAbs(pages.at(1).width() - 297 * pointsPerMm) < 1);
        QVERIFY(qAbs(pages.at(1).height() - 420 * pointsPerMm) < 1);
    }

    // A lying sheet sent to a printer set to lie is printed on an upright
    // page, turned on it, so that no driver cuts its sides off.
    void printingUsesUprightPages()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Document document;
        QVERIFY(!document.design().schematic.sheets.first().portrait);
        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(dir.filePath(QStringLiteral("print.pdf")));
        printer.setPageSize(QPageSize(QPageSize::A4));
        printer.setPageOrientation(QPageLayout::Landscape);
        QVERIFY(printSheets(printer, document, 0, 0));
        QFile file(dir.filePath(QStringLiteral("print.pdf")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromLatin1(file.readAll());
        const QRegularExpressionMatch m =
            QRegularExpression(QStringLiteral("/MediaBox \\[0 0 ([0-9.]+) ([0-9.]+)\\]")).match(text);
        QVERIFY(m.hasMatch());
        QVERIFY(m.captured(1).toDouble() < m.captured(2).toDouble());
    }

    // A project never saved has no output folder: nothing is written.
    void pdfNeedsASavedProject()
    {
        Document document;
        const QString message = exportSchematicPdf(nullptr, document, -1);
        QVERIFY(!message.isEmpty());
        QVERIFY(!message.startsWith(writtenMessage(QString()).trimmed()));
    }
};

QTEST_MAIN(TestSchematicPrint)
#include "test_schematicprint.moc"
