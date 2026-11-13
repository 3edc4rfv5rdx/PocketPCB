#include "appsettings.h"
#include "language.h"
#include "mainwindow.h"
#include "project.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ppcb::AppSettings::initApplication();
    ppcb::installLanguage();

    ppcb::MainWindow window;
    window.show();
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
