#pragma once

#include <QDialog>

class QDialogButtonBox;
class QLabel;
class QLineEdit;

namespace ppcb {

// Asks for the name of a project and the folder to make its own folder in,
// and shows the project file that will be made, or why it cannot be.
class SaveProjectDialog : public QDialog
{
    Q_OBJECT

public:
    SaveProjectDialog(const QString &name, const QString &parentFolder, QWidget *parent = nullptr);

    // The name without the spaces around it.
    QString name() const;
    QString parentFolder() const;

private:
    void chooseFolder();
    // Shows the path or the problem and lets the dialog be accepted only
    // when there is no problem.
    void updatePreview();

    QLineEdit *m_nameEdit = nullptr;
    QLineEdit *m_folderEdit = nullptr;
    QLabel *m_previewLabel = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace ppcb
