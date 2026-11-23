#pragma once

#include "wiring.h"

#include <QByteArray>
#include <QColor>
#include <QString>
#include <QStringList>

namespace ppcb {

enum class GridStyle { Lines, Dots };
enum class CursorStyle { Arrow, SmallCross, FullCross };

// Program-wide settings (not project settings), kept in an INI file in the
// user's configuration folder. Every key of that file is named only here.
class AppSettings
{
public:
    // Sets the application name and version. Call once, before anything
    // that reads settings or standard paths.
    static void initApplication();

    static QString filePath();

    // Interface language code ("en", "uk", "ru"); empty: follow the system.
    static QString language();
    static void setLanguage(const QString &language);

    static GridStyle gridStyle();
    static void setGridStyle(GridStyle style);

    static CursorStyle cursorStyle();
    static void setCursorStyle(CursorStyle style);

    // The paper of the schematic sheets; white by default. Not kept in the
    // project: each user sees their own.
    static QColor sheetBackground();
    static void setSheetBackground(const QColor &color);
    // The pale colours offered for it, white first.
    static QList<QColor> sheetBackgrounds();

    // The dots of the screen in a millimetre, as set in Settings > Screen
    // size; 0 (by default): as the screen reports its size.
    static double screenDotsPerMm();
    static void setScreenDotsPerMm(double dots);

    // How wires are laid again when what they are joined to goes; on the
    // grid by default.
    static WireRouting wireRouting();
    static void setWireRouting(WireRouting routing);

    // true: the point under the cursor is moved to the view centre on zoom
    // and the mouse cursor follows it; false: the point stays where it is.
    static bool zoomCenterAndWarp();
    static void setZoomCenterAndWarp(bool on);

    // Whether the coordinates of the corner being drawn on the board are
    // shown beside it; on by default.
    static bool drawingCoordinatesShown();
    static void setDrawingCoordinatesShown(bool on);

    // Whether the lines of the ratsnest are drawn as gentle arcs instead of
    // straight; straight by default.
    static bool ratsnestArcs();
    static void setRatsnestArcs(bool on);

    // Whether every step made in a saved project is written into its history
    // folder (DesignFiles::historyPath()), for finding out what went wrong in
    // one (see writeStep()); off by default.
    static bool keepStepHistory();
    static void setKeepStepHistory(bool on);

    // The user's library: a folder with symbols/, footprints/ and parts/.
    // By default "library" in the user's data folder; set in the file by
    // hand for now, and the first read writes the default there.
    static QString userLibraryFolder();
    // Makes the user's library folder and its three folders when missing.
    static bool makeUserLibraryFolders(QString &error);
    // The built-in library, read only: beside the program in an AppImage
    // (<program folder>/../share/pocketpcb/library), else the one in the
    // source tree the program was built from.
    static QString builtInLibraryFolder();
    // The user guide, help.html: beside the program in an AppImage, as the
    // built-in library is, else the one in the source tree.
    static QString helpFile();

    // Folder the last library file was opened from; empty before the first one.
    static QString lastLibraryFolder();
    static void setLastLibraryFolder(const QString &folder);

    // Folder that holds the folder of the last project opened or saved.
    static QString lastProjectFolder();
    static void setLastProjectFolder(const QString &folder);
    // The common folder of the project folders: "PocketPCB-Projects" beside the
    // program, or beside the AppImage file it runs from, unless another is
    // chosen. Only a chosen folder is written into the settings file, so the
    // default follows the program when it is moved; the program makes the
    // folder when it starts.
    static QString projectsFolder();
    static void setProjectsFolder(const QString &folder);
    // Where the dialogs that open and save projects start: the folder of the
    // last project, else the projects folder, made when missing.
    static QString projectDialogFolder();

    // Project files opened or saved lately, the latest first, at most
    // RECENT_PROJECTS_MAX of them.
    static const int RECENT_PROJECTS_MAX = 10;
    static QStringList recentProjects();
    // Puts 'path' first, once; the oldest beyond the limit drops out.
    static void addRecentProject(const QString &path);
    static void removeRecentProject(const QString &path);
    static void clearRecentProjects();

    // How many earlier versions of each project file a save keeps (see
    // Document::setBackupCount()). Set in the file by hand for now; the
    // first read writes the default there.
    static int backupCount();
    // How often the open project is autosaved, in minutes; 0: never. Set in
    // the file by hand for now; the first read writes the default there.
    static int autosaveMinutes();

    static QByteArray windowGeometry();
    static void setWindowGeometry(const QByteArray &geometry);

    static QByteArray windowState();
    static void setWindowState(const QByteArray &state);
};

} // namespace ppcb
