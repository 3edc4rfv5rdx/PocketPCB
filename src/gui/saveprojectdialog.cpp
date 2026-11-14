#include "saveprojectdialog.h"

#include "document.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace ppcb {

SaveProjectDialog::SaveProjectDialog(const QString &name, const QString &parentFolder,
                                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Save project as"));

    m_nameEdit = new QLineEdit(name, this);
    m_nameEdit->setObjectName(QStringLiteral("projectNameEdit"));
    m_folderEdit = new QLineEdit(QDir::toNativeSeparators(parentFolder), this);
    m_folderEdit->setObjectName(QStringLiteral("projectFolderEdit"));
    QPushButton *browseButton = new QPushButton(tr("Browse") + QStringLiteral("…"), this);

    QHBoxLayout *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(browseButton);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("Name") + QLatin1Char(':'), m_nameEdit);
    form->addRow(tr("Folder") + QLatin1Char(':'), folderRow);

    m_previewLabel = new QLabel(this);
    m_previewLabel->setObjectName(QStringLiteral("projectPreviewLabel"));
    m_previewLabel->setWordWrap(true);
    m_previewLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_previewLabel);
    layout->addStretch(1);
    layout->addWidget(m_buttons);

    connect(m_nameEdit, &QLineEdit::textChanged, this, &SaveProjectDialog::updatePreview);
    connect(m_folderEdit, &QLineEdit::textChanged, this, &SaveProjectDialog::updatePreview);
    connect(browseButton, &QPushButton::clicked, this, &SaveProjectDialog::chooseFolder);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(qMax(sizeHint().width(), 480), sizeHint().height());
    m_nameEdit->selectAll();
    updatePreview();
}

QString SaveProjectDialog::name() const
{
    return m_nameEdit->text().trimmed();
}

QString SaveProjectDialog::parentFolder() const
{
    return QDir::fromNativeSeparators(m_folderEdit->text().trimmed());
}

void SaveProjectDialog::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Folder for the project"),
                                                             parentFolder());
    if (!folder.isEmpty())
        m_folderEdit->setText(QDir::toNativeSeparators(folder));
}

void SaveProjectDialog::updatePreview()
{
    // The disk may change while the dialog is open: saving checks again.
    const QString problem = Document::newFolderProblem(parentFolder(), name());
    if (problem.isEmpty()) {
        m_previewLabel->setText(
            tr("Will be made") + QStringLiteral(":\n")
            + QDir::toNativeSeparators(Document::projectPathInFolder(parentFolder(), name())));
    } else {
        m_previewLabel->setText(problem);
    }
    m_buttons->button(QDialogButtonBox::Save)->setEnabled(problem.isEmpty());
}

} // namespace ppcb
