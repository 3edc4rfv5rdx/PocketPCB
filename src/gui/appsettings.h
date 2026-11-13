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

    // Folder the last library file was opened from; empty before the first one.
    static QString lastLibraryFolder();
    static void setLastLibraryFolder(const QString &folder);

    static QByteArray windowGeometry();
    static void setWindowGeometry(const QByteArray &geometry);

    static QByteArray windowState();
    static void setWindowState(const QByteArray &state);
};

} // namespace ppcb
