#include "updatepartsdialog.h"

#include "format.h"
#include "language.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QVBoxLayout>

namespace ppcb {

namespace {

// The least width of the dialog, in pixels.
const int MIN_DIALOG_WIDTH = 520;

} // namespace

UpdatePartsDialog::UpdatePartsDialog(const QList<PartDifference> &differences,
                                     const Library &library, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(title());

    QLabel *intro = new QLabel(tr("These parts of the project differ from the library") + QLatin1Char(':'),
                               this);
    intro->setWordWrap(true);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("updatePartsList"));
    const QStringList languages = nameLanguages();
    for (const PartDifference &difference : differences) {
        QStringList what;
        if (difference.fields)
            what.append(tr("part fields"));
        if (difference.drawing)
            what.append(symbolWord());
        if (difference.pins)
            what.append(tr("pins moved, wires stay where they are"));
        if (difference.footprints)
            what.append(footprintsWord());
        const Part *part = library.findPart(difference.part);
        const QString name =
            part != nullptr ? part->displayName(languages, difference.part) : difference.part;
        QListWidgetItem *item =
            new QListWidgetItem(name + QStringLiteral(": ") + what.join(QStringLiteral(", ")), m_list);
        item->setData(Qt::UserRole, difference.part);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_list, 1);
    layout->addWidget(buttons);
    resize(qMax(sizeHint().width(), MIN_DIALOG_WIDTH), sizeHint().height());
}

QString UpdatePartsDialog::title()
{
    return tr("Update parts from library");
}

QString UpdatePartsDialog::nothingToUpdate()
{
    return tr("Every part of the project is as in the library");
}

QStringList UpdatePartsDialog::chosenParts() const
{
    QStringList parts;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem *item = m_list->item(i);
        if (item->checkState() == Qt::Checked)
            parts.append(item->data(Qt::UserRole).toString());
    }
    return parts;
}

} // namespace ppcb
