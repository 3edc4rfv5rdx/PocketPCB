#pragma once

#include "project.h"

#include <QDialog>

class QComboBox;
class QLineEdit;

namespace ppcb {

// The properties of a project, edited on a copy: on the General tab the
// fields of the title block and the style of designators.
class ProjectPropertiesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectPropertiesDialog(const Project &project, QWidget *parent = nullptr);

    // The name of the dialog and of the step it makes.
    static QString title();

    // The project given, with what was typed and chosen; the fields without
    // the spaces around them.
    Project project() const;

private:
    Project m_project;
    QLineEdit *m_titleEdit = nullptr;
    QLineEdit *m_authorEdit = nullptr;
    QLineEdit *m_organizationEdit = nullptr;
    QLineEdit *m_contactsEdit = nullptr;
    QLineEdit *m_revisionEdit = nullptr;
    QLineEdit *m_dateEdit = nullptr;
    QLineEdit *m_noteEdit = nullptr;
    QComboBox *m_styleBox = nullptr;
};

} // namespace ppcb
