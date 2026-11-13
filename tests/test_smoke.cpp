#include "appsettings.h"
#include "canvasview.h"
#include "grid.h"
#include "libraryfile.h"
#include "libraryviewitem.h"
#include "mainwindow.h"

#include <QAction>
#include <QComboBox>
#include <QSettings>
#include <QGraphicsScene>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
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

    void libraryFileIsShownAndReloaded()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("item.json"));
        QString error;

        LibraryItem symbol;
        symbol.kind = LibraryKind::Symbol;
        Pin pin;
        pin.number = QStringLiteral("1");
        symbol.symbol.pins.append(pin);
        QVERIFY2(LibraryFile::write(path, symbol, error), qPrintable(error));

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.libraryFilePath().isEmpty());

        QVERIFY(window.openLibraryFile(path));
        QCOMPARE(window.libraryFilePath(), path);
        QVERIFY(window.libraryItem() != nullptr);
        QVERIFY(window.libraryItem()->content().kind == LibraryKind::Symbol);
        QCOMPARE(window.canvas()->scene()->items().size(), 1);
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("item.json")));

        // The file becomes a footprint: a reload shows it, in the same item.
        LibraryItem footprint;
        footprint.kind = LibraryKind::Footprint;
        QVERIFY2(LibraryFile::write(path, footprint, error), qPrintable(error));
        QVERIFY(window.reloadLibraryFile());
        QVERIFY(window.libraryItem()->content().kind == LibraryKind::Footprint);
        QCOMPARE(window.canvas()->scene()->items().size(), 1);

        // A damaged file: the last good drawing stays, the reason is shown.
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("{ damaged");
        file.close();
        QVERIFY(!window.reloadLibraryFile());
        QVERIFY(window.libraryItem()->content().kind == LibraryKind::Footprint);
        QVERIFY(window.statusBar()->currentMessage().startsWith(path));

        // A file that cannot be opened changes nothing.
        QVERIFY(!window.openLibraryFile(dir.filePath(QStringLiteral("absent.json"))));
        QCOMPARE(window.libraryFilePath(), path);
    }

    // Saving the file in another program redraws it without any command.
    void libraryFileIsWatched()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("watched.json"));
        QString error;

        LibraryItem symbol;
        symbol.kind = LibraryKind::Symbol;
        QVERIFY2(LibraryFile::write(path, symbol, error), qPrintable(error));

        MainWindow window;
        QVERIFY(window.openLibraryFile(path));

        // Twice: the watch must survive a save that replaces the file.
        LibraryItem footprint;
        footprint.kind = LibraryKind::Footprint;
        QVERIFY2(LibraryFile::write(path, footprint, error), qPrintable(error));
        QTRY_VERIFY(window.libraryItem()->content().kind == LibraryKind::Footprint);

        QVERIFY2(LibraryFile::write(path, symbol, error), qPrintable(error));
        QTRY_VERIFY(window.libraryItem()->content().kind == LibraryKind::Symbol);
    }

    void partFileIsDescribedInTheStatusBar()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("part.json"));
        LibraryItem part;
        part.kind = LibraryKind::Part;
        part.part.symbol = QStringLiteral("diode");
        part.part.footprints = QStringList({"diode-7.62", "diode-10.16"});
        part.part.prefix = QStringLiteral("D");
        part.part.prefixGost = QStringLiteral("VD");
        QString error;
        QVERIFY2(LibraryFile::write(path, part, error), qPrintable(error));

        MainWindow window;
        QVERIFY(window.openLibraryFile(path));
        const QString message = window.statusBar()->currentMessage();
        QVERIFY2(message.contains(QStringLiteral("diode-7.62, diode-10.16")), qPrintable(message));
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
