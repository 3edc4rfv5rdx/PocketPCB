#include "appsettings.h"
#include "language.h"
#include "mainwindow.h"
#include "project.h"

#include <QApplication>
#include <QDir>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ppcb::AppSettings::initApplication();
    // Before any setting is read: they may be in the folders of an earlier
    // version still.
    QString movingError;
    const ppcb::AppSettings::OldFolders moved = ppcb::AppSettings::moveOldFolders(movingError);
    ppcb::installLanguage();
    // The common folder of the projects is there from the start.
    QDir().mkpath(ppcb::AppSettings::projectsFolder());

    ppcb::MainWindow window;
    window.show();
    if (moved != ppcb::AppSettings::OldFolders::None)
        window.alert(ppcb::AppSettings::oldFoldersAlert(moved, movingError));
    // "pocketpcb some.ppcb" opens a project at once, "pocketpcb some.json"
    // a library file. After show(), so the canvas knows its size and can fit
    // the drawing.
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1) {
        const QString path = arguments.at(1);
        if (path.endsWith(QLatin1String(ppcb::FILE_EXT_PROJECT), Qt::CaseInsensitive))
            window.openProject(path);
        else
            window.openLibraryFile(path);
    }
    return app.exec();
}
