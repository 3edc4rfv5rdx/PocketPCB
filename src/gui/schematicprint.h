#pragma once

#include "alerts.h"

#include <QString>

class QPrinter;
class QWidget;

namespace ppcb {

class Document;

// The schematic on paper and in a PDF: black on white, a sheet on a page,
// with its frame and title block, without the selection or the marks of the
// rules check.

// The names of the two commands, for their menu items, buttons and dialogs.
QString printSchematicTitle();
QString exportSchematicPdfTitle();

// Asks with the printer dialog for the printer and the sheets: all of them,
// a range, or the sheet 'current' (-1 when no sheet is shown), and prints
// each on a page of the printer's paper, turned and sized to fit it; a
// sheet of that paper fills the page 1:1. What to tell the user: an error
// when it cannot be printed; none when the dialog was cancelled.
Alert printSchematic(QWidget *parent, const Document &document, int current);

// Prints the sheets 'from' to 'to' of 'document' (indices, both in range)
// on 'printer' as the dialog set it up, on upright pages of its paper
// whatever way it was asked to lay them, each sheet turned and sized to
// fit. False when the printer cannot be printed on.
bool printSheets(QPrinter &printer, const Document &document, int from, int to);

// Writes the sheets into "<project>-schematic.pdf" in the project's output
// folder, each on a page of its own paper, 1:1. With several sheets and one
// of them shown ('current', -1 when none) it asks first whether that one
// or all of them. What to tell the user: an error when it is not written;
// none when cancelled.
Alert exportSchematicPdf(QWidget *parent, const Document &document, int current);

} // namespace ppcb
