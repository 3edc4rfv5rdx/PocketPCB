#include "projectpropertiesdialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ppcb {

namespace {

// The least width of the dialog, in pixels.
const int MIN_DIALOG_WIDTH = 480;

} // namespace

ProjectPropertiesDialog::ProjectPropertiesDialog(const Project &project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(title());

    const TitleBlock &block = project.titleBlock;
    const auto lineEdit = [this](const QString &text, const char *name) {
        QLineEdit *edit = new QLineEdit(text, this);
        edit->setObjectName(QLatin1String(name));
        return edit;
    };
    m_titleEdit = lineEdit(block.title, "projectTitleEdit");
    m_authorEdit = lineEdit(block.author, "projectAuthorEdit");
    m_organizationEdit = lineEdit(block.organization, "projectOrganizationEdit");
    m_contactsEdit = lineEdit(block.contacts, "projectContactsEdit");
    m_revisionEdit = lineEdit(block.revision, "projectRevisionEdit");
    m_dateEdit = lineEdit(block.date, "projectDateEdit");
    m_noteEdit = lineEdit(block.note, "projectNoteEdit");

    const QString colon = QStringLiteral(":");
    QGroupBox *blockBox = new QGroupBox(tr("Title block"), this);
    QFormLayout *blockForm = new QFormLayout(blockBox);
    blockForm->addRow(tr("Title") + colon, m_titleEdit);
    blockForm->addRow(tr("Author") + colon, m_authorEdit);
    blockForm->addRow(tr("Organization") + colon, m_organizationEdit);
    blockForm->addRow(tr("Contacts") + colon, m_contactsEdit);
    blockForm->addRow(tr("Revision") + colon, m_revisionEdit);
    blockForm->addRow(tr("Date") + colon, m_dateEdit);
    blockForm->addRow(tr("Note") + colon, m_noteEdit);

    // In the order of DesignatorStyle.
    m_styleBox = new QComboBox(this);
    m_styleBox->setObjectName(QStringLiteral("designatorStyleBox"));
    m_styleBox->addItem(tr("International") + QStringLiteral(" (R, D, Q, U)"));
    m_styleBox->addItem(tr("GOST") + QStringLiteral(" (R, VD, VT, DD)"));
    m_styleBox->setCurrentIndex(int(project.designatorStyle));
    QFormLayout *styleForm = new QFormLayout;
    styleForm->addRow(tr("Designator style") + colon, m_styleBox);

    QWidget *general = new QWidget(this);
    QVBoxLayout *generalLayout = new QVBoxLayout(general);
    generalLayout->addWidget(blockBox);
    generalLayout->addLayout(styleForm);
    generalLayout->addStretch(1);

    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(general, tr("General"));

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    resize(qMax(sizeHint().width(), MIN_DIALOG_WIDTH), sizeHint().height());
}

QString ProjectPropertiesDialog::title()
{
    return tr("Project properties");
}

Project ProjectPropertiesDialog::project() const
{
    Project result = m_project;
    TitleBlock &block = result.titleBlock;
    block.title = m_titleEdit->text().trimmed();
    block.author = m_authorEdit->text().trimmed();
    block.organization = m_organizationEdit->text().trimmed();
    block.contacts = m_contactsEdit->text().trimmed();
    block.revision = m_revisionEdit->text().trimmed();
    block.date = m_dateEdit->text().trimmed();
    block.note = m_noteEdit->text().trimmed();
    result.designatorStyle = DesignatorStyle(m_styleBox->currentIndex());
    return result;
}

} // namespace ppcb
