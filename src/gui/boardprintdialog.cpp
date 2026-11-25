#include "boardprintdialog.h"

#include "document.h"
#include "format.h"
#include "paint.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPageLayout>
#include <QPainter>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSaveFile>
#include <QSpinBox>

namespace ppcb {

namespace {

const char PDF_EXTENSION[] = "pdf";
const double MM_PER_INCH = 25.4;
// The centre holes typed, in mm: from none to this, by this step.
const double MAX_CENTRE_HOLE_MM = 3;
const double CENTRE_HOLE_STEP_MM = 0.1;
const int MAX_COPIES = 100;
// Room around the outline for its line, in mm.
const double OUTLINE_PRINT_ROOM = 0.5;

// The paper's size in millimetres, upright.
QSizeF uprightSize(QPageSize::PageSizeId id)
{
    return QPageSize(id).size(QPageSize::Millimeter);
}

// "Top copper", "Bottom silkscreen" and so on.
QString layerName(BoardPrintLayer layer, BoardSide side)
{
    const bool top = side == BoardSide::Top;
    if (layer == BoardPrintLayer::Copper)
        return top ? BoardPrintDialog::tr("Top copper") : BoardPrintDialog::tr("Bottom copper");
    return top ? BoardPrintDialog::tr("Top silkscreen") : BoardPrintDialog::tr("Bottom silkscreen");
}

// What the top of each page says: the project, the layer and how it is
// printed.
QString caption(const Document &document, const BoardPrint &print)
{
    QString text = projectName(document.projectPath()) + QStringLiteral("   ")
                   + layerName(print.layer, print.side);
    if (print.mirrored)
        text += QStringLiteral(", ") + BoardPrintDialog::tr("mirrored");
    if (print.inverted && print.layer == BoardPrintLayer::Copper)
        text += QStringLiteral(", ") + BoardPrintDialog::tr("negative");
    return text + QStringLiteral(", 1:1");
}

// What a copy of the board holds, in the board's millimetres: the outline,
// with room for its line, or everything laid when there is none yet. Parts
// put aside off the board are not printed.
QRectF printArea(const Design &design)
{
    const Board &board = design.board;
    if (board.outline.isEmpty())
        return boardBounds(board, design.library);
    const double room = OUTLINE_PRINT_ROOM;
    return outlinePath(board.outline).boundingRect().adjusted(-room, -room, room, room);
}

// The combo box's data for a layer of a side, and back.
int layerChoice(BoardPrintLayer layer, BoardSide side)
{
    return int(layer) * 2 + int(side);
}

BoardPrintLayer choiceLayer(int choice)
{
    return BoardPrintLayer(choice / 2);
}

BoardSide choiceSide(int choice)
{
    return BoardSide(choice % 2);
}

} // namespace

QString printBoardTitle()
{
    return BoardPrintDialog::tr("Print board");
}

QString exportBoardPdfTitle()
{
    return BoardPrintDialog::tr("Export board to PDF");
}

bool paintBoardPages(QPagedPaintDevice &device, const QSizeF &page, double dotsPerMm,
                     const Document &document, const BoardPrint &print, bool &fits)
{
    const Design &design = document.design();
    const Board &board = design.board;
    const QRectF area = printArea(design);
    const QRectF copy = boardPrintCopyRect(area, print.registrationMarks);
    const BoardPrintLayout layout = boardPrintLayout(copy.size(), page, print.copies);
    fits = layout.fits;
    const QString title = caption(document, print);
    const QString rulerLabel =
        QString::number(SCALE_RULER_LENGTH) + QLatin1Char(' ') + mmUnit();

    QPainter painter;
    if (!painter.begin(&device))
        return false;
    for (int index = 0; index < layout.pages.size(); ++index) {
        if (index > 0 && !device.newPage())
            return false;
        painter.resetTransform();
        painter.scale(dotsPerMm, dotsPerMm);
        paintBoardPrintHeader(painter, QPointF(BOARD_PRINT_MARGIN, BOARD_PRINT_MARGIN), title,
                              rulerLabel);
        for (const QPointF &corner : layout.pages.at(index)) {
            painter.save();
            // Read from the last: the board mirrored about the middle of
            // the copy, its corner taken to the origin, turned a quarter
            // clockwise when the layout says so, then to its place.
            painter.translate(corner);
            if (layout.turned) {
                painter.translate(copy.height(), 0);
                painter.rotate(90);
            }
            painter.translate(-copy.topLeft());
            if (print.mirrored) {
                painter.translate(copy.left() + copy.right(), 0);
                painter.scale(-1, 1);
            }
            painter.setClipRect(copy);
            paintBoardLayerPrint(painter, board, design.library, print, area);
            painter.restore();
        }
    }
    return painter.end();
}

QString printBoard(QWidget *parent, const Document &document)
{
    BoardPrintDialog choices(document.design().board, parent);
    if (choices.exec() != QDialog::Accepted)
        return QString();
    const BoardPrint print = choices.print();
    const QString notFitting = BoardPrintDialog::tr("The board does not fit on the paper");
    bool fits = true;

    if (choices.output() == BoardPrintDialog::Output::Printer) {
        QPrinter printer(QPrinter::HighResolution);
        printer.setDocName(projectName(document.projectPath()));
        printer.setFullPage(true);
        QPrintDialog dialog(&printer, parent);
        dialog.setWindowTitle(printBoardTitle());
        dialog.setOption(QAbstractPrintDialog::PrintPageRange, false);
        dialog.setOption(QAbstractPrintDialog::PrintCurrentPage, false);
        dialog.setOption(QAbstractPrintDialog::PrintSelection, false);
        if (dialog.exec() != QDialog::Accepted)
            return QString();
        // Black from the black toner alone, not mixed of colours.
        printer.setColorMode(QPrinter::GrayScale);
        // Upright pages, as for the schematic: the layout turns the copies
        // instead of a driver that might not turn the page.
        printer.setFullPage(true);
        printer.setPageOrientation(QPageLayout::Portrait);
        const QSizeF page = printer.pageLayout().fullRect(QPageLayout::Millimeter).size();
        if (!paintBoardPages(printer, page, printer.resolution() / MM_PER_INCH, document, print,
                             fits))
            return cannotPrintMessage();
        return fits ? sentToPrinterMessage() : notFitting;
    }

    const QString suffix =
        (print.layer == BoardPrintLayer::Copper ? QStringLiteral("-copper")
                                                : QStringLiteral("-silkscreen"))
        + (print.side == BoardSide::Top ? QStringLiteral("-top") : QStringLiteral("-bottom"));
    QString problem;
    const QString path = projectOutputFile(document.projectPath(), suffix,
                                           QLatin1String(PDF_EXTENSION), problem);
    if (path.isEmpty())
        return problem;
    // A4, or A3 when a copy does not fit on A4.
    const Design &design = document.design();
    const QRectF copy = boardPrintCopyRect(printArea(design), print.registrationMarks);
    QPageSize::PageSizeId paper = QPageSize::A4;
    if (!boardPrintLayout(copy.size(), uprightSize(paper), 1).fits)
        paper = QPageSize::A3;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return cannotWriteMessage(path);
    {
        QPdfWriter writer(&file);
        writer.setTitle(projectName(document.projectPath()));
        writer.setCreator(QCoreApplication::applicationName());
        writer.setPageLayout(QPageLayout(QPageSize(paper), QPageLayout::Portrait, QMarginsF()));
        if (!paintBoardPages(writer, uprightSize(paper), writer.resolution() / MM_PER_INCH,
                             document, print, fits))
            return cannotWriteMessage(path);
    }
    if (!file.commit())
        return cannotWriteMessage(path);
    return fits ? writtenMessage(path) : writtenMessage(path) + QStringLiteral(". ") + notFitting;
}

BoardPrintDialog::BoardPrintDialog(const Board &board, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(printBoardTitle());
    const BoardPrint defaults;

    // The copper of the sides that have it, the silkscreen of those with
    // something on it.
    m_layer = new QComboBox(this);
    m_layer->setObjectName(QStringLiteral("printLayer"));
    for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
        if (copperOn(board.copper, side))
            m_layer->addItem(layerName(BoardPrintLayer::Copper, side),
                             layerChoice(BoardPrintLayer::Copper, side));
    }
    for (const BoardSide side : {BoardSide::Top, BoardSide::Bottom}) {
        if (silkscreenUsed(board, side))
            m_layer->addItem(layerName(BoardPrintLayer::Silkscreen, side),
                             layerChoice(BoardPrintLayer::Silkscreen, side));
    }
    m_mirrored = new QCheckBox(this);
    m_mirrored->setObjectName(QStringLiteral("printMirrored"));
    m_inverted = new QCheckBox(this);
    m_inverted->setObjectName(QStringLiteral("printInverted"));
    m_inverted->setChecked(defaults.inverted);
    m_centreHole = mmSizeBox(defaults.centreHole, 0, MAX_CENTRE_HOLE_MM, CENTRE_HOLE_STEP_MM, this);
    m_centreHole->setObjectName(QStringLiteral("printCentreHole"));
    m_centreHole->setSuffix(QLatin1Char(' ') + mmUnit());
    m_centreHole->setSpecialValueText(tr("None"));
    // A side starts mirrored or not; the negative and the centre holes are
    // for the copper.
    const auto layerChosen = [this] {
        const int choice = m_layer->currentData().toInt();
        m_mirrored->setChecked(printMirroredByDefault(choiceSide(choice)));
        const bool copper = choiceLayer(choice) == BoardPrintLayer::Copper;
        m_inverted->setEnabled(copper);
        m_centreHole->setEnabled(copper);
    };
    layerChosen();
    connect(m_layer, &QComboBox::currentIndexChanged, this, layerChosen);

    m_copies = new QSpinBox(this);
    m_copies->setObjectName(QStringLiteral("printCopies"));
    m_copies->setRange(1, MAX_COPIES);
    m_copies->setValue(defaults.copies);
    m_marks = new QCheckBox(this);
    m_marks->setObjectName(QStringLiteral("printMarks"));
    m_marks->setChecked(defaults.registrationMarks);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *printButton =
        buttons->addButton(tr("Print") + QStringLiteral("…"), QDialogButtonBox::AcceptRole);
    printButton->setObjectName(QStringLiteral("printToPrinter"));
    printButton->setDefault(true);
    QPushButton *pdfButton = buttons->addButton(exportBoardPdfTitle(), QDialogButtonBox::AcceptRole);
    pdfButton->setObjectName(QStringLiteral("printToPdf"));
    connect(printButton, &QPushButton::clicked, this, [this] {
        m_output = Output::Printer;
        accept();
    });
    connect(pdfButton, &QPushButton::clicked, this, [this] {
        m_output = Output::Pdf;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QFormLayout(this);
    layout->addRow(m_layer);
    layout->addRow(capitalized(tr("mirrored")), m_mirrored);
    layout->addRow(tr("Negative for photoresist"), m_inverted);
    layout->addRow(tr("Centre holes"), m_centreHole);
    layout->addRow(tr("Copies"), m_copies);
    layout->addRow(tr("Registration marks"), m_marks);
    layout->addRow(buttons);
}

BoardPrint BoardPrintDialog::print() const
{
    const int choice = m_layer->currentData().toInt();
    BoardPrint print;
    print.layer = choiceLayer(choice);
    print.side = choiceSide(choice);
    print.mirrored = m_mirrored->isChecked();
    print.inverted = print.layer == BoardPrintLayer::Copper && m_inverted->isChecked();
    print.centreHole = mmSizeOf(m_centreHole);
    print.copies = m_copies->value();
    print.registrationMarks = m_marks->isChecked();
    return print;
}

} // namespace ppcb
