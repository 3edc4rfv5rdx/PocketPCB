#include "appsettings.h"
#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    ppcb::AppSettings::initApplication();

    ppcb::MainWindow window;
    window.show();
    return app.exec();
}
