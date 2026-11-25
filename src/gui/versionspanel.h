#pragma once

#include <QWidget>

class QAction;
class QPushButton;
class QTreeWidget;

namespace ppcb {

class Document;

// The versions of the project (projectVersions()), the newest first: the
// number, when it was made, the revision and what changed. Read again when
// the panel is shown, a project is opened or saved, and on refresh().
// "Restore this version", a button over the list and in the menu of a
// version, reports the version chosen with restoreRequested().
class VersionsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit VersionsPanel(Document *document, QWidget *parent = nullptr);

    // The panel's title.
    static QString versionsTitle();
    // Reads the versions again, while the panel is shown.
    void refresh();
    // The command that puts a version back, for its button and its box.
    static QString restoreTitle();

signals:
    void restoreRequested(int number, const QString &path);

protected:
    void showEvent(QShowEvent *event) override;

private:
    // The restore command follows the version chosen: only a readable one.
    void updateRestore();
    void restoreChosen();

    Document *m_document;
    QTreeWidget *m_tree = nullptr;
    QAction *m_restoreAction = nullptr;
    QPushButton *m_restoreButton = nullptr;
};

} // namespace ppcb
