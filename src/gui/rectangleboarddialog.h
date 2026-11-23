#pragma once

#include "units.h"

#include <QDialog>

class QDoubleSpinBox;

namespace ppcb {

// Asks for the width and the height of a rectangular board, in millimetres;
// the arrows go a cell of the grid at a time, and any size may be typed.
// The name of the dialog is that of its command
// (BoardEditor::actionName(BoardEditor::Action::RectangleBoard)).
class RectangleBoardDialog : public QDialog
{
    Q_OBJECT

public:
    // Starts with 'width' by 'height', the arrows stepping by 'gridStep'.
    RectangleBoardDialog(Coord width, Coord height, Coord gridStep, QWidget *parent = nullptr);

    Coord boardWidth() const;
    Coord boardHeight() const;

private:
    QDoubleSpinBox *m_width = nullptr;
    QDoubleSpinBox *m_height = nullptr;
};

} // namespace ppcb
