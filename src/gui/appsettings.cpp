#include "appsettings.h"

#include "document.h"
#include "library.h"
#include "paint.h"
#include "version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

namespace ppcb {

namespace {

// Names the settings and data folders: ~/.config/PocketPCB, ~/.local/share/PocketPCB.
const char APP_NAME[] = "PocketPCB";
const char SETTINGS_FILE[] = "pocketpcb.ini";

// Not in a group "general": an INI file writes it as [%General] and reads
// it back as "General", so the key would be lost after a restart.
const char KEY_LANGUAGE[] = "interface/language";
const char KEY_GRID_STYLE[] = "view/gridStyle";
const char KEY_CURSOR_STYLE[] = "view/cursorStyle";
const char KEY_ZOOM_WARP[] = "view/zoomCenterAndWarp";
const char KEY_WIRE_ROUTING[] = "schematic/wireRouting";
const char KEY_STEP_HISTORY[] = "debug/keepStepHistory";
const char KEY_SHEET_BACKGROUND[] = "view/sheetBackground";
const char KEY_LAST_LIBRARY_FOLDER[] = "library/lastFolder";
const char KEY_USER_LIBRARY_FOLDER[] = "library/folder";
const char KEY_LAST_PROJECT_FOLDER[] = "project/lastFolder";
const char KEY_RECENT_PROJECTS[] = "project/recent";
const char KEY_BACKUP_COUNT[] = "project/backupCount";
const char KEY_AUTOSAVE_MINUTES[] = "project/autosaveMinutes";

const int DEFAULT_AUTOSAVE_MINUTES = 5;

// Inside the user's data folder.
const char USER_LIBRARY_FOLDER[] = "library";
// Beside the program.
const char STEP_HISTORY_FOLDER[] = "history";
// Where an AppImage keeps the built-in library, from the program's folder.
const char INSTALLED_LIBRARY_FOLDER[] = "../share/pocketpcb/library";
const char KEY_WINDOW_GEOMETRY[] = "window/geometry";
const char KEY_WINDOW_STATE[] = "window/state";

const char GRID_LINES[] = "lines";
const char GRID_DOTS[] = "dots";

const char CURSOR_ARROW[] = "arrow";
const char CURSOR_SMALL_CROSS[] = "smallCross";
const char CURSOR_FULL_CROSS[] = "fullCross";

const char ROUTING_HAND[] = "hand";
const char ROUTING_GRID[] = "grid";

QVariant readValue(const char *key, const QVariant &defaultValue)
{
    QSettings settings(AppSettings::filePath(), QSettings::IniFormat);
    return settings.value(QLatin1String(key), defaultValue);
}

// The same, but a key that is not in the file yet is written there with the
// default value, so the file shows what can be set in it by hand.
QVariant readValueOrWriteDefault(const char *key, const QVariant &defaultValue)
{
    QSettings settings(AppSettings::filePath(), QSettings::IniFormat);
    if (!settings.contains(QLatin1String(key)))
        settings.setValue(QLatin1String(key), defaultValue);
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

QColor AppSettings::sheetBackground()
{
    const QColor color(
        readValue(KEY_SHEET_BACKGROUND, paintcolor::background().name()).toString());
    return color.isValid() ? color : paintcolor::background();
}

void AppSettings::setSheetBackground(const QColor &color)
{
    writeValue(KEY_SHEET_BACKGROUND, color.name());
}

QList<QColor> AppSettings::sheetBackgrounds()
{
    return {paintcolor::background(), QColor(0xFF, 0xFE, 0xF2), QColor(0xFB, 0xF7, 0xE9),
            QColor(0xFF, 0xFB, 0xDC), QColor(0xF1, 0xF8, 0xEC), QColor(0xEC, 0xF7, 0xF3),
            QColor(0xED, 0xF3, 0xFA), QColor(0xF3, 0xF3, 0xF3), QColor(0xF3, 0xF0, 0xF9),
            QColor(0xFB, 0xF0, 0xEF)};
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

WireRouting AppSettings::wireRouting()
{
    const QString text = readValue(KEY_WIRE_ROUTING, QLatin1String(ROUTING_GRID)).toString();
    if (text == QLatin1String(ROUTING_HAND))
        return WireRouting::Hand;
    return WireRouting::Grid;
}

void AppSettings::setWireRouting(WireRouting routing)
{
    const char *text = ROUTING_GRID;
    if (routing == WireRouting::Hand)
        text = ROUTING_HAND;
    writeValue(KEY_WIRE_ROUTING, QLatin1String(text));
}

bool AppSettings::zoomCenterAndWarp()
{
    return readValue(KEY_ZOOM_WARP, true).toBool();
}

void AppSettings::setZoomCenterAndWarp(bool on)
{
    writeValue(KEY_ZOOM_WARP, on);
}

bool AppSettings::keepStepHistory()
{
    return readValue(KEY_STEP_HISTORY, false).toBool();
}

void AppSettings::setKeepStepHistory(bool on)
{
    writeValue(KEY_STEP_HISTORY, on);
}

QString AppSettings::stepHistoryFolder()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QLatin1String(STEP_HISTORY_FOLDER));
}

QString AppSettings::lastLibraryFolder()
{
    return readValue(KEY_LAST_LIBRARY_FOLDER, QString()).toString();
}

void AppSettings::setLastLibraryFolder(const QString &folder)
{
    writeValue(KEY_LAST_LIBRARY_FOLDER, folder);
}

QString AppSettings::userLibraryFolder()
{
    const QString dataFolder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString byDefault = QDir(dataFolder).filePath(QLatin1String(USER_LIBRARY_FOLDER));
    const QString folder = readValueOrWriteDefault(KEY_USER_LIBRARY_FOLDER, byDefault).toString();
    return folder.isEmpty() ? byDefault : folder;
}

bool AppSettings::makeUserLibraryFolders(QString &error)
{
    const QDir library(userLibraryFolder());
    for (const char *kind :
         {LIBRARY_SYMBOLS_FOLDER, LIBRARY_FOOTPRINTS_FOLDER, LIBRARY_PARTS_FOLDER}) {
        if (!JsonFile::makeFolder(library.filePath(QLatin1String(kind)), error))
            return false;
    }
    return true;
}

QString AppSettings::builtInLibraryFolder()
{
    const QString installed = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QLatin1String(INSTALLED_LIBRARY_FOLDER));
    if (QFileInfo(installed).isDir())
        return QDir::cleanPath(installed);
    return QStringLiteral(PPCB_SOURCE_LIBRARY_DIR);
}

QString AppSettings::lastProjectFolder()
{
    return readValue(KEY_LAST_PROJECT_FOLDER, QString()).toString();
}

void AppSettings::setLastProjectFolder(const QString &folder)
{
    writeValue(KEY_LAST_PROJECT_FOLDER, folder);
}

QStringList AppSettings::recentProjects()
{
    // An empty list may come back from the file as one empty string.
    QStringList paths = readValue(KEY_RECENT_PROJECTS, QStringList()).toStringList();
    paths.removeAll(QString());
    return paths;
}

void AppSettings::addRecentProject(const QString &path)
{
    const QString file = QFileInfo(path).absoluteFilePath();
    QStringList paths = recentProjects();
    paths.removeAll(file);
    paths.prepend(file);
    writeValue(KEY_RECENT_PROJECTS, paths.mid(0, RECENT_PROJECTS_MAX));
}

void AppSettings::removeRecentProject(const QString &path)
{
    QStringList paths = recentProjects();
    paths.removeAll(QFileInfo(path).absoluteFilePath());
    writeValue(KEY_RECENT_PROJECTS, paths);
}

void AppSettings::clearRecentProjects()
{
    writeValue(KEY_RECENT_PROJECTS, QStringList());
}

int AppSettings::autosaveMinutes()
{
    bool ok = false;
    const int minutes = readValueOrWriteDefault(KEY_AUTOSAVE_MINUTES, DEFAULT_AUTOSAVE_MINUTES).toInt(&ok);
    return ok && minutes >= 0 ? minutes : DEFAULT_AUTOSAVE_MINUTES;
}

int AppSettings::backupCount()
{
    bool ok = false;
    const int count = readValueOrWriteDefault(KEY_BACKUP_COUNT, Document::DEFAULT_BACKUP_COUNT).toInt(&ok);
    return ok && count >= 0 ? count : Document::DEFAULT_BACKUP_COUNT;
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
