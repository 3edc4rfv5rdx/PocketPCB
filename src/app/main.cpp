#include "appsettings.h"
#include "language.h"
#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ppcb::AppSettings::initApplication();
    ppcb::installLanguage();

    ppcb::MainWindow window;
    window.show();
    // "pocketpcb some.json" opens a library file at once. After show(), so
    // the canvas knows its size and can fit the drawing.
    const QStringList arguments = QCoreApplication::arguments();
    if (arguments.size() > 1)
        window.openLibraryFile(arguments.at(1));
    return app.exec();
}
