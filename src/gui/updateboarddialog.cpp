#include "updateboarddialog.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace ppcb {

namespace {

// The least width of the dialog, in pixels.
const int MIN_DIALOG_WIDTH = 520;

} // namespace

UpdateBoardDialog::UpdateBoardDialog(const QList<BoardChange> &changes, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title());

    QLabel *intro = new QLabel(tr("These changes will be made on the board") + QLatin1Char(':'), this);
    intro->setWordWrap(true);
    QListWidget *list = new QListWidget(this);
    list->setObjectName(QStringLiteral("updateBoardList"));
    for (const BoardChange &change : changes)
        list->addItem(changeText(change));

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(list, 1);
    layout->addWidget(buttons);
    resize(qMax(sizeHint().width(), MIN_DIALOG_WIDTH), sizeHint().height());
}

QString UpdateBoardDialog::title()
{
    return tr("Update board from schematic");
}

QString UpdateBoardDialog::nothingToUpdate()
{
    return tr("The board matches the schematic");
}

QString UpdateBoardDialog::changeText(const BoardChange &change)
{
    const QString arrow = QStringLiteral(" → ");
    const QString of = change.designator + QStringLiteral(": ");
    switch (change.kind) {
    case BoardChangeKind::Add:
        return tr("Add") + QLatin1Char(' ') + change.designator + QStringLiteral(" (")
               + change.after + QLatin1Char(')');
    case BoardChangeKind::Remove:
        return tr("Remove") + QLatin1Char(' ') + change.designator;
    case BoardChangeKind::Designator:
        return of + tr("designator") + QLatin1Char(' ') + change.before + arrow + change.after;
    case BoardChangeKind::Value:
        return of + tr("value") + QLatin1Char(' ') + change.before + arrow + change.after;
    case BoardChangeKind::Footprint:
        return of + tr("footprint") + QLatin1Char(' ') + change.before + arrow + change.after;
    case BoardChangeKind::Nets:
        return of + tr("nets of the pads");
    case BoardChangeKind::MissingFootprint:
        return of + tr("kept as it is") + QStringLiteral(", ")
               + tr("the project library has no footprint") + QLatin1Char(' ') + change.after;
    case BoardChangeKind::MissingFootprintNotAdded:
        return of + tr("not added") + QStringLiteral(", ")
               + tr("the project library has no footprint") + QLatin1Char(' ') + change.after;
    }
    return QString();
}

} // namespace ppcb
