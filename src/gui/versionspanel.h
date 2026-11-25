#pragma once

#include <QWidget>

class QTreeWidget;

namespace ppcb {

class Document;

// The versions of the project (projectVersions()), the newest first: the
// number, when it was made, the revision and what changed. Read again when
// the panel is shown, a project is opened or saved, and on refresh().
class VersionsPanel : public QWidget
{
    Q_OBJECT

public:
    explicit VersionsPanel(Document *document, QWidget *parent = nullptr);

    // The panel's title.
    static QString versionsTitle();
    // Reads the versions again, while the panel is shown.
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

private:
    Document *m_document;
    QTreeWidget *m_tree = nullptr;
};

} // namespace ppcb
