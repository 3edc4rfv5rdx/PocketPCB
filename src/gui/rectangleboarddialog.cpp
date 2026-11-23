#include "rectangleboarddialog.h"

#include "boardeditor.h"
#include "format.h"
#include "grid.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>

namespace ppcb {

namespace {

// The sides of the board, in millimetres: the least and the largest typed.
const double MIN_SIDE_MM = 1;
const double MAX_SIDE_MM = 1000;

} // namespace

RectangleBoardDialog::RectangleBoardDialog(Coord width, Coord height, Coord gridStep,
                                           QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(BoardEditor::actionName(BoardEditor::Action::RectangleBoard));
    const double cell = nmToMm(gridStep > 0 ? gridStep : DEFAULT_BOARD_GRID_STEP);
    m_width = mmSizeBox(width, MIN_SIDE_MM, MAX_SIDE_MM, cell, this);
    m_width->setObjectName(QStringLiteral("boardWidth"));
    m_width->setSuffix(QLatin1Char(' ') + mmUnit());
    m_height = mmSizeBox(height, MIN_SIDE_MM, MAX_SIDE_MM, cell, this);
    m_height->setObjectName(QStringLiteral("boardHeight"));
    m_height->setSuffix(QLatin1Char(' ') + mmUnit());

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QFormLayout(this);
    layout->addRow(tr("Width"), m_width);
    layout->addRow(tr("Height"), m_height);
    layout->addRow(buttons);
}

Coord RectangleBoardDialog::boardWidth() const
{
    return mmSizeOf(m_width);
}

Coord RectangleBoardDialog::boardHeight() const
{
    return mmSizeOf(m_height);
}

} // namespace ppcb
