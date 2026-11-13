#include "appsettings.h"

#include "version.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

namespace ppcb {

namespace {

// Names the settings and data folders: ~/.config/PocketPCB, ~/.local/share/PocketPCB.
const char APP_NAME[] = "PocketPCB";
const char SETTINGS_FILE[] = "pocketpcb.ini";

const char KEY_LANGUAGE[] = "general/language";
const char KEY_GRID_STYLE[] = "view/gridStyle";
const char KEY_CURSOR_STYLE[] = "view/cursorStyle";
const char KEY_ZOOM_WARP[] = "view/zoomCenterAndWarp";
const char KEY_LAST_LIBRARY_FOLDER[] = "library/lastFolder";
const char KEY_LAST_PROJECT_FOLDER[] = "project/lastFolder";
const char KEY_WINDOW_GEOMETRY[] = "window/geometry";
const char KEY_WINDOW_STATE[] = "window/state";

const char GRID_LINES[] = "lines";
const char GRID_DOTS[] = "dots";

const char CURSOR_ARROW[] = "arrow";
const char CURSOR_SMALL_CROSS[] = "smallCross";
const char CURSOR_FULL_CROSS[] = "fullCross";

QVariant readValue(const char *key, const QVariant &defaultValue)
{
    QSettings settings(AppSettings::filePath(), QSettings::IniFormat);
    return settings.value(QLatin1String(key), defaultValue);
}

void writeValue(const char *key, const QVariant &value)
{
    QSettings settings(AppSettings::filePath(), QSettings::IniFormat);
    settings.setValue(QLatin1String(key), value);
}

} // namespace

void AppSettings::initApplication()
{
    // No organization name on purpose: it would add a folder level to the
    // configuration and data paths (~/.config/<org>/PocketPCB).
    QCoreApplication::setApplicationName(QLatin1String(APP_NAME));
    QCoreApplication::setApplicationVersion(QLatin1String(appVersion()));
}

QString AppSettings::filePath()
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(folder).filePath(QLatin1String(SETTINGS_FILE));
}

QString AppSettings::language()
{
    return readValue(KEY_LANGUAGE, QString()).toString();
}

void AppSettings::setLanguage(const QString &language)
{
    writeValue(KEY_LANGUAGE, language);
}

GridStyle AppSettings::gridStyle()
{
    const QString text = readValue(KEY_GRID_STYLE, QLatin1String(GRID_LINES)).toString();
    if (text == QLatin1String(GRID_DOTS))
        return GridStyle::Dots;
    return GridStyle::Lines;
}

void AppSettings::setGridStyle(GridStyle style)
{
    const char *text = GRID_LINES;
    if (style == GridStyle::Dots)
        text = GRID_DOTS;
    writeValue(KEY_GRID_STYLE, QLatin1String(text));
}

CursorStyle AppSettings::cursorStyle()
{
    const QString text = readValue(KEY_CURSOR_STYLE, QLatin1String(CURSOR_SMALL_CROSS)).toString();
    if (text == QLatin1String(CURSOR_ARROW))
        return CursorStyle::Arrow;
    if (text == QLatin1String(CURSOR_FULL_CROSS))
        return CursorStyle::FullCross;
    return CursorStyle::SmallCross;
}

void AppSettings::setCursorStyle(CursorStyle style)
{
    const char *text = CURSOR_SMALL_CROSS;
    if (style == CursorStyle::Arrow)
        text = CURSOR_ARROW;
    else if (style == CursorStyle::FullCross)
        text = CURSOR_FULL_CROSS;
    writeValue(KEY_CURSOR_STYLE, QLatin1String(text));
}

bool AppSettings::zoomCenterAndWarp()
{
    return readValue(KEY_ZOOM_WARP, true).toBool();
}

void AppSettings::setZoomCenterAndWarp(bool on)
{
    writeValue(KEY_ZOOM_WARP, on);
}

QString AppSettings::lastLibraryFolder()
{
    return readValue(KEY_LAST_LIBRARY_FOLDER, QString()).toString();
}

void AppSettings::setLastLibraryFolder(const QString &folder)
{
    writeValue(KEY_LAST_LIBRARY_FOLDER, folder);
}

QString AppSettings::lastProjectFolder()
{
    return readValue(KEY_LAST_PROJECT_FOLDER, QString()).toString();
}

void AppSettings::setLastProjectFolder(const QString &folder)
{
    writeValue(KEY_LAST_PROJECT_FOLDER, folder);
}

QByteArray AppSettings::windowGeometry()
{
    return readValue(KEY_WINDOW_GEOMETRY, QByteArray()).toByteArray();
}

void AppSettings::setWindowGeometry(const QByteArray &geometry)
{
    writeValue(KEY_WINDOW_GEOMETRY, geometry);
}

QByteArray AppSettings::windowState()
{
    return readValue(KEY_WINDOW_STATE, QByteArray()).toByteArray();
}

void AppSettings::setWindowState(const QByteArray &state)
{
    writeValue(KEY_WINDOW_STATE, state);
}

} // namespace ppcb
