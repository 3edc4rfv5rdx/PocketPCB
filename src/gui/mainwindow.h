#pragma once

#include "appsettings.h"
#include "units.h"

#include <QMainWindow>

class QActionGroup;
class QFileSystemWatcher;
class QTimer;
class QComboBox;
class QLabel;
class QMenu;

namespace ppcb {

class CanvasView;
class LibraryViewItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    CanvasView *canvas() const { return m_canvas; }

    // Shows a symbol or footprint file of the library on the canvas and
    // keeps watching it: the drawing follows every save of the file.
    // Returns false, with the reason in the status bar, when the file cannot
    // be read; what was shown before stays.
    bool openLibraryFile(const QString &path);
    // Reads the shown file again. An unreadable file keeps the last drawing
    // on screen and puts the reason into the status bar.
    bool reloadLibraryFile();
    // Path of the shown library file; empty when there is none.
    QString libraryFilePath() const { return m_libraryPath; }
    LibraryViewItem *libraryItem() const { return m_libraryItem; }

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createStatusBar();
    // Adds a checkable, mutually exclusive item to a menu.
    QAction *addChoice(QMenu *menu, QActionGroup *group, const QString &text, bool checked);

    void applyGridStyle(GridStyle style);
    void applyCursorStyle(CursorStyle style);
    void applyZoomCenterAndWarp(bool on);

    void chooseLibraryFile();
    // Reads 'path' and puts it on the canvas; the common part of opening
    // and reloading.
    bool showLibraryFile(const QString &path);
    void watchLibraryFile();

    void showCursorPosition(Coord x, Coord y);
    void showGridStep(Coord step);

    CanvasView *m_canvas = nullptr;
    QLabel *m_positionLabel = nullptr;
    QComboBox *m_gridStepBox = nullptr;

    QString m_libraryPath;
    // Owned by the canvas scene.
    LibraryViewItem *m_libraryItem = nullptr;
    QFileSystemWatcher *m_libraryWatcher = nullptr;
    // Delays the reload a little: editors often save in several steps.
    QTimer *m_reloadTimer = nullptr;
};

} // namespace ppcb
