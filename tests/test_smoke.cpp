#include "appsettings.h"
#include "canvasview.h"
#include "designviewitem.h"
#include "document.h"
#include "grid.h"
#include "libraryfile.h"
#include "librarypanel.h"
#include "libraryviewitem.h"
#include "paint.h"
#include "mainwindow.h"
#include "saveprojectdialog.h"
#include "sheeteditor.h"
#include "wiring.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QGraphicsScene>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace ppcb;

namespace {

// How many items of the canvas are on show.
int visibleItems(const MainWindow &window)
{
    int count = 0;
    const QList<QGraphicsItem *> items = window.canvas()->scene()->items();
    for (const QGraphicsItem *item : items) {
        if (item->isVisible())
            ++count;
    }
    return count;
}

QStringList tabTexts(const MainWindow &window)
{
    const QTabBar *tabs = window.findChild<QTabBar *>();
    QStringList texts;
    for (int i = 0; tabs != nullptr && i < tabs->count(); ++i)
        texts.append(tabs->tabText(i));
    return texts;
}

// A project of two sheets, the second one named.
Design twoSheetDesign()
{
    Design design = newDesign(QStringLiteral("amp"));
    Sheet second;
    second.id = newObjectId();
    second.title = QStringLiteral("Питание");
    second.paper = PaperSize::A3;
    design.schematic.sheets.append(second);
    return design;
}

// Presses 'button' in the message box named 'name' as soon as it is shown.
void answerBox(const QString &name, QMessageBox::StandardButton button)
{
    QTimer::singleShot(0, [name, button] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        QVERIFY(box != nullptr);
        QCOMPARE(box->objectName(), name);
        box->button(button)->click();
    });
}

void answerUnsavedChanges(QMessageBox::StandardButton button)
{
    answerBox(QStringLiteral("unsavedChangesBox"), button);
}

Design withTitle(Design design, const QString &title)
{
    design.schematic.sheets[0].title = title;
    return design;
}

} // namespace

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
        QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("PocketPCB"));
        QVERIFY(!QCoreApplication::applicationVersion().isEmpty());
    }

    void gridStepActionsDriveCanvasAndStatusBar()
    {
        MainWindow window;
        QAction *next = window.findChild<QAction *>(QStringLiteral("actionGridNext"));
        QAction *previous = window.findChild<QAction *>(QStringLiteral("actionGridPrevious"));
        QComboBox *stepBox = window.findChild<QComboBox *>(QStringLiteral("gridStepBox"));
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
        // The library file is shown in the place of the project.
        QCOMPARE(visibleItems(window), 1);
        QVERIFY(window.libraryItem()->isVisible());
        QVERIFY(!window.designItem()->isVisible());
        QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("item.json")));

        // The file becomes a footprint: a reload shows it, in the same item.
        LibraryItem footprint;
        footprint.kind = LibraryKind::Footprint;
        QVERIFY2(LibraryFile::write(path, footprint, error), qPrintable(error));
        QVERIFY(window.reloadLibraryFile());
        QVERIFY(window.libraryItem()->content().kind == LibraryKind::Footprint);
        QCOMPARE(visibleItems(window), 1);

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

    void windowStartsWithANewProject()
    {
        MainWindow window;
        QVERIFY(window.document() != nullptr);
        QVERIFY(window.document()->projectPath().isEmpty());
        QVERIFY(window.designItem()->isVisible());
        QCOMPARE(visibleItems(window), 1);
        QCOMPARE(tabTexts(window), QStringList({QStringLiteral("Sheet 1"), QStringLiteral("Board")}));
        QVERIFY2(window.windowTitle().startsWith(QStringLiteral("untitled")),
                 qPrintable(window.windowTitle()));
        QVERIFY(!window.isWindowModified());
        for (const char *name : {"actionNewProject", "actionOpenProject", "actionSaveProject",
                                 "actionSaveProjectAs", "actionUndo", "actionRedo"}) {
            QVERIFY2(window.findChild<QAction *>(QLatin1String(name)) != nullptr, name);
        }
    }

    void projectIsOpenedAndItsPagesAreTabs()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        QString error;
        QVERIFY2(DesignFiles::write(path, twoSheetDesign(), error), qPrintable(error));

        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(path));
        QCOMPARE(window.document()->projectPath(), path);
        QCOMPARE(tabTexts(window), QStringList({QStringLiteral("Sheet 1"), QStringLiteral("Питание"),
                                                QStringLiteral("Board")}));
        QVERIFY2(window.windowTitle().startsWith(QStringLiteral("amp.ppcb")),
                 qPrintable(window.windowTitle()));
        QCOMPARE(window.designItem()->page(), 0);

        // The board is the last page; the tab follows the page.
        QTabBar *tabs = window.findChild<QTabBar *>();
        QVERIFY(tabs != nullptr);
        window.showPage(2);
        QCOMPARE(window.designItem()->page(), window.designItem()->boardPage());
        QCOMPARE(tabs->currentIndex(), 2);
        // And the page follows the tab.
        tabs->setCurrentIndex(1);
        QCOMPARE(window.designItem()->page(), 1);
        // A page that is not there is the nearest one.
        window.showPage(9);
        QCOMPARE(window.designItem()->page(), 2);

        // An opened project goes first in the recent ones, once.
        AppSettings::addRecentProject(dir.filePath(QStringLiteral("other.ppcb")));
        QVERIFY(window.openProject(path));
        QCOMPARE(AppSettings::recentProjects().value(0), QFileInfo(path).absoluteFilePath());
        QCOMPARE(AppSettings::recentProjects().count(QFileInfo(path).absoluteFilePath()), 1);

        // A file that cannot be read changes nothing.
        QVERIFY(!window.openProject(dir.filePath(QStringLiteral("absent.ppcb"))));
        QCOMPARE(window.document()->projectPath(), path);
        QCOMPARE(tabTexts(window).size(), 3);
        QVERIFY(!window.statusBar()->currentMessage().isEmpty());
        // Nor does it go into the recent ones.
        QVERIFY(!AppSettings::recentProjects().contains(
            QFileInfo(dir.filePath(QStringLiteral("absent.ppcb"))).absoluteFilePath()));
        AppSettings::removeRecentProject(path);
        QVERIFY(!AppSettings::recentProjects().contains(QFileInfo(path).absoluteFilePath()));
        AppSettings::clearRecentProjects();
        QVERIFY(AppSettings::recentProjects().isEmpty());

        // A new project drops the file and the second sheet.
        window.newProject();
        QVERIFY(window.document()->projectPath().isEmpty());
        QCOMPARE(tabTexts(window).size(), 2);
        QCOMPARE(window.designItem()->page(), 0);
    }

    void libraryProblemsAreShownOnOpening()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        Design design = twoSheetDesign();
        Component component;
        component.id = newObjectId();
        component.part = QStringLiteral("diode");
        component.designator = QStringLiteral("D1");
        design.schematic.sheets[0].components.append(component);
        QString error;
        QVERIFY2(DesignFiles::write(path, design, error), qPrintable(error));

        // The project opens all the same.
        MainWindow window;
        QVERIFY(window.openProject(path));
        QCOMPARE(window.document()->projectPath(), path);
        QVERIFY2(window.statusBar()->currentMessage().startsWith(
                     QStringLiteral("library/parts/diode.json")),
                 qPrintable(window.statusBar()->currentMessage()));
    }

    void projectIsSavedIntoItsOwnFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("first/first.ppcb"));

        MainWindow window;
        QVERIFY(window.saveProjectAs(dir.path(), QStringLiteral("first")));
        QCOMPARE(window.document()->projectPath(), path);
        QVERIFY(QFile::exists(path));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("first/first.ppsch"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("first/first.ppbrd"))));
        QVERIFY2(window.windowTitle().startsWith(QStringLiteral("first.ppcb")),
                 qPrintable(window.windowTitle()));

        // The folder of the first one is taken.
        QVERIFY(!window.saveProjectAs(dir.path(), QStringLiteral("first")));
        QCOMPARE(window.document()->projectPath(), path);
        QVERIFY(!window.statusBar()->currentMessage().isEmpty());
    }

    void saveProjectDialogShowsThePlaceOrTheProblem()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SaveProjectDialog dialog(QStringLiteral("amp"), dir.path());
        QLineEdit *name = dialog.findChild<QLineEdit *>(QStringLiteral("projectNameEdit"));
        QLabel *preview = dialog.findChild<QLabel *>(QStringLiteral("projectPreviewLabel"));
        QDialogButtonBox *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(name != nullptr && preview != nullptr && buttons != nullptr);
        QPushButton *save = buttons->button(QDialogButtonBox::Save);

        QVERIFY2(preview->text().contains(QDir::toNativeSeparators(
                     Document::projectPathInFolder(dir.path(), QStringLiteral("amp")))),
                 qPrintable(preview->text()));
        QVERIFY(save->isEnabled());

        name->setText(QStringLiteral("a|b"));
        QCOMPARE(preview->text(), Document::nameProblem(QStringLiteral("a|b")));
        QVERIFY(!save->isEnabled());

        // The spaces around the name are not part of it.
        name->setText(QStringLiteral("  amp  "));
        QCOMPARE(dialog.name(), QStringLiteral("amp"));
        QVERIFY(save->isEnabled());
        QCOMPARE(dialog.parentFolder(), dir.path());
    }

    void unsavedChangesAreAskedAbout()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        // Nothing changed: nothing to ask.
        QVERIFY(window.confirmLeavingProject());

        Design design = window.document()->design();
        design.schematic.sheets[0].title = QStringLiteral("Вход");
        window.document()->change(QStringLiteral("Rename sheet"), design);

        // Cancel keeps the project, closing the window too.
        answerUnsavedChanges(QMessageBox::Cancel);
        QVERIFY(!window.confirmLeavingProject());
        answerUnsavedChanges(QMessageBox::Cancel);
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QVERIFY(window.document()->isModified());

        // Discard lets it go unsaved.
        answerUnsavedChanges(QMessageBox::Discard);
        QVERIFY(window.confirmLeavingProject());
        QVERIFY(window.document()->isModified());

        // Save writes it first.
        QVERIFY(window.saveProjectAs(dir.path(), QStringLiteral("amp")));
        window.document()->change(QStringLiteral("Rename sheet"),
                                  withTitle(window.document()->design(), QStringLiteral("B")));
        answerUnsavedChanges(QMessageBox::Save);
        QVERIFY(window.confirmLeavingProject());
        QVERIFY(!window.document()->isModified());
    }

    void autosaveIsOfferedOnOpening()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        QString error;
        // A session that ended without saving, twice.
        const auto crash = [&path] {
            Document crashed;
            QString crashError;
            QVERIFY2(crashed.open(path, crashError), qPrintable(crashError));
            crashed.change(QStringLiteral("A"),
                           withTitle(crashed.design(), QStringLiteral("Lost")));
            QVERIFY2(crashed.autosave(crashError), qPrintable(crashError));
        };
        QVERIFY2(DesignFiles::write(path, twoSheetDesign(), error), qPrintable(error));
        crash();

        MainWindow window;
        answerBox(QStringLiteral("restoreAutosaveBox"), QMessageBox::Yes);
        QVERIFY(window.openProject(path));
        QCOMPARE(window.document()->design().schematic.sheets.at(0).title, QStringLiteral("Lost"));
        QVERIFY(window.document()->isModified());

        // Not saving drops the copy.
        answerUnsavedChanges(QMessageBox::Discard);
        QVERIFY(window.confirmLeavingProject());
        QVERIFY(!Document::hasAutosave(path));

        // Said no: the changes go, and the question with them.
        crash();
        QVERIFY(Document::hasAutosave(path));
        answerBox(QStringLiteral("restoreAutosaveBox"), QMessageBox::No);
        QVERIFY(window.openProject(path));
        QCOMPARE(window.document()->design().schematic.sheets.at(0).title, QString());
        QVERIFY(!Document::hasAutosave(path));
    }

    void librariesAreLoadedOnStart()
    {
        MainWindow window;
        // The user's folders are made, and the built-in library is read.
        const QDir user(AppSettings::userLibraryFolder());
        for (const char *kind : {"symbols", "footprints", "parts"})
            QVERIFY2(user.exists(QLatin1String(kind)), kind);
        QVERIFY(window.library().items.footprints.contains(QStringLiteral("resistor-10.16")));
        // The default is written for the user to see.
        QVERIFY(QSettings(AppSettings::filePath(), QSettings::IniFormat)
                    .contains(QStringLiteral("library/folder")));
    }

    void libraryPanelShowsTheParts()
    {
        // A part of the user's own, under a prefix of its own.
        const QDir user(AppSettings::userLibraryFolder());
        QVERIFY(user.mkpath(QStringLiteral("parts")));
        const QString ownPath = user.filePath(QStringLiteral("parts/test-own.json"));
        const QString secondPath = user.filePath(QStringLiteral("parts/test-second.json"));
        LibraryItem own;
        own.kind = LibraryKind::Part;
        own.part.symbol = QStringLiteral("resistor");
        own.part.prefix = QStringLiteral("TST");
        own.part.prefixGost = QStringLiteral("TSTG");
        own.part.name.insert(QStringLiteral("en"), QStringLiteral("Own part"));
        QString error;
        QVERIFY2(LibraryFile::write(ownPath, own, error), qPrintable(error));

        MainWindow window;
        LibraryPanel *panel = window.libraryPanel();
        QTreeWidget *tree = panel->tree();
        QCOMPARE(tree->topLevelItemCount(), 1);
        // The group of a prefix in the first section, as it is now.
        const auto group = [tree](const QString &prefix) -> QTreeWidgetItem * {
            QTreeWidgetItem *section = tree->topLevelItem(0);
            for (int i = 0; i < section->childCount(); ++i) {
                if (section->child(i)->text(0) == prefix)
                    return section->child(i);
            }
            return nullptr;
        };
        QVERIFY(group(QStringLiteral("TST")) != nullptr);
        QVERIFY(group(QStringLiteral("R")) != nullptr);

        // Choosing a part draws it, with its first footprint.
        tree->setCurrentItem(group(QStringLiteral("R"))->child(0));
        QCOMPARE(panel->currentPart(), QStringLiteral("resistor"));
        QCOMPARE(panel->footprintBox()->currentText(), QStringLiteral("resistor-10.16"));

        // A group the user opened stays open when the library is read again
        // with something new in it, and after a search.
        tree->expandItem(group(QStringLiteral("R")));
        LibraryItem second = own;
        second.part.name.insert(QStringLiteral("en"), QStringLiteral("Second part"));
        QVERIFY2(LibraryFile::write(secondPath, second, error), qPrintable(error));
        window.loadLibrary();
        QCOMPARE(group(QStringLiteral("TST"))->childCount(), 2);
        QVERIFY(group(QStringLiteral("R"))->isExpanded());
        QVERIFY(!group(QStringLiteral("TST"))->isExpanded());
        QCOMPARE(panel->currentPart(), QStringLiteral("resistor"));
        panel->setFilter(QStringLiteral("second"));
        QVERIFY(group(QStringLiteral("TST"))->isExpanded());
        QVERIFY(group(QStringLiteral("R"))->isHidden());
        panel->setFilter(QString());
        QVERIFY(group(QStringLiteral("R"))->isExpanded());
        QVERIFY(!group(QStringLiteral("TST"))->isExpanded());

        // All groups at once, kept across a rebuild.
        panel->setAllGroupsExpanded(true);
        QVERIFY(group(QStringLiteral("TST"))->isExpanded());
        QVERIFY(group(QStringLiteral("R"))->isExpanded());
        window.loadLibrary();
        QVERIFY(group(QStringLiteral("TST"))->isExpanded());
        panel->setAllGroupsExpanded(false);
        QVERIFY(!group(QStringLiteral("R"))->isExpanded());
        // The sections stay open.
        QVERIFY(tree->topLevelItem(0)->isExpanded());

        // The search goes by prefix too.
        panel->setFilter(QStringLiteral("tst"));
        QVERIFY(!group(QStringLiteral("TST"))->isHidden());
        QVERIFY(group(QStringLiteral("R"))->isHidden());
        panel->setFilter(QString());
        QVERIFY(QFile::remove(ownPath));
        QVERIFY(QFile::remove(secondPath));

        // The open project's parts are a section of their own.
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Project"));
    }

    void sheetIsEditedWithTheMouseAndKeys()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        QCOMPARE(editor->sheet(), 0);
        const auto sheet = [&window]() -> const Sheet & {
            return window.document()->design().schematic.sheets.at(0);
        };
        const Component r1 = sheet().components.at(0);
        QCOMPARE(r1.designator, QStringLiteral("R1"));
        const QPointF at = toMm(r1.at);

        // A click takes it, a drag moves it along the grid.
        editor->press(at, false);
        QCOMPARE(editor->selection(), QSet<ObjectId>({r1.id}));
        editor->move(at + QPointF(5.2, 0.4), false);
        QVERIFY(editor->preview() != nullptr);
        editor->release(at + QPointF(5.2, 0.4), false);
        QVERIFY(editor->preview() == nullptr);
        QCOMPARE(sheet().components.at(0).at, r1.at + Point({5080000, 0}));
        QAction *undo = window.findChild<QAction *>(QStringLiteral("actionUndo"));
        QVERIFY2(undo->text().contains(QStringLiteral("R1")), qPrintable(undo->text()));

        // R while dragging turns what is dragged, as often as asked, and the
        // drop makes one step of it all.
        const int steps = window.document()->undoStack()->count();
        const QPointF moved = toMm(sheet().components.at(0).at);
        editor->press(moved, false);
        editor->move(moved + QPointF(5.2, 0), false);
        window.findChild<QAction *>(QStringLiteral("actionRotate"))->trigger();
        window.findChild<QAction *>(QStringLiteral("actionRotate"))->trigger();
        QCOMPARE(editor->preview()->components.at(0).rotation, 180);
        editor->release(moved + QPointF(5.2, 0), false);
        QCOMPARE(sheet().components.at(0).rotation, 180);
        QCOMPARE(sheet().components.at(0).at, r1.at + Point({2 * 5080000, 0}));
        QCOMPARE(window.document()->undoStack()->count(), steps + 1);
        window.document()->undoStack()->undo();

        // Undo puts it back and chooses it again.
        editor->cancel();
        QVERIFY(editor->selection().isEmpty());
        window.document()->undoStack()->undo();
        QCOMPARE(sheet().components.at(0).at, r1.at);
        QCOMPARE(editor->selection(), QSet<ObjectId>({r1.id}));

        // R turns it about its own point.
        window.findChild<QAction *>(QStringLiteral("actionRotate"))->trigger();
        QCOMPARE(sheet().components.at(0).rotation, 90);
        QCOMPARE(sheet().components.at(0).at, r1.at);

        // A rectangle around it takes it, Delete removes it. The rectangle
        // starts below and to the left, where the demo sheet is empty.
        editor->press(at + QPointF(-12, 12), false);
        QVERIFY(editor->selection().isEmpty());
        editor->move(at + QPointF(12, -12), false);
        QVERIFY(!editor->rubberBand().isEmpty());
        editor->release(at + QPointF(12, -12), false);
        QVERIFY(editor->selection().contains(r1.id));
        window.findChild<QAction *>(QStringLiteral("actionDelete"))->trigger();
        QVERIFY(editor->selection().isEmpty());
        for (const Component &component : sheet().components)
            QVERIFY(component.id != r1.id);

        // The board is not a sheet.
        window.showPage(window.designItem()->boardPage());
        QCOMPARE(editor->sheet(), -1);
    }

    void partsArePlacedUntilEsc()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto components = [&window]() {
            return window.document()->design().schematic.sheets.at(0).components;
        };
        const int before = int(components().size());

        // A capacitor the project does not have yet: it is copied in.
        QVERIFY(window.placePart(LibraryPanel::Source::Library, QStringLiteral("capacitor"),
                                 QStringLiteral("capacitor-5.08")));
        QVERIFY(editor->isPlacing());
        QVERIFY(editor->preview() != nullptr);
        editor->hover(Point{mmToNm(50.8), mmToNm(50.8)});
        editor->press(QPointF(50.8, 50.8), false);
        window.findChild<QAction *>(QStringLiteral("actionRotate"))->trigger();
        editor->press(QPointF(60.96, 50.8), false);
        QVERIFY(editor->isPlacing());
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        QVERIFY(!editor->isPlacing());

        QCOMPARE(int(components().size()), before + 2);
        const Component first = components().at(before);
        const Component second = components().at(before + 1);
        QCOMPARE(first.designator, QStringLiteral("C1"));
        QCOMPARE(first.footprint, QStringLiteral("capacitor-5.08"));
        QCOMPARE(first.at, Point({mmToNm(50.8), mmToNm(50.8)}));
        QCOMPARE(first.rotation, 0);
        QCOMPARE(second.designator, QStringLiteral("C2"));
        QCOMPARE(second.rotation, 90);
        QVERIFY(first.id != second.id);
        const Library &project = window.document()->design().library;
        QVERIFY(project.findPart(QStringLiteral("capacitor")) != nullptr);
        QVERIFY(project.findFootprint(QStringLiteral("capacitor-2.54")) != nullptr);

        // Each one is a step of its own.
        window.document()->undoStack()->undo();
        QCOMPARE(int(components().size()), before + 1);

        // Nothing is placed on the board.
        window.showPage(window.designItem()->boardPage());
        QVERIFY(!window.placePart(LibraryPanel::Source::Library, QStringLiteral("capacitor"),
                                  QStringLiteral("capacitor-5.08")));
    }

    void wiresAreDrawnUntilEsc()
    {
        MainWindow window;
        window.showPage(0);
        SheetEditor *editor = window.sheetEditor();
        const auto sheet = [&window]() { return window.document()->design().schematic.sheets.at(0); };
        const auto at = [](double x, double y) { return Point{mmToNm(x), mmToNm(y)}; };
        QAction *deselect = window.findChild<QAction *>(QStringLiteral("actionDeselect"));

        window.findChild<QAction *>(QStringLiteral("actionWire"))->trigger();
        QVERIFY(editor->isWiring());
        QVERIFY(editor->preview() == nullptr);
        // A square corner, along X first; a second click on a point ends the wire.
        editor->press(QPointF(10.16, 10.16), false);
        editor->hover(at(20.32, 15.24));
        QVERIFY(editor->preview() != nullptr);
        editor->press(QPointF(20.32, 15.24), false);
        editor->press(QPointF(20.32, 15.24), false);
        QCOMPARE(sheet().wires.size(), 1);
        QCOMPARE(sheet().wires.at(0).points,
                 QList<Point>({at(10.16, 10.16), at(20.32, 10.16), at(20.32, 15.24)}));

        // A wire that reaches the middle of another ends there by itself,
        // with a dot, and drawing goes on.
        window.findChild<QAction *>(QStringLiteral("actionWireCorner"))->trigger();
        editor->press(QPointF(15.24, 20.32), false);
        editor->press(QPointF(15.24, 10.16), false);
        QCOMPARE(sheet().wires.size(), 2);
        QCOMPARE(junctions(sheet(), window.document()->design().library),
                 QList<Point>({at(15.24, 10.16)}));
        QVERIFY(editor->isWiring());

        // Esc drops the wire begun, a second one ends drawing.
        editor->press(QPointF(30.48, 30.48), false);
        QVERIFY(editor->preview() != nullptr);
        deselect->trigger();
        QVERIFY(editor->isWiring());
        QVERIFY(editor->preview() == nullptr);
        deselect->trigger();
        QVERIFY(!editor->isWiring());
        QCOMPARE(sheet().wires.size(), 2);

        // Each wire is a step.
        window.document()->undoStack()->undo();
        QCOMPARE(sheet().wires.size(), 1);
    }

    void gridStepIsKeptInTheProject()
    {
        MainWindow window;
        QAction *next = window.findChild<QAction *>(QStringLiteral("actionGridNext"));
        next->trigger();
        QCOMPARE(window.document()->design().project.schematicGridStep, mmToNm(5.08));
        // The board has a step of its own.
        window.showPage(window.designItem()->boardPage());
        QCOMPARE(window.canvas()->gridStep(), window.document()->design().project.boardGridStep);
        window.showPage(0);
        QCOMPARE(window.canvas()->gridStep(), mmToNm(5.08));
        // Undo gives the old step back to the canvas.
        window.document()->undoStack()->undo();
        QCOMPARE(window.canvas()->gridStep(), mmToNm(2.54));
    }

    void undoActionsAndTitleFollowTheDocument()
    {
        MainWindow window;
        QAction *undo = window.findChild<QAction *>(QStringLiteral("actionUndo"));
        QAction *redo = window.findChild<QAction *>(QStringLiteral("actionRedo"));
        QVERIFY(undo != nullptr && redo != nullptr);
        QVERIFY(!undo->isEnabled());
        QVERIFY(!redo->isEnabled());

        Design design = window.document()->design();
        design.schematic.sheets[0].title = QStringLiteral("Вход");
        window.document()->change(QStringLiteral("Rename sheet"), design);
        QVERIFY(undo->isEnabled());
        QVERIFY(window.isWindowModified());
        QCOMPARE(tabTexts(window).at(0), QStringLiteral("Вход"));

        undo->trigger();
        QVERIFY(!window.isWindowModified());
        QCOMPARE(tabTexts(window).at(0), QStringLiteral("Sheet 1"));
        QVERIFY(redo->isEnabled());
        redo->trigger();
        QCOMPARE(tabTexts(window).at(0), QStringLiteral("Вход"));
    }

    void libraryFileAndProjectTakeTurnsOnTheCanvas()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("item.json"));
        LibraryItem footprint;
        footprint.kind = LibraryKind::Footprint;
        QString error;
        QVERIFY2(LibraryFile::write(path, footprint, error), qPrintable(error));

        MainWindow window;
        QVERIFY(window.openLibraryFile(path));
        QVERIFY(!window.designItem()->isVisible());

        // Showing a page brings the project back and lets the file go.
        window.showPage(1);
        QVERIFY(window.designItem()->isVisible());
        QVERIFY(!window.libraryItem()->isVisible());
        QCOMPARE(visibleItems(window), 1);
        QVERIFY(window.libraryFilePath().isEmpty());
        QVERIFY(!window.reloadLibraryFile());
        QVERIFY(window.designItem()->isVisible());

        // And the file can be shown again.
        QVERIFY(window.openLibraryFile(path));
        QVERIFY(window.libraryItem()->isVisible());
        QVERIFY(!window.designItem()->isVisible());
        window.newProject();
        QVERIFY(window.designItem()->isVisible());
        QVERIFY(!window.libraryItem()->isVisible());
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
