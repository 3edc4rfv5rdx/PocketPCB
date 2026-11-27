#include "alerts.h"
#include "appsettings.h"
#include "boardeditor.h"
#include "canvasview.h"
#include "designviewitem.h"
#include "document.h"
#include "footprinteditor.h"
#include "symboleditor.h"
#include "libraryeditor.h"
#include "parteditor.h"
#include "format.h"
#include "gerberexport.h"
#include "projectversions.h"
#include "grid.h"
#include "libraryfile.h"
#include "librarypanel.h"
#include "libraryviewitem.h"
#include "outline.h"
#include "paint.h"
#include "mainwindow.h"
#include "saveprojectdialog.h"
#include "sheeteditor.h"
#include "sheettabs.h"
#include "wiring.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
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
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

#include <algorithm>

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
        // No one answers a box here: errors and warnings go to the status bar.
        Alerts::setBoxesShown(false);
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
        QVERIFY(window.statusText().contains(QStringLiteral("item.json")));

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
        QVERIFY(window.statusText().startsWith(path));

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

        // The BOM is a panel, hidden at first; showing it leaves the tabs
        // and the page shown as they are.
        QDockWidget *bom = window.findChild<QDockWidget *>(QStringLiteral("bomDock"));
        QVERIFY(bom != nullptr && bom->isHidden());
        bom->toggleViewAction()->trigger();
        QVERIFY(!bom->isHidden());
        QCOMPARE(tabTexts(window), QStringList({QStringLiteral("Sheet 1"), QStringLiteral("Board")}));
        QCOMPARE(window.findChild<QTabBar *>()->currentIndex(), 0);
        QVERIFY(window.designItem()->isVisible());
        QVERIFY2(window.windowTitle().startsWith(QStringLiteral("untitled")),
                 qPrintable(window.windowTitle()));
        QVERIFY(!window.isWindowModified());
        for (const char *name : {"actionNewProject", "actionOpenProject", "actionSaveProject",
                                 "actionSaveProjectAs", "actionUndo", "actionRedo"}) {
            QVERIFY2(window.findChild<QAction *>(QLatin1String(name)) != nullptr, name);
        }
    }

    void pagesOpenAsTheyWereLastLookedAt()
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
        CanvasView *canvas = window.canvas();
        canvas->setView(QPointF(30, 40), 20);
        window.showPage(1);
        canvas->setView(QPointF(-10, 5), 8);
        // Back to the first sheet: as it was left.
        window.showPage(0);
        QCOMPARE(canvas->pixelsPerMm(), 20.0);
        QVERIFY(QLineF(canvas->viewCenter(), QPointF(30, 40)).length() < 0.5);

        // Another project, then this one again: the views come from the file.
        window.newProject();
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral(".ppview"))));
        QVERIFY(window.openProject(path));
        QCOMPARE(canvas->pixelsPerMm(), 20.0);
        window.showPage(1);
        QCOMPARE(canvas->pixelsPerMm(), 8.0);
        QVERIFY(QLineF(canvas->viewCenter(), QPointF(-10, 5)).length() < 0.5);
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
        QVERIFY(!window.statusText().isEmpty());
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

    void problemsAreListedAndShown()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.newProject();
        QTreeWidget *tree = window.findChild<QTreeWidget *>(QStringLiteral("ercTree"));
        QVERIFY(tree != nullptr);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 0);

        // A piece of wire on the second sheet: two loose ends.
        Document *document = window.document();
        Design design = document->design();
        Sheet second;
        second.id = newObjectId();
        Wire wire;
        wire.id = newObjectId();
        wire.points = {Point{0, 0}, Point{10 * NM_PER_MM, 0}};
        second.wires = {wire};
        design.schematic.sheets.append(second);
        document->change(QStringLiteral("piece"), design);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QTreeWidgetItem *group = tree->topLevelItem(0);
        QCOMPARE(group->text(0), QStringLiteral("Sheet 2"));
        QCOMPARE(group->childCount(), 2);
        QCOMPARE(group->child(0)->text(0), QStringLiteral("Wire end joins nothing"));

        // Chosen, it is shown with the wire chosen.
        window.showPage(0);
        group->child(1)->setSelected(true);
        QCOMPARE(window.designItem()->page(), 1);
        QCOMPARE(window.sheetEditor()->selection(), QSet<ObjectId>({wire.id}));
        QVERIFY(group->child(1)->isSelected());

        // Chosen on the sheet, the wire chooses its first problem in the list;
        // anything else clears it.
        window.sheetEditor()->setSelection({});
        QVERIFY(tree->selectedItems().isEmpty());
        window.sheetEditor()->setSelection({wire.id});
        QCOMPARE(tree->selectedItems(), QList<QTreeWidgetItem *>({group->child(0)}));
    }

    void theSheetShownStaysShownWhenSheetsChange()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        window.newProject();
        Document *document = window.document();
        const QStringList oneSheet = {QStringLiteral("Sheet 1"), QStringLiteral("Board")};
        const QStringList twoSheets = {QStringLiteral("Sheet 1"), QStringLiteral("Sheet 2"),
                                       QStringLiteral("Board")};

        // "+" adds a sheet at the end and shows it.
        QToolButton *add = nullptr;
        for (QToolButton *button : window.findChild<SheetTabs *>()->findChildren<QToolButton *>()) {
            if (button->text() == QStringLiteral("+"))
                add = button;
        }
        QVERIFY(add != nullptr);
        add->click();
        QCOMPARE(tabTexts(window), twoSheets);
        QCOMPARE(window.designItem()->page(), 1);
        QCOMPARE(window.findChild<QTabBar *>()->currentIndex(), 1);

        // Undone while shown, the sheet goes and the last sheet is shown,
        // not the board that takes its place.
        document->undoStack()->undo();
        QCOMPARE(tabTexts(window), oneSheet);
        QCOMPARE(window.designItem()->page(), 0);
        document->undoStack()->redo();
        QCOMPARE(tabTexts(window), twoSheets);
        QCOMPARE(window.designItem()->page(), 0);

        // The sheet shown follows its sheet when the sheets swap.
        window.showPage(1);
        const ObjectId shown = document->design().schematic.sheets.at(1).id;
        Design design = document->design();
        design.schematic.sheets.swapItemsAt(0, 1);
        document->change(QStringLiteral("swap"), design);
        QCOMPARE(window.designItem()->page(), 0);
        QVERIFY(document->design().schematic.sheets.at(0).id == shown);
        QCOMPARE(window.findChild<QTabBar *>()->currentIndex(), 0);
        QCOMPARE(window.sheetEditor()->sheet(), 0);

        // Deleted while shown, the sheet that takes its place is shown.
        design = document->design();
        design.schematic.sheets.removeAt(0);
        document->change(QStringLiteral("delete"), design);
        QCOMPARE(tabTexts(window), oneSheet);
        QCOMPARE(window.designItem()->page(), 0);
        QVERIFY(document->design().schematic.sheets.at(0).id != shown);
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
        QVERIFY2(window.statusText().startsWith(
                     QStringLiteral("library/parts/diode.json")),
                 qPrintable(window.statusText()));
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
        QVERIFY(!window.statusText().isEmpty());
    }

    // A version opens in a window for reading only, its number in the
    // title, and is not made a recent project.
    void aVersionCopyIsShownForReading()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        QVERIFY(window.saveProjectAs(dir.path(), QStringLiteral("amp")));
        const QString projectPath = window.document()->projectPath();
        QString path;
        QString error;
        QVERIFY2(saveProjectVersion(projectPath, window.document()->design().project,
                                    QStringLiteral("Start"), QDateTime::currentDateTime(), path,
                                    error),
                 qPrintable(error));
        const QStringList recent = AppSettings::recentProjects();

        MainWindow copy;
        QVERIFY2(copy.showVersionCopy(1, path, projectPath), qPrintable(copy.statusText()));
        QVERIFY(copy.document()->isReadOnly());
        QVERIFY(!copy.findChild<QLabel *>(QStringLiteral("versionBand"))->isHidden());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("versionBand"))->isHidden());
        QVERIFY(copy.document()->projectPath() != projectPath);
        QVERIFY2(copy.windowTitle().contains(QStringLiteral("1")), qPrintable(copy.windowTitle()));
        QCOMPARE(AppSettings::recentProjects(), recent);
        // What is made from it goes into the project's own output folder.
        QString problem;
        QCOMPARE(projectOutputFile(*copy.document(), QStringLiteral("-bom"), QStringLiteral("txt"),
                                   problem),
                 QDir(DesignFiles::outputPath(projectPath)).filePath(QStringLiteral("amp-v1-bom.txt")));
        QVERIFY(!copy.showVersionCopy(2, dir.filePath(QStringLiteral("none.zip")), projectPath));
    }

    void theSavedMarkTellsWhetherTheProjectIsSaved()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        MainWindow window;
        const QPushButton *mark = window.findChild<QPushButton *>(QStringLiteral("savedMark"));
        QVERIFY(mark != nullptr);
        QCOMPARE(mark->text(), QStringLiteral("New"));

        QVERIFY(window.saveProjectAs(dir.path(), QStringLiteral("marked")));
        QCOMPARE(mark->text(), QStringLiteral("Saved"));

        Design changed = window.document()->design();
        changed.schematic.sheets[0].title = QStringLiteral("Power");
        window.document()->change(QStringLiteral("Rename"), changed);
        QCOMPARE(mark->text(), QStringLiteral("Not saved"));

        window.document()->undoStack()->undo();
        QCOMPARE(mark->text(), QStringLiteral("Saved"));

        // A click on the mark saves.
        window.document()->change(QStringLiteral("Rename"), changed);
        QCOMPARE(mark->text(), QStringLiteral("Not saved"));
        const_cast<QPushButton *>(mark)->click();
        QCOMPARE(mark->text(), QStringLiteral("Saved"));
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

    // Before the Gerber export: OK takes the revision typed, trimmed, and
    // Cancel leaves it; there is no Next there.
    void gerberRevisionIsAsked()
    {
        QString revision = QStringLiteral("A");
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog != nullptr);
            QVERIFY(dialog->findChild<QPushButton *>(QStringLiteral("nextRevisionButton"))
                    == nullptr);
            auto *edit = dialog->findChild<QLineEdit *>(QStringLiteral("gerberRevisionEdit"));
            QCOMPARE(edit->text(), QStringLiteral("A"));
            edit->setText(QStringLiteral(" B "));
            dialog->accept();
        });
        QVERIFY(askGerberRevision(nullptr, revision));
        QCOMPARE(revision, QStringLiteral("B"));

        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog != nullptr);
            dialog->findChild<QLineEdit *>(QStringLiteral("gerberRevisionEdit"))
                ->setText(QStringLiteral("7"));
            dialog->reject();
        });
        QVERIFY(!askGerberRevision(nullptr, revision));
        QCOMPARE(revision, QStringLiteral("B"));
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

    // Pads placed one by one go on in number, turn, step back with Undo
    // and are saved into the user's library, which the window reads again.
    void aFootprintIsDrawnAndSaved()
    {
        MainWindow window;
        const QString name = QStringLiteral("test-editor-pads");
        const QString path =
            QDir(AppSettings::userLibraryFolder()).filePath(QStringLiteral("footprints/") + name
                                                            + QStringLiteral(".json"));
        // Left by an earlier run.
        QFile::remove(path);
        FootprintEditor *editor = window.openFootprintEditor(true);
        QVERIFY(editor != nullptr);
        QVERIFY(editor->name().isEmpty());
        // A new one has its designator and value apart, above and below.
        QCOMPARE(editor->footprint().refAt, (Point{0, -2540000}));
        QCOMPARE(editor->footprint().valueAt, (Point{0, 2540000}));
        editor->setMode(FootprintEditor::Mode::Place);
        editor->press(QPointF(0, 0), false);
        editor->release(QPointF(0, 0));
        editor->press(QPointF(2.54, 0), false);
        editor->release(QPointF(2.54, 0));
        QCOMPARE(editor->footprint().pads.size(), 2);
        QCOMPARE(editor->footprint().pads.at(1).number, QStringLiteral("2"));
        QCOMPARE(editor->footprint().pads.at(1).at, (Point{2540000, 0}));
        QVERIFY(!editor->isSaved());

        // A short right click calls the pads off, as Esc.
        emit editor->canvas()->contextMenuRequested(QPoint());
        QCOMPARE(editor->mode(), FootprintEditor::Mode::Select);
        editor->chooseAll();
        editor->rotate();
        QCOMPARE(editor->footprint().pads.at(1).at.x, editor->footprint().pads.at(0).at.x);
        editor->undoStack()->undo();
        QCOMPARE(editor->footprint().pads.at(1).at, (Point{2540000, 0}));

        // A drag of a chosen pad moves it on the grid.
        ItemChoice padTwo;
        padTwo.terminals = {1};
        editor->setChosen(padTwo);
        editor->press(QPointF(2.54, 0), false);
        editor->move(QPointF(3.9, 1.2));
        editor->release(QPointF(3.9, 1.2));
        QCOMPARE(editor->footprint().pads.at(1).at, (Point{3810000, 1270000}));

        // The silkscreen: a line from click to click, a rectangle by its
        // corners, one of which is then dragged.
        editor->setMode(FootprintEditor::Mode::Line);
        editor->press(QPointF(0, 5.08), false);
        editor->press(QPointF(5.08, 5.5), false);
        editor->escape();
        QCOMPARE(editor->footprint().graphics.size(), 1);
        // Kept across, the click a little off.
        QCOMPARE(editor->footprint().graphics.at(0).b, (Point{5080000, 5080000}));
        editor->setMode(FootprintEditor::Mode::Rect);
        editor->press(QPointF(-2.54, -2.54), false);
        editor->press(QPointF(7.62, 2.54), false);
        QCOMPARE(editor->footprint().graphics.size(), 2);
        editor->escape();
        QCOMPARE(editor->mode(), FootprintEditor::Mode::Select);
        editor->press(QPointF(7.62, 2.54), false);
        editor->move(QPointF(8.89, 3.81));
        editor->release(QPointF(8.89, 3.81));
        QCOMPARE(editor->footprint().graphics.at(1).b, (Point{8890000, 3810000}));
        QCOMPARE(editor->footprint().graphics.at(1).a, (Point{-2540000, -2540000}));
        // The designator's place is dragged, taken beside pad 1 on the
        // origin: close in, the pad no longer reaches there.
        editor->canvas()->setView(QPointF(0, 0), 40);
        Footprint onOrigin = editor->footprint();
        onOrigin.refAt = Point();
        editor->change(QStringLiteral("Designator on the origin"), onOrigin, {});
        const Point ref = editor->footprint().refAt;
        const QPointF beside = toMm(ref) + QPointF(1.4, 0);
        editor->press(beside, false);
        editor->move(beside + QPointF(2.54, 0));
        editor->release(beside + QPointF(2.54, 0));
        QCOMPARE(editor->footprint().refAt, (ref + Point{2540000, 0}));

        // Measured from pad centre to pad centre, taken off them.
        editor->setMode(FootprintEditor::Mode::Measure);
        editor->press(QPointF(0.3, 0.2), false);
        editor->hover(QPointF(3.7, 1.0));
        editor->press(QPointF(3.7, 1.0), false);
        QCOMPARE(editor->measurement(), (std::optional<QLineF>(QLineF(0, 0, 3.81, 1.27))));
        editor->escape();
        QVERIFY(!editor->measurement());
        editor->escape();
        QCOMPARE(editor->mode(), FootprintEditor::Mode::Select);

        // A circle off the grid by its diameter typed.
        editor->setMode(FootprintEditor::Mode::Circle);
        editor->press(QPointF(0, 10.16), false);
        editor->press(QPointF(2.54, 10.16), false);
        editor->escape();
        QLineEdit *diameter = editor->findChild<QLineEdit *>(QStringLiteral("drawingDiameter"));
        QVERIFY(diameter != nullptr && diameter->isEnabled());
        QCOMPARE(diameter->text(), QStringLiteral("5.08"));
        diameter->setText(QStringLiteral("3.3"));
        emit diameter->editingFinished();
        QCOMPARE(editor->footprint().graphics.last().radius, 1650000);

        // A body: a cylinder standing up, its height typed.
        QComboBox *bodyShape = editor->findChild<QComboBox *>(QStringLiteral("bodyShape"));
        QVERIFY(bodyShape != nullptr);
        emit bodyShape->activated(int(BodyShape::Upright) + 1);
        QVERIFY(editor->footprint().body.has_value());
        QCOMPARE(editor->footprint().body->shape, BodyShape::Upright);
        QLineEdit *bodyHeight = editor->findChild<QLineEdit *>(QStringLiteral("bodyHeight"));
        bodyHeight->setText(QStringLiteral("7"));
        emit bodyHeight->editingFinished();
        QCOMPARE(editor->footprint().body->height, 7000000);
        emit bodyShape->activated(0);
        QVERIFY(!editor->footprint().body.has_value());

        // Pads nearer than the project's clearance are found.
        QVERIFY(editor->padGaps().isEmpty());
        editor->setClearance(5000000);
        QCOMPARE(editor->padGaps().size(), 1);
        editor->setClearance(0);

        QVERIFY(editor->saveAs(name, true));
        QVERIFY(editor->isSaved());
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(window.library().items.footprints.contains(name));
        QVERIFY(FootprintEditor::isInUserLibrary(name));
        QCOMPARE(editor->tabTitle(), name);

        // Renamed, the user's part listing it follows.
        const QDir userLibrary(AppSettings::userLibraryFolder());
        const QString partPath = userLibrary.filePath(QStringLiteral("parts/test-editor-part.json"));
        LibraryItem partItem;
        partItem.kind = LibraryKind::Part;
        partItem.part.symbol = QStringLiteral("resistor");
        partItem.part.prefix = QStringLiteral("R");
        partItem.part.prefixGost = QStringLiteral("R");
        partItem.part.footprints = {name};
        QString partError;
        QVERIFY2(LibraryFile::write(partPath, partItem, partError), qPrintable(partError));
        const QString renamed = name + QStringLiteral("-renamed");
        const QString renamedPath = userLibrary.filePath(QStringLiteral("footprints/") + renamed
                                                         + QStringLiteral(".json"));
        QFile::remove(renamedPath);
        QVERIFY(editor->canRename());
        QVERIFY(editor->renameTo(renamed));
        QCOMPARE(editor->name(), renamed);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(QFileInfo::exists(renamedPath));
        LibraryItem readBack;
        QVERIFY(LibraryFile::read(partPath, readBack, partError));
        QCOMPARE(readBack.part.footprints, QStringList({renamed}));
        QVERIFY(window.library().items.footprints.contains(renamed));
        QVERIFY(!window.library().items.footprints.contains(name));
        QFile::remove(partPath);
        QFile::remove(renamedPath);

        // One window, a tab for each footprint; a new one in a tab of its
        // own beside it.
        LibraryEditor *libraryWindow = window.libraryEditor();
        QVERIFY(libraryWindow != nullptr);
        FootprintEditor *second = window.openFootprintEditor(true);
        QCOMPARE(window.libraryEditor(), libraryWindow);
        QCOMPARE(libraryWindow->tabs()->count(), 2);
        QCOMPARE(libraryWindow->currentFootprintEditor(), second);
        QVERIFY(libraryWindow->closeTab(1));
        QCOMPARE(libraryWindow->tabs()->count(), 1);
        // A copy has the pads and no name yet.
        FootprintEditor *copy = qobject_cast<FootprintEditor *>(libraryWindow->openCopy());
        QVERIFY(copy != nullptr);
        QVERIFY(copy->name().isEmpty());
        QCOMPARE(copy->footprint(), editor->footprint());
        QVERIFY(!copy->isSaved());
        QCOMPARE(copy->tabTitle(), FootprintEditor::newFootprintTitle() + QLatin1Char('*'));
        QCOMPARE(libraryWindow->tabs()->count(), 2);
        // Dropped without asking.
        copy->undoStack()->setClean();
        QVERIFY(libraryWindow->closeTab(1));
        libraryWindow->close();
        QFile::remove(path);
    }

    // Drawings and pads are copied and pasted, into another tab too: a
    // pad's number taken is numbered on, and a symbol takes the drawings.
    void itemsAreCopiedAndPasted()
    {
        MainWindow window;
        FootprintEditor *editor = window.openFootprintEditor(true);
        QVERIFY(editor != nullptr);
        editor->setMode(FootprintEditor::Mode::Place);
        editor->press(QPointF(0, 0), false);
        editor->release(QPointF(0, 0));
        editor->setMode(FootprintEditor::Mode::Rect);
        editor->press(QPointF(-2.54, -2.54), false);
        editor->press(QPointF(2.54, 2.54), false);
        editor->escape();
        QCOMPARE(editor->footprint().pads.size(), 1);
        QCOMPARE(editor->footprint().graphics.size(), 1);
        ItemChoice both;
        both.terminals = {0};
        both.graphics = {0};
        editor->setChosen(both);
        editor->copyChosen();
        QCOMPARE(ItemEditor::clipboard().pads.size(), 1);
        QCOMPARE(ItemEditor::clipboard().graphics.size(), 1);

        // It follows the cursor by the middle it was taken by; a click
        // puts it in, chosen.
        editor->paste();
        QCOMPARE(editor->mode(), FootprintEditor::Mode::Row);
        editor->press(QPointF(10.16, 0), false);
        editor->release(QPointF(10.16, 0));
        QCOMPARE(editor->mode(), FootprintEditor::Mode::Select);
        QCOMPARE(editor->footprint().pads.size(), 2);
        QCOMPARE(editor->footprint().pads.at(1).number, QStringLiteral("2"));
        QCOMPARE(editor->footprint().pads.at(1).at, (Point{10160000, 0}));
        QCOMPARE(editor->footprint().graphics.size(), 2);
        QCOMPARE(editor->chosen().terminals, QSet<int>({1}));
        QCOMPARE(editor->chosen().graphics, QSet<int>({1}));
        editor->undoStack()->undo();
        QCOMPARE(editor->footprint().pads.size(), 1);

        // Cut: copied and gone in one step.
        editor->setChosen(both);
        editor->cutChosen();
        QVERIFY(editor->footprint().pads.isEmpty());
        QVERIFY(editor->footprint().graphics.isEmpty());
        editor->undoStack()->undo();
        QCOMPARE(editor->footprint().pads.size(), 1);

        // A symbol takes the drawing, not the pad.
        SymbolEditor *symbol = window.openSymbolEditor(true);
        QVERIFY(symbol != nullptr);
        symbol->paste();
        QCOMPARE(symbol->mode(), SymbolEditor::Mode::Row);
        symbol->press(QPointF(0, 0), false);
        symbol->release(QPointF(0, 0));
        QCOMPARE(symbol->symbol().graphics.size(), 1);
        QVERIFY(symbol->symbol().pins.isEmpty());
        // Its own texts stay where a new symbol has them.
        QCOMPARE(symbol->symbol().refAt, (Point{0, -2540000}));

        editor->undoStack()->setClean();
        symbol->undoStack()->setClean();
        window.libraryEditor()->close();
    }

    // Pins land on the pin grid whatever the grid of the drawings, turn
    // with their side, are dragged by whole steps of it, and the symbol is
    // saved into the user's library.
    void aSymbolIsDrawnAndSaved()
    {
        MainWindow window;
        const QString name = QStringLiteral("test-editor-pins");
        const QString path =
            QDir(AppSettings::userLibraryFolder()).filePath(QStringLiteral("symbols/") + name
                                                            + QStringLiteral(".json"));
        // Left by an earlier run.
        QFile::remove(path);
        SymbolEditor *editor = window.openSymbolEditor(true);
        QVERIFY(editor != nullptr);
        QVERIFY(editor->name().isEmpty());
        editor->setMode(ItemEditor::Mode::Place);
        // It starts on the pin grid; off it, on a finer grid of the canvas.
        QCOMPARE(editor->canvas()->gridStep(), PIN_GRID);
        editor->canvas()->setGridStep(1270000);
        editor->press(QPointF(-4.9, 0.3), false);
        editor->release(QPointF(-4.9, 0.3));
        QCOMPARE(editor->symbol().pins.size(), 1);
        QCOMPARE(editor->symbol().pins.at(0).at, (Point{-5080000, 0}));
        QCOMPARE(editor->symbol().pins.at(0).number, QStringLiteral("1"));
        // R turns the pin about to be placed: it comes from the top.
        editor->rotate();
        QCOMPARE(editor->nextPin().side, PinSide::Up);
        editor->press(QPointF(0, -5.08), false);
        editor->release(QPointF(0, -5.08));
        QCOMPARE(editor->symbol().pins.at(1).side, PinSide::Up);
        QCOMPARE(editor->symbol().pins.at(1).number, QStringLiteral("2"));
        editor->escape();
        QCOMPARE(editor->mode(), ItemEditor::Mode::Select);

        // Taken on its line and dragged a little more than half a step.
        ItemChoice first;
        first.terminals = {0};
        editor->setChosen(first);
        editor->press(QPointF(-4.5, 0), false);
        editor->move(QPointF(-3.0, 1.4));
        editor->release(QPointF(-3.0, 1.4));
        QCOMPARE(editor->symbol().pins.at(0).at, (Point{-2540000, 2540000}));
        // Turned about a point of the pin grid.
        editor->rotate();
        QCOMPARE(editor->symbol().pins.at(0).at, (Point{0, 0}));
        QCOMPARE(editor->symbol().pins.at(0).side, PinSide::Up);

        // Its end at the body dragged on the finer grid sets its length;
        // the end a wire meets stays.
        editor->canvas()->setGridStep(635000);
        editor->canvas()->setView(QPointF(0, 0), 40);
        editor->press(QPointF(0, 2.54), false);
        editor->move(QPointF(0, 3.2));
        editor->release(QPointF(0, 3.2));
        QCOMPARE(editor->symbol().pins.at(0).at, (Point{0, 0}));
        QCOMPARE(editor->symbol().pins.at(0).length, 3175000);
        editor->canvas()->setGridStep(1270000);

        // A drawing keeps the finer grid.
        editor->setMode(ItemEditor::Mode::Rect);
        editor->press(QPointF(-1.27, -1.27), false);
        editor->press(QPointF(1.27, 1.27), false);
        QCOMPARE(editor->symbol().graphics.size(), 1);
        QCOMPARE(editor->symbol().graphics.at(0).a, (Point{-1270000, -1270000}));
        editor->escape();

        // A number taken already is refused; a name is set.
        ItemChoice second;
        second.terminals = {1};
        editor->setChosen(second);
        QLineEdit *number = editor->findChild<QLineEdit *>(QStringLiteral("pinNumber"));
        QVERIFY(number != nullptr && number->isEnabled());
        number->setText(QStringLiteral("1"));
        emit number->editingFinished();
        QCOMPARE(editor->symbol().pins.at(1).number, QStringLiteral("2"));
        QLineEdit *pinName = editor->findChild<QLineEdit *>(QStringLiteral("pinName"));
        pinName->setText(QStringLiteral("VCC"));
        emit pinName->editingFinished();
        QCOMPARE(editor->symbol().pins.at(1).name, QStringLiteral("VCC"));

        QVERIFY(editor->saveAs(name, true));
        QVERIFY(editor->isSaved());
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(window.library().items.symbols.contains(name));
        QVERIFY(SymbolEditor::isInUserLibrary(name));

        // A power symbol has one pin, at its origin: with two it is not
        // written.
        QLineEdit *power = editor->findChild<QLineEdit *>(QStringLiteral("symbolPower"));
        QVERIFY(power != nullptr);
        power->setText(QStringLiteral("+5V"));
        emit power->editingFinished();
        QVERIFY(editor->symbol().isPower());
        QVERIFY(!editor->saveAs(name, true));
        editor->undoStack()->undo();
        QVERIFY(!editor->symbol().isPower());
        QVERIFY(editor->isSaved());

        // A copy of a symbol is a symbol.
        LibraryEditor *libraryWindow = window.libraryEditor();
        LibraryTab *copy = libraryWindow->openCopy();
        QVERIFY(qobject_cast<SymbolEditor *>(copy) != nullptr);
        copy->undoStack()->setClean();

        // A new symbol from a ready-made one, a diode put into it.
        SymbolEditor *fresh = libraryWindow->openSymbolFrom(pieceSymbol(SymbolPiece::Resistor));
        QCOMPARE(fresh->symbol(), pieceSymbol(SymbolPiece::Resistor));
        QVERIFY(!fresh->isSaved());
        fresh->placePiece(pieceSymbol(SymbolPiece::Diode));
        QCOMPARE(fresh->mode(), ItemEditor::Mode::Row);
        fresh->press(QPointF(0.3, 10.0), false);
        fresh->release(QPointF(0.3, 10.0));
        QCOMPARE(fresh->mode(), ItemEditor::Mode::Select);
        QCOMPARE(fresh->symbol().pins.size(), 4);
        QCOMPARE(fresh->symbol().pins.at(2).number, QStringLiteral("A"));
        QCOMPARE(fresh->symbol().pins.at(2).at, (Point{-5080000, 10160000}));
        QCOMPARE(fresh->chosen().terminals, QSet<int>({2, 3}));
        fresh->undoStack()->setClean();
        libraryWindow->close();
        QFile::remove(path);
    }

    // A part is set up in its form, checked for a pad under every pin,
    // stepped back by Undo and saved into the user's library.
    void aPartIsSetUpAndSaved()
    {
        MainWindow window;
        const QString name = QStringLiteral("test-editor-part");
        const QString path =
            QDir(AppSettings::userLibraryFolder()).filePath(QStringLiteral("parts/") + name
                                                            + QStringLiteral(".json"));
        // Left by an earlier run.
        QFile::remove(path);
        PartEditor *editor = window.openPartEditor(true);
        QVERIFY(editor != nullptr);
        QVERIFY(editor->name().isEmpty());
        // Without a symbol or prefixes it is not written.
        QVERIFY(!editor->saveAs(name, true));

        Part part = editor->part();
        part.symbol = QStringLiteral("diode");
        part.footprints = {QStringLiteral("diode-7.62"), QStringLiteral("resistor-10.16")};
        editor->change(part);
        // The resistor's pads are numbered 1 and 2, the diode's pins A and K.
        QVERIFY(editor->checkText().contains(QStringLiteral("resistor-10.16")));
        QVERIFY(!editor->checkText().contains(QStringLiteral("diode-7.62")));
        editor->chooseFootprint(1);
        editor->removeChosenFootprint();
        QCOMPARE(editor->part().footprints, QStringList({QStringLiteral("diode-7.62")}));
        QCOMPARE(editor->chosenFootprint(), 0);

        // The fields are taken when typed, and before a save.
        QLineEdit *prefix = editor->findChild<QLineEdit *>(QStringLiteral("partPrefix"));
        QVERIFY(prefix != nullptr);
        prefix->setText(QStringLiteral(" D "));
        emit prefix->editingFinished();
        QCOMPARE(editor->part().prefix, QStringLiteral("D"));
        editor->undoStack()->undo();
        QVERIFY(editor->part().prefix.isEmpty());
        QCOMPARE(prefix->text(), QString());
        prefix->setText(QStringLiteral("D"));
        QLineEdit *prefixGost = editor->findChild<QLineEdit *>(QStringLiteral("partPrefixGost"));
        prefixGost->setText(QStringLiteral("VD"));
        // The English name is needed to save.
        QLineEdit *partName = editor->findChild<QLineEdit *>(QStringLiteral("partName"));
        QVERIFY(partName != nullptr);
        partName->setText(QStringLiteral("Test diode"));
        // Another language: what was typed is kept, the field shows its own.
        QComboBox *language = editor->findChild<QComboBox *>(QStringLiteral("partNameLanguage"));
        QVERIFY(language != nullptr);
        const int russian = language->findData(QStringLiteral("ru"));
        language->setCurrentIndex(russian);
        emit language->activated(russian);
        QCOMPARE(partName->text(), QString());
        partName->setText(QStringLiteral("Тестовый диод"));
        language->setCurrentIndex(0);
        emit language->activated(0);
        QCOMPARE(partName->text(), QStringLiteral("Test diode"));
        QCOMPARE(editor->part().name.value(QStringLiteral("ru")), QStringLiteral("Тестовый диод"));

        QVERIFY(editor->saveAs(name, true));
        QCOMPARE(editor->part().prefixGost, QStringLiteral("VD"));
        QVERIFY(editor->isSaved());
        QVERIFY(QFileInfo::exists(path));
        QVERIFY(window.library().items.parts.contains(name));
        QVERIFY(PartEditor::isInUserLibrary(name));

        // A copy of a part is a part.
        LibraryEditor *libraryWindow = window.libraryEditor();
        LibraryTab *copy = libraryWindow->openCopy();
        QVERIFY(qobject_cast<PartEditor *>(copy) != nullptr);
        copy->undoStack()->setClean();
        libraryWindow->close();
        QFile::remove(path);
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
        // Left by an earlier run: it is written again below.
        QFile::remove(secondPath);
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
        // A group of the first section by its name, as it is now; the own
        // part has no group and goes under Other.
        const auto group = [tree](const QString &name) -> QTreeWidgetItem * {
            QTreeWidgetItem *section = tree->topLevelItem(0);
            for (int i = 0; i < section->childCount(); ++i) {
                if (section->child(i)->text(0) == name)
                    return section->child(i);
            }
            return nullptr;
        };
        QVERIFY(group(QStringLiteral("Other")) != nullptr);
        QVERIFY(group(QStringLiteral("Resistors")) != nullptr);
        // Other parts of the user's may be under Other too.
        const int othersBefore = group(QStringLiteral("Other"))->childCount();

        // Choosing a part draws it, with its first footprint.
        QTreeWidgetItem *resistors = group(QStringLiteral("Resistors"));
        for (int i = 0; i < resistors->childCount(); ++i) {
            if (resistors->child(i)->data(0, Qt::ToolTipRole).toString() == QStringLiteral("resistor"))
                tree->setCurrentItem(resistors->child(i));
        }
        QCOMPARE(panel->currentPart(), QStringLiteral("resistor"));
        QCOMPARE(panel->footprintBox()->currentText(), QStringLiteral("resistor-10.16"));

        // A group the user opened stays open when the library is read again
        // with something new in it, and after a search.
        tree->expandItem(group(QStringLiteral("Resistors")));
        LibraryItem second = own;
        second.part.name.insert(QStringLiteral("en"), QStringLiteral("Second part"));
        QVERIFY2(LibraryFile::write(secondPath, second, error), qPrintable(error));
        window.loadLibrary();
        QCOMPARE(group(QStringLiteral("Other"))->childCount(), othersBefore + 1);
        QVERIFY(group(QStringLiteral("Resistors"))->isExpanded());
        QVERIFY(!group(QStringLiteral("Other"))->isExpanded());
        QCOMPARE(panel->currentPart(), QStringLiteral("resistor"));
        panel->setFilter(QStringLiteral("second"));
        QVERIFY(group(QStringLiteral("Other"))->isExpanded());
        QVERIFY(group(QStringLiteral("Resistors"))->isHidden());
        panel->setFilter(QString());
        QVERIFY(group(QStringLiteral("Resistors"))->isExpanded());
        QVERIFY(!group(QStringLiteral("Other"))->isExpanded());

        // The section of the chosen part opens whole, kept across a rebuild.
        panel->expandChosenSection();
        QVERIFY(group(QStringLiteral("Other"))->isExpanded());
        QVERIFY(group(QStringLiteral("Resistors"))->isExpanded());
        window.loadLibrary();
        QVERIFY(group(QStringLiteral("Other"))->isExpanded());
        // Closing closes the sections too, and they stay closed after a
        // rebuild; a search opens what it finds, and its end closes it again.
        panel->collapseAll();
        QVERIFY(!group(QStringLiteral("Resistors"))->isExpanded());
        QVERIFY(!tree->topLevelItem(0)->isExpanded());
        window.loadLibrary();
        QVERIFY(!tree->topLevelItem(0)->isExpanded());
        panel->setFilter(QStringLiteral("second"));
        QVERIFY(tree->topLevelItem(0)->isExpanded());
        panel->setFilter(QString());
        QVERIFY(!tree->topLevelItem(0)->isExpanded());
        // A section opened by hand stays open.
        tree->expandItem(tree->topLevelItem(0));
        window.loadLibrary();
        QVERIFY(tree->topLevelItem(0)->isExpanded());
        // The title of a section stands out.
        QVERIFY(tree->topLevelItem(0)->font(0).bold());

        // The search goes by prefix too.
        panel->setFilter(QStringLiteral("tst"));
        QVERIFY(!group(QStringLiteral("Other"))->isHidden());
        QVERIFY(group(QStringLiteral("Resistors"))->isHidden());
        panel->setFilter(QString());

        // The power symbols come first, in a group of their own; one chosen
        // has no footprint, and a search finds it by its net name too.
        QTreeWidgetItem *power = tree->topLevelItem(0)->child(0);
        QCOMPARE(power->text(0), QStringLiteral("Power"));
        QVERIFY(power->childCount() >= 4);
        tree->setCurrentItem(power->child(0));
        QVERIFY(panel->currentIsPower());
        QCOMPARE(panel->footprintBox()->count(), 0);
        panel->setFilter(QStringLiteral("GND"));
        QVERIFY(!power->isHidden());
        QVERIFY(group(QStringLiteral("Resistors"))->isHidden());
        panel->setFilter(QString());

        QVERIFY(QFile::remove(ownPath));
        QVERIFY(QFile::remove(secondPath));

        // The open project's parts are a section of their own.
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        QCOMPARE(tree->topLevelItemCount(), 2);
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Project"));
    }

    void propertiesPanelEditsTheChosenComponents()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto sheet = [&window]() -> const Sheet & {
            return window.document()->design().schematic.sheets.at(0);
        };
        // R1 10k on resistor-10.16, R2 4.7k on resistor-vertical.
        const ObjectId r1 = sheet().components.at(0).id;
        const ObjectId r2 = sheet().components.at(1).id;
        auto *designator = window.findChild<QLineEdit *>(QStringLiteral("propertiesDesignator"));
        auto *value = window.findChild<QLineEdit *>(QStringLiteral("propertiesValue"));
        auto *footprint = window.findChild<QComboBox *>(QStringLiteral("propertiesFootprint"));
        QVERIFY(designator != nullptr && value != nullptr && footprint != nullptr);
        QVERIFY(!value->isEnabled());

        editor->setSelection({r1});
        QVERIFY(designator->isEnabled());
        QCOMPARE(designator->text(), QStringLiteral("R1"));
        QCOMPARE(value->text(), QStringLiteral("10k"));
        QCOMPARE(footprint->currentText(), QStringLiteral("resistor-10.16"));
        // R2's designator is red, and is still set.
        designator->setText(QStringLiteral("R2"));
        emit designator->textEdited(designator->text());
        QVERIFY(!designator->styleSheet().isEmpty());

        // Both: one designator cannot be set, the values differ, and a value
        // set goes to both as one step.
        editor->setSelection({r1, r2});
        QVERIFY(!designator->isEnabled());
        QVERIFY(value->text().isEmpty());
        QVERIFY(!value->placeholderText().isEmpty());
        QCOMPARE(footprint->currentIndex(), -1);
        // Left empty over values that differ, nothing changes.
        value->clear();
        emit value->editingFinished();
        QCOMPARE(sheet().components.at(1).value, QStringLiteral("4.7k"));
        const int steps = window.document()->undoStack()->count();
        value->setText(QStringLiteral("22k"));
        emit value->editingFinished();
        QCOMPARE(sheet().components.at(0).value, QStringLiteral("22k"));
        QCOMPARE(sheet().components.at(1).value, QStringLiteral("22k"));
        QCOMPARE(window.document()->undoStack()->count(), steps + 1);
        QCOMPARE(value->text(), QStringLiteral("22k"));
        // Esc takes back what was typed, and the selection stays.
        value->setFocus();
        value->setText(QStringLiteral("zzz"));
        QTest::keyClick(value, Qt::Key_Escape);
        QCOMPARE(value->text(), QStringLiteral("22k"));
        QCOMPARE(editor->selection(), QSet<ObjectId>({r1, r2}));
        QCOMPARE(sheet().components.at(0).value, QStringLiteral("22k"));

        footprint->setCurrentIndex(footprint->findText(QStringLiteral("resistor-vertical")));
        emit footprint->activated(footprint->currentIndex());
        QCOMPARE(sheet().components.at(0).footprint, QStringLiteral("resistor-vertical"));

        // A drag of one of the parts chosen takes both; a click keeps it alone.
        editor->setSelection({r1, r2});
        const QPointF r1At = toMm(sheet().components.at(0).at);
        editor->press(r1At, false);
        editor->move(r1At + QPointF(5.08, 0), false);
        editor->release(r1At + QPointF(5.08, 0), false);
        QCOMPARE(editor->selection(), QSet<ObjectId>({r1, r2}));
        editor->press(r1At + QPointF(5.08, 0), false);
        editor->release(r1At + QPointF(5.08, 0), false);
        QCOMPARE(editor->selection(), QSet<ObjectId>({r1}));

        // A click on R1's value chooses the text alone; the panel shows R1.
        const QPointF valueAt =
            toMm(componentTextPoint(sheet().components.at(0), ComponentText::Value));
        editor->press(valueAt, false);
        editor->release(valueAt, false);
        QVERIFY(editor->selection().isEmpty());
        QVERIFY(!editor->chosenText().isEmpty());
        QCOMPARE(designator->text(), QStringLiteral("R1"));
        // R turns nothing, Delete deletes nothing.
        const Sheet before = sheet();
        editor->rotateSelection();
        editor->deleteSelection();
        QVERIFY(sheet() == before);
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

        // An Undo while dragging ends the drag: the drop brings back
        // neither the step undone nor the drag.
        {
            const Sheet current = sheet();
            window.document()->undoStack()->undo();
            const Sheet afterUndo = sheet();
            window.document()->undoStack()->redo();
            QVERIFY(sheet() == current);
            const QPointF from = toMm(sheet().components.at(0).at);
            editor->press(from, false);
            editor->move(from + QPointF(5.2, 0), false);
            QVERIFY(editor->preview() != nullptr);
            window.document()->undoStack()->undo();
            QVERIFY(editor->preview() == nullptr);
            editor->release(from + QPointF(5.2, 0), false);
            QVERIFY(sheet() == afterUndo);
            QVERIFY(window.document()->undoStack()->canRedo());
            window.document()->undoStack()->redo();
            QVERIFY(sheet() == current);
        }

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
        // The wires on its pins go with them.
        QCOMPARE(sheet().wires.at(0).points.last(), r1.at + Point({0, -5080000}));
        // The other may be reached by a new wire off its net instead.
        const Point lower = r1.at + Point({0, 5080000});
        QVERIFY(std::any_of(sheet().wires.cbegin(), sheet().wires.cend(), [&lower](const Wire &w) {
            return w.points.first() == lower || w.points.last() == lower;
        }));

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

    // The demo's first wire ends on the left pin of R1. Taken at that end
    // and dragged down, its segment goes and a bend keeps the end on the pin.
    void aWireEndOnAPinIsNotDraggedOffIt()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto wire = [&window]() {
            return window.document()->design().schematic.sheets.at(0).wires.at(0);
        };
        // The wire runs from the +5V symbol's point to a pin: both hold its
        // ends, so the segment goes down and bends lead back to them.
        const Point power = wire().points.first();
        const Point pin = wire().points.last();
        editor->press(toMm(pin), false);
        editor->move(toMm(pin) + QPointF(0, 5.2), false);
        editor->release(toMm(pin) + QPointF(0, 5.2), false);
        QCOMPARE(wire().points.last(), pin);
        QCOMPARE(wire().points.first(), power);
        const QList<Point> points = wire().points;
        QVERIFY(std::any_of(points.cbegin(), points.cend(),
                            [&pin](const Point &p) { return p.y == pin.y + 5080000; }));
    }

    // The demo's OUT wire starts at the corner of the wire from R1 to R2: a
    // joint. Taken there and dragged down, it slides along that wire, and
    // the OUT wire follows, its far end staying.
    void aJointIsDraggedAlongItsWire()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto wires = [&window]() {
            return window.document()->design().schematic.sheets.at(0).wires;
        };
        const Wire through = wires().at(1);
        const Wire out = wires().at(2);
        const Point joint = out.points.first();
        QCOMPARE(through.points.at(1), joint);
        editor->press(toMm(joint) + QPointF(0.3, 0), false);
        editor->move(toMm(joint) + QPointF(0.3, 5.2), false);
        editor->release(toMm(joint) + QPointF(0.3, 5.2), false);
        const auto now = [&wires](const ObjectId &id) {
            const QList<Wire> list = wires();
            return *std::find_if(list.cbegin(), list.cend(),
                                 [&id](const Wire &w) { return w.id == id; });
        };
        QCOMPARE(now(through.id).points, through.points);
        QCOMPARE(now(out.id).points.first(), joint + Point({0, 5080000}));
        QCOMPARE(now(out.id).points.last(), out.points.last());
        // Its label stays on it.
        const Label label = window.document()->design().schematic.sheets.at(0).labels.at(0);
        QVERIFY(isOnWire(now(out.id), label.at));
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

    // P places the chassis first, then the power symbol placed last; each
    // one gets the net name of its symbol, which is copied into the project.
    void powerSymbolsArePlacedWithP()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto powers = [&window]() {
            return window.document()->design().schematic.sheets.at(0).powerSymbols;
        };
        const int before = int(powers().size());
        QAction *place = window.findChild<QAction *>(QStringLiteral("actionPlacePower"));
        QVERIFY(place != nullptr);

        place->trigger();
        QVERIFY(editor->isPlacing());
        editor->hover(Point{mmToNm(50.8), mmToNm(50.8)});
        editor->press(QPointF(50.8, 50.8), false);
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        QCOMPARE(int(powers().size()), before + 1);
        QCOMPARE(powers().last().symbol, QStringLiteral("chassis"));
        QCOMPARE(powers().last().name, QStringLiteral("CHASSIS"));
        QCOMPARE(powers().last().at, Point({mmToNm(50.8), mmToNm(50.8)}));

        QVERIFY(window.placePower(LibraryPanel::Source::Library, QStringLiteral("power-arrow")));
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        place->trigger();
        editor->hover(Point{mmToNm(60.96), mmToNm(50.8)});
        editor->press(QPointF(60.96, 50.8), false);
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        QCOMPARE(int(powers().size()), before + 2);
        QCOMPARE(powers().last().symbol, QStringLiteral("power-arrow"));
        QCOMPARE(powers().last().name, QStringLiteral("+5V"));
        QVERIFY(window.document()->design().library.findSymbol(QStringLiteral("power-arrow"))
                != nullptr);

        // A symbol that is not a power symbol is not placed as one.
        QVERIFY(!window.placePower(LibraryPanel::Source::Library, QStringLiteral("resistor")));
    }

    // A double click on a power symbol opens a line over its net name: Enter
    // renames it as one step, Esc and a blank name leave it as it is.
    void powerNamesAreEditedInPlace()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto name = [&window]() {
            return window.document()->design().schematic.sheets.at(0).powerSymbols.at(0).name;
        };
        // The line shown: one closed is hidden until it is deleted.
        const auto line = [&window]() -> QLineEdit * {
            for (QLineEdit *edit : window.canvas()->viewport()->findChildren<QLineEdit *>()) {
                if (edit->isVisible())
                    return edit;
            }
            return nullptr;
        };
        // Between the bar of the +5V symbol and its name, clear of the wire
        // from its point.
        const QPointF stem(91.44, 73.0);

        editor->doubleClick(stem, false);
        QVERIFY(line() != nullptr);
        QCOMPARE(line()->text(), QStringLiteral("+5V"));
        line()->setText(QStringLiteral(" +12V "));
        QTest::keyClick(line(), Qt::Key_Return);
        QCOMPARE(name(), QStringLiteral("+12V"));
        QVERIFY(window.document()->undoStack()->undoText().startsWith(QStringLiteral("Rename +5V")));

        editor->doubleClick(stem, false);
        line()->setText(QStringLiteral("+9V"));
        QTest::keyClick(line(), Qt::Key_Escape);
        QCOMPARE(name(), QStringLiteral("+12V"));

        editor->doubleClick(stem, false);
        line()->setText(QStringLiteral("  "));
        QTest::keyClick(line(), Qt::Key_Return);
        QCOMPARE(name(), QStringLiteral("+12V"));

        window.document()->undoStack()->undo();
        QCOMPARE(name(), QStringLiteral("+5V"));
    }

    // L: a label follows the cursor, a click asks for its text, Enter puts
    // it down and offers the next one numbered on; Esc in the line drops
    // that one only. A double click on a label edits its text.
    void labelsArePlacedWithTheirText()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto labels = [&window]() {
            return window.document()->design().schematic.sheets.at(0).labels;
        };
        const auto line = [&window]() -> QLineEdit * {
            for (QLineEdit *edit : window.canvas()->viewport()->findChildren<QLineEdit *>()) {
                if (edit->isVisible())
                    return edit;
            }
            return nullptr;
        };
        const int before = int(labels().size());

        window.findChild<QAction *>(QStringLiteral("actionPlaceLabel"))->trigger();
        QVERIFY(editor->isPlacing());
        editor->press(QPointF(50.8, 50.8), false);
        QVERIFY(line() != nullptr);
        QCOMPARE(line()->text(), QString());
        // Nothing comes of a blank text: the line closes, the label waits.
        QTest::keyClick(line(), Qt::Key_Return);
        QCOMPARE(int(labels().size()), before);
        editor->press(QPointF(50.8, 50.8), false);
        line()->setText(QStringLiteral("D0"));
        QTest::keyClick(line(), Qt::Key_Return);
        QCOMPARE(int(labels().size()), before + 1);
        QCOMPARE(labels().last().text, QStringLiteral("D0"));
        QCOMPARE(labels().last().at, Point({mmToNm(50.8), mmToNm(50.8)}));
        QVERIFY(editor->isPlacing());

        editor->hover(Point{mmToNm(50.8), mmToNm(55.88)});
        editor->press(QPointF(50.8, 55.88), false);
        QCOMPARE(line()->text(), QStringLiteral("D1"));
        QTest::keyClick(line(), Qt::Key_Escape);
        QCOMPARE(int(labels().size()), before + 1);
        QVERIFY(editor->isPlacing());
        editor->press(QPointF(50.8, 55.88), false);
        QTest::keyClick(line(), Qt::Key_Return);
        QCOMPARE(labels().last().text, QStringLiteral("D1"));
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        QVERIFY(!editor->isPlacing());
        QVERIFY(window.document()->undoStack()->undoText().startsWith(QStringLiteral("Place D1")));

        editor->doubleClick(QPointF(51.5, 54.9), false);
        QVERIFY(line() != nullptr);
        QCOMPARE(line()->text(), QStringLiteral("D1"));
        line()->setText(QStringLiteral("CLK"));
        QTest::keyClick(line(), Qt::Key_Return);
        QCOMPARE(labels().last().text, QStringLiteral("CLK"));

        // Its text dragged two grid steps up, far enough to be a drag at
        // this zoom: the point stays.
        const Point point = labels().last().at;
        editor->press(QPointF(51.5, 54.9), false);
        editor->move(QPointF(51.5, 49.82), false);
        editor->release(QPointF(51.5, 49.82), false);
        QCOMPARE(labels().last().at, point);
        QCOMPARE(labels().last().textAt, Point({0, -mmToNm(5.08)}));
    }

    // X: a no-connect mark is put down only on a pin of a part that has
    // none yet; elsewhere a click does nothing and placing goes on.
    void noConnectMarksGoOnPinsOnly()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto marks = [&window]() {
            return window.document()->design().schematic.sheets.at(0).noConnects;
        };
        const int before = int(marks().size());
        window.findChild<QAction *>(QStringLiteral("actionPlaceNoConnect"))->trigger();
        QVERIFY(editor->isPlacing());
        editor->press(QPointF(50.8, 50.8), false);
        QCOMPARE(int(marks().size()), before);
        // The left pin of R1.
        editor->press(QPointF(96.52, 76.2), false);
        QCOMPARE(int(marks().size()), before + 1);
        QCOMPARE(marks().last().at, Point({mmToNm(96.52), mmToNm(76.2)}));
        editor->press(QPointF(96.52, 76.2), false);
        QCOMPARE(int(marks().size()), before + 1);
        QVERIFY(editor->isPlacing());
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        QVERIFY(!editor->isPlacing());
    }

    // B draws buses like wires, each named BUS and a number of its own; a
    // wire drawn to a bus ends on it; a double click on the name renames it.
    void busesAreDrawnAndRenamed()
    {
        MainWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(window.openProject(QStringLiteral(PPCB_SOURCE_DIR "/examples/demo/demo.ppcb")));
        SheetEditor *editor = window.sheetEditor();
        const auto sheet = [&window]() -> const Sheet & {
            return window.document()->design().schematic.sheets.at(0);
        };
        const int buses = int(sheet().buses.size());
        const int wires = int(sheet().wires.size());

        window.findChild<QAction *>(QStringLiteral("actionBus"))->trigger();
        QVERIFY(editor->isWiring());
        editor->press(QPointF(30.48, 30.48), false);
        editor->press(QPointF(60.96, 30.48), false);
        editor->press(QPointF(60.96, 30.48), false);
        QCOMPARE(int(sheet().buses.size()), buses + 1);
        QCOMPARE(sheet().buses.last().name, QStringLiteral("BUS1"));
        QCOMPARE(sheet().buses.last().points,
                 QList<Point>({Point({mmToNm(30.48), mmToNm(30.48)}),
                               Point({mmToNm(60.96), mmToNm(30.48)})}));
        QCOMPARE(int(sheet().wires.size()), wires);

        // A wire from below ends where it reaches the bus, entering it at
        // 45 degrees over its last grid step.
        window.findChild<QAction *>(QStringLiteral("actionWire"))->trigger();
        editor->press(QPointF(45.72, 40.64), false);
        editor->press(QPointF(45.72, 30.48), false);
        QCOMPARE(int(sheet().wires.size()), wires + 1);
        const QList<Point> entry = sheet().wires.last().points;
        QCOMPARE(int(entry.size()), 3);
        const Coord step = window.canvas()->gridStep();
        QCOMPARE(entry.last(), Point({mmToNm(45.72) + step, mmToNm(30.48)}));
        QCOMPARE(entry.at(1), Point({mmToNm(45.72), mmToNm(30.48) + step}));
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        QVERIFY(!editor->isWiring());

        // Its name stands above its first point.
        const QPointF name = busNameRect(sheet().buses.last(), 0).center();
        editor->doubleClick(name, false);
        QLineEdit *line = nullptr;
        for (QLineEdit *edit : window.canvas()->viewport()->findChildren<QLineEdit *>()) {
            if (edit->isVisible())
                line = edit;
        }
        QVERIFY(line != nullptr);
        QCOMPARE(line->text(), QStringLiteral("BUS1"));
        line->setText(QStringLiteral("D[0..7]"));
        QTest::keyClick(line, Qt::Key_Return);
        QCOMPARE(sheet().buses.last().name, QStringLiteral("D[0..7]"));
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

    // Two parts on the board, R1 at (0, 0) and R2 at (20, 0), pads 5 mm
    // either side: chosen by a click, with Ctrl and by a frame, dragged,
    // turned and locked, and a designator dragged alone.
    void boardPartsAreChosenMovedAndTurned()
    {
        MainWindow window;
        BoardEditor *editor = window.boardEditor();
        window.showPage(window.designItem()->boardPage());
        window.canvas()->setGridStep(NM_PER_MM);
        window.canvas()->setView(QPointF(10, 0), 20);
        Design design = window.document()->design();
        Footprint footprint;
        Pad pad;
        pad.number = QStringLiteral("1");
        pad.at = Point{-5 * NM_PER_MM, 0};
        pad.sizeX = 2 * NM_PER_MM;
        pad.sizeY = 2 * NM_PER_MM;
        pad.hole = NM_PER_MM;
        footprint.pads = {pad};
        pad.number = QStringLiteral("2");
        pad.at = Point{5 * NM_PER_MM, 0};
        footprint.pads.append(pad);
        design.library.footprints.insert(QStringLiteral("fp"), footprint);
        for (const int x : {0, 20}) {
            Placement placement;
            placement.id = newObjectId();
            placement.component = newObjectId();
            placement.footprint = QStringLiteral("fp");
            placement.designator = x == 0 ? QStringLiteral("R1") : QStringLiteral("R2");
            placement.at = Point{x * NM_PER_MM, 0};
            placement.refAt = Point{0, -3 * NM_PER_MM};
            design.board.placements.append(placement);
        }
        window.document()->change(QStringLiteral("parts"), design, window.designItem()->boardPage());
        const auto placements = [&window] { return window.document()->design().board.placements; };
        const ObjectId r1 = placements().at(0).id;
        const ObjectId r2 = placements().at(1).id;

        editor->press(QPointF(5, 0));
        editor->release(QPointF(5, 0));
        QCOMPARE(editor->chosenPlacements(), QSet<ObjectId>{r1});
        // Its designator is shown chosen with it, a line joining them.
        QCOMPARE(editor->textGuides(), QList<QLineF>{QLineF(QPointF(0, 0), QPointF(0, -3))});
        editor->press(QPointF(25, 0), true);
        editor->release(QPointF(25, 0));
        QCOMPARE(editor->chosenPlacements(), (QSet<ObjectId>{r1, r2}));
        // A click on the empty board drops them, a frame chooses R2.
        editor->press(QPointF(10, 10));
        editor->release(QPointF(10, 10));
        QVERIFY(editor->chosenPlacements().isEmpty());
        editor->press(QPointF(12, -4));
        editor->move(QPointF(28, 4));
        QVERIFY(!editor->chooserFrame().isEmpty());
        editor->release(QPointF(28, 4));
        QCOMPARE(editor->chosenPlacements(), QSet<ObjectId>{r2});

        // Dragged along the grid, as one step.
        editor->press(QPointF(25, 0));
        editor->move(QPointF(27.2, 3.1));
        editor->release(QPointF(27.2, 3.1));
        QCOMPARE(placements().at(1).at, (Point{22 * NM_PER_MM, 3 * NM_PER_MM}));
        // R turns it about its own point.
        QAction *rotate = window.findChild<QAction *>(QStringLiteral("actionRotate"));
        rotate->trigger();
        QCOMPARE(placements().at(1).rotation, 90);
        QCOMPARE(placements().at(1).at, (Point{22 * NM_PER_MM, 3 * NM_PER_MM}));
        // And while it is dragged: turned and moved as one step.
        editor->press(QPointF(22, 3));
        editor->move(QPointF(24, 3));
        rotate->trigger();
        editor->move(QPointF(26, 3));
        editor->release(QPointF(26, 3));
        QCOMPARE(placements().at(1).rotation, 180);
        QCOMPARE(placements().at(1).at, (Point{26 * NM_PER_MM, 3 * NM_PER_MM}));
        window.document()->undoStack()->undo();
        QCOMPARE(placements().at(1).rotation, 90);
        // Locked, it stays.
        QAction *lock = window.findChild<QAction *>(QStringLiteral("actionLock"));
        QVERIFY(lock->isEnabled());
        lock->trigger();
        QVERIFY(placements().at(1).locked);
        QVERIFY(lock->isChecked());
        editor->press(QPointF(22, 3));
        editor->move(QPointF(30, 3));
        editor->release(QPointF(30, 3));
        QCOMPARE(placements().at(1).at, (Point{22 * NM_PER_MM, 3 * NM_PER_MM}));
        QVERIFY(!window.statusText().isEmpty());
        // Undo unlocks it and chooses it again.
        window.document()->undoStack()->undo();
        QVERIFY(!placements().at(1).locked);
        QCOMPARE(editor->chosenPlacements(), QSet<ObjectId>{r2});

        // R1's designator, at (0, -3), taken alone: R1 stays.
        editor->press(QPointF(0, -3));
        QCOMPARE(editor->chosenText().owner, r1);
        QVERIFY(editor->chosenPlacements().isEmpty());
        // A line from R1's point to its designator shows whose it is.
        QCOMPARE(editor->textGuides(), QList<QLineF>{QLineF(QPointF(0, 0), QPointF(0, -3))});
        editor->move(QPointF(0, -6));
        editor->release(QPointF(0, -6));
        QCOMPARE(placements().at(0).refAt, (Point{0, -6 * NM_PER_MM}));
        QCOMPARE(placements().at(0).at, Point());
    }

    void boardOutlineIsEditedOnTheBoard()
    {
        MainWindow window;
        BoardEditor *editor = window.boardEditor();
        QAction *wire = window.findChild<QAction *>(QStringLiteral("actionWire"));
        QAction *cutOut = window.findChild<QAction *>(QStringLiteral("actionCutOut"));
        QAction *select = window.findChild<QAction *>(QStringLiteral("actionSelectTool"));
        // The modes of the sheet on a sheet, those of the board on the board.
        QVERIFY(wire->isVisible());
        QVERIFY(!cutOut->isVisible());
        QVERIFY(!editor->isActive());
        window.showPage(window.designItem()->boardPage());
        QVERIFY(editor->isActive());
        QVERIFY(!wire->isVisible());
        QVERIFY(cutOut->isVisible());
        QVERIFY(select->isChecked());
        // Update board from schematic shows on the strip of the board only.
        QVERIFY(window.findChild<QAction *>(QStringLiteral("actionUpdateBoardTool"))->isVisible());

        window.canvas()->setGridStep(NM_PER_MM);
        // Corners and edges are found within a few pixels: a close view.
        window.canvas()->setView(QPointF(25, 15), 20);
        editor->makeRectangleBoard(50 * NM_PER_MM, 30 * NM_PER_MM);
        const auto outline = [&window] { return window.document()->design().board.outline; };
        QCOMPARE(outline().size(), 4);

        // A notch cut in the top edge, as one step.
        cutOut->trigger();
        QVERIFY(cutOut->isChecked());
        QVERIFY(!select->isChecked());
        editor->press(QPointF(20, -5));
        editor->move(QPointF(30, 10));
        QVERIFY(!editor->cutRect().isNull());
        editor->release(QPointF(30, 10));
        QCOMPARE(outline().size(), 8);
        // One inside the board is refused and says why.
        editor->press(QPointF(5, 15));
        editor->release(QPointF(10, 20));
        QCOMPARE(outline().size(), 8);
        QVERIFY(!window.statusText().isEmpty());

        // Esc goes back to choosing; the bottom of the notch dragged deeper.
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        QCOMPARE(editor->tool(), BoardEditor::Tool::Select);
        QVERIFY(select->isChecked());
        editor->press(QPointF(25, 10));
        editor->move(QPointF(25, 15));
        QVERIFY(editor->shownBoard().outline != outline());
        editor->release(QPointF(25, 15));
        QCOMPARE(outline().at(2).at, (Point{20 * NM_PER_MM, 15 * NM_PER_MM}));
        QCOMPARE(outline().at(3).at, (Point{30 * NM_PER_MM, 15 * NM_PER_MM}));

        window.document()->undoStack()->undo();
        QCOMPARE(outline().at(2).at, (Point{20 * NM_PER_MM, 10 * NM_PER_MM}));
        window.document()->undoStack()->undo();
        QCOMPARE(outline().size(), 4);

        // An outline drawn corner by corner; the first corner closes it only
        // once there are three.
        window.findChild<QAction *>(QStringLiteral("actionDrawOutline"))->trigger();
        QCOMPARE(editor->tool(), BoardEditor::Tool::Outline);
        editor->press(QPointF(0, 0));
        editor->press(QPointF(10, 0));
        editor->press(QPointF(0, 0));
        QCOMPARE(outline().size(), 4);
        QVERIFY(editor->isDrawing());
        editor->press(QPointF(10, 10));
        editor->press(QPointF(0, 10));
        editor->press(QPointF(0, 0));
        QVERIFY(!editor->isDrawing());
        QCOMPARE(editor->tool(), BoardEditor::Tool::Select);
        QCOMPARE(outline(), rectangleOutline(Point(), 10 * NM_PER_MM, 10 * NM_PER_MM));

        // A double click on a side chooses the outline whole; locked, it
        // stays where it is; unlocked, a drag moves it all.
        QAction *lock = window.findChild<QAction *>(QStringLiteral("actionLock"));
        QVERIFY(!lock->isEnabled());
        editor->doubleClick(QPointF(5, 0));
        QVERIFY(editor->hasChosen());
        QVERIFY(lock->isEnabled());
        lock->trigger();
        QVERIFY(window.document()->design().board.outlineLocked);
        QVERIFY(lock->isChecked());
        QVERIFY(!cutOut->isEnabled());
        const QList<OutlineCorner> square = outline();
        editor->press(QPointF(5, 0));
        editor->move(QPointF(7, 3));
        editor->release(QPointF(7, 3));
        QCOMPARE(outline(), square);
        lock->trigger();
        QVERIFY(!window.document()->design().board.outlineLocked);
        editor->press(QPointF(5, 0));
        editor->move(QPointF(7, 3));
        editor->release(QPointF(7, 3));
        QCOMPARE(outline().first().at, (Point{2 * NM_PER_MM, 3 * NM_PER_MM}));
        // Esc lets it go.
        window.findChild<QAction *>(QStringLiteral("actionDeselect"))->trigger();
        QVERIFY(!editor->hasChosen());

        // A sheet again: the board's modes step aside.
        window.showPage(0);
        QVERIFY(!editor->isActive());
        QVERIFY(wire->isVisible());
        QVERIFY(!cutOut->isVisible());
        QVERIFY(!window.findChild<QAction *>(QStringLiteral("actionUpdateBoardTool"))->isVisible());
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
        const QString message = window.statusText();
        QVERIFY2(message.contains(QStringLiteral("diode-7.62, diode-10.16")), qPrintable(message));
    }
};

QTEST_MAIN(TestSmoke)
#include "test_smoke.moc"
