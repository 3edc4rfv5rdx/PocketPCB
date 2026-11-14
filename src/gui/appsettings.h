#pragma once

#include <QByteArray>
#include <QString>

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

    // true: the point under the cursor is moved to the view centre on zoom
    // and the mouse cursor follows it; false: the point stays where it is.
    static bool zoomCenterAndWarp();
    static void setZoomCenterAndWarp(bool on);

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

    // Folder the last library file was opened from; empty before the first one.
    static QString lastLibraryFolder();
    static void setLastLibraryFolder(const QString &folder);

    // Folder that holds the folder of the last project opened or saved.
    static QString lastProjectFolder();
    static void setLastProjectFolder(const QString &folder);

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
