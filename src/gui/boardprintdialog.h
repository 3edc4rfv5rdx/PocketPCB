#pragma once

#include "boardprint.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPagedPaintDevice;
class QSpinBox;

namespace ppcb {

class Document;
struct Design;

// The names of the command that prints the board, for its menu item, its
// button and its dialog, and of the button that writes the PDF instead.
QString printBoardTitle();
QString exportBoardPdfTitle();

// Asks what to print of the board of 'document' (BoardPrintDialog) and
// prints it 1:1 on the printer chosen then, or writes it into
// "<project>-copper-top.pdf" (-copper-bottom, -silkscreen-top,
// -silkscreen-bottom) in the project's output folder, on A4 or, for a
// larger board, A3. What to tell the user, empty when cancelled.
QString printBoard(QWidget *parent, const Document &document);

// Paints the copies 'print' asks for on 'device', a page of paper 'page'
// (millimetres, upright) at a time, 1:1 at 'dotsPerMm', each page with the
// caption and the scale ruler. False when the device cannot be painted on.
// 'fits' is set to whether a copy fits on the paper (BoardPrintLayout).
bool paintBoardPages(QPagedPaintDevice &device, const QSizeF &page, double dotsPerMm,
                     const Document &document, const BoardPrint &print, bool &fits);

// The choices of the board's print: the layer (the copper of a side that
// has it, the silkscreen of a side with something on it), mirrored, the
// negative and the centre holes (for the copper), the copies and the
// registration marks, then Print or Export to PDF; beside them a small
// picture of one copy as it will be printed, following the choices.
class BoardPrintDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Output { Printer, Pdf };

    explicit BoardPrintDialog(const Design &design, QWidget *parent = nullptr);

    BoardPrint print() const;
    // What the dialog was closed with, when accepted.
    Output output() const { return m_output; }

private:
    // Draws the picture of a copy again.
    void updatePreview();

    const Design &m_design;
    QLabel *m_preview = nullptr;
    QComboBox *m_layer = nullptr;
    QCheckBox *m_mirrored = nullptr;
    QCheckBox *m_inverted = nullptr;
    QDoubleSpinBox *m_centreHole = nullptr;
    QSpinBox *m_copies = nullptr;
    QCheckBox *m_marks = nullptr;
    Output m_output = Output::Printer;
};

} // namespace ppcb
