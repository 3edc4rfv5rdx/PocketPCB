#pragma once

#include <QString>

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
// sheet of that paper fills the page 1:1. What to tell the user, empty when
// the dialog was cancelled.
QString printSchematic(QWidget *parent, const Document &document, int current);

// Writes the sheets into "<project>-schematic.pdf" in the project's output
// folder, each on a page of its own paper, 1:1. With several sheets and one
// of them shown ('current', -1 when none) it asks first whether that one
// or all of them. What to tell the user, empty when cancelled.
QString exportSchematicPdf(QWidget *parent, const Document &document, int current);

} // namespace ppcb
