#include "appsettings.h"
#include "canvasview.h"
#include "grid.h"
#include "mainwindow.h"

#include <QAction>
#include <QComboBox>
#include <QSettings>
#include <QStandardPaths>
#include <QtTest>

using namespace ppcb;

// The program starts and its main window works, without a display.
class TestSmoke : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        AppSettings::initApplication();
        QLocale::setDefault(QLocale::c());
    }

    void init()
    {
        // clear(), not deleting the file: QSettings caches files inside a process.
        QSettings(AppSettings::filePath(), QSettings::IniFormat).clear();
    }

    void windowOpens()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.canvas() != nullptr);
        QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("pocketpcb"));
        QVERIFY(!QCoreApplication::applicationVersion().isEmpty());
    }

    void gridStepActionsDriveCanvasAndStatusBar()
    {
        MainWindow window;
        QAction *next = window.findChild<QAction *>(QStringLiteral("actionGridNext"));
        QAction *previous = window.findChild<QAction *>(QStringLiteral("actionGridPrevious"));
        QComboBox *stepBox = window.findChild<QComboBox *>();
        QVERIFY(next != nullptr);
        QVERIFY(previous != nullptr);
        QVERIFY(stepBox != nullptr);
        QCOMPARE(stepBox->currentText(), QStringLiteral("2.54 mm"));

        next->trigger();
        QCOMPARE(window.canvas()->gridStep(), mmToNm(5.08));
        QCOMPARE(stepBox->currentText(), QStringLiteral("5.08 mm"));

        previous->trigger();
        previous->trigger();
        QCOMPARE(window.canvas()->gridStep(), mmToNm(1.27));
        QCOMPARE(stepBox->currentText(), QStringLiteral("1.27 mm"));
    }

    void closingSavesTheWindowLayout()
    {
        QVERIFY(AppSettings::windowGeometry().isEmpty());
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.close());
        QVERIFY(!AppSettings::windowGeometry().isEmpty());
        QVERIFY(!AppSettings::windowState().isEmpty());
    }

    void savedViewSettingsAreApplied()
    {
        AppSettings::setGridStyle(GridStyle::Dots);
        AppSettings::setCursorStyle(CursorStyle::FullCross);
        AppSettings::setZoomCenterAndWarp(false);

        MainWindow window;
        QCOMPARE(window.canvas()->gridStyle(), GridStyle::Dots);
        QCOMPARE(window.canvas()->cursorStyle(), CursorStyle::FullCross);
        QCOMPARE(window.canvas()->zoomCenterAndWarp(), false);
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
