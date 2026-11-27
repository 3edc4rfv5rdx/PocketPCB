#include "appsettings.h"

#include "document.h"
#include "library.h"
#include "paint.h"
#include "version.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QVariant>

#include <algorithm>

namespace ppcb {

namespace {

// The name of the program, and of the folders an earlier version kept the
// user's things in: ~/.config/PocketPCB, ~/.local/share/PocketPCB and
// ~/.cache/PocketPCB.
const char APP_NAME[] = "PocketPCB";
// Every path of the user's own is here: the folder in the home folder that
// holds them all, the settings file in it, and its file in those old folders.
const char HOME_FOLDER[] = ".PocketPCB";
const char SETTINGS_FILE[] = "settings.ini";
const char OLD_SETTINGS_FILE[] = "pocketpcb.ini";

// Not in a group "general": an INI file writes it as [%General] and reads
// it back as "General", so the key would be lost after a restart.
const char KEY_LANGUAGE[] = "interface/language";
const char KEY_GRID_STYLE[] = "view/gridStyle";
const char KEY_CURSOR_STYLE[] = "view/cursorStyle";
const char KEY_ZOOM_WARP[] = "view/zoomCenterAndWarp";
const char KEY_DRAWING_COORDINATES[] = "view/drawingCoordinates";
const char KEY_RATSNEST_ARCS[] = "view/ratsnestArcs";
const char KEY_SCREEN_DOTS[] = "view/screenDotsPerMm";
const char KEY_WIRE_ROUTING[] = "schematic/wireRouting";
const char KEY_STEP_HISTORY[] = "debug/keepStepHistory";
const char KEY_SHEET_BACKGROUND[] = "view/sheetBackground";
const char KEY_LAST_LIBRARY_FOLDER[] = "library/lastFolder";
const char KEY_USER_LIBRARY_FOLDER[] = "library/folder";
const char KEY_LAST_PROJECT_FOLDER[] = "project/lastFolder";
const char KEY_PROJECTS_FOLDER[] = "project/projectsFolder";
const char KEY_RECENT_PROJECTS[] = "project/recent";
const char KEY_HIDDEN_WARNINGS[] = "view/hiddenWarnings";
const char KEY_BACKUP_COUNT[] = "project/backupCount";
const char KEY_AUTOSAVE_MINUTES[] = "project/autosaveMinutes";

const int DEFAULT_AUTOSAVE_MINUTES = 5;

// Inside the home folder of the program (AppSettings::homeFolder()).
const char USER_LIBRARY_FOLDER[] = "library";
// What the name of an export of it starts with, the day following.
const char LIBRARY_ARCHIVE_PREFIX[] = "pocketpcb-library-";
// Beside the program (programFolder()).
const char PROJECTS_FOLDER[] = "PocketPCB-Projects";
// Names the AppImage file the program runs from.
const char APPIMAGE_VARIABLE[] = "APPIMAGE";
// Where an AppImage keeps the built-in library, from the program's folder.
const char INSTALLED_LIBRARY_FOLDER[] = "../share/pocketpcb/library";
// The same for the user guide.
const char INSTALLED_HELP_FILE[] = "../share/pocketpcb/help.html";
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

// The folder the program is in, or the AppImage file it runs from: an
// AppImage's own folder cannot be written.
QString programFolder()
{
    const QString appImage = qEnvironmentVariable(APPIMAGE_VARIABLE);
    return appImage.isEmpty() ? QCoreApplication::applicationDirPath()
                              : QFileInfo(appImage).absolutePath();
}

void writeValue(const char *key, const QVariant &value)
{
    QSettings settings(AppSettings::filePath(), QSettings::IniFormat);
    settings.setValue(QLatin1String(key), value);
}

QString defaultLibraryFolder()
{
    return QDir(AppSettings::homeFolder()).filePath(QLatin1String(USER_LIBRARY_FOLDER));
}

// True when 'path' is 'folder' or inside it.
bool isInside(const QString &path, const QString &folder)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanFolder = QDir::cleanPath(folder);
    return cleanPath == cleanFolder || cleanPath.startsWith(cleanFolder + QLatin1Char('/'));
}

// Copies the folder 'from' with all it holds to 'to', which must not be
// there yet.
bool copyFolder(const QString &from, const QString &to)
{
    const QDir source(from);
    if (!QDir().mkpath(to))
        return false;
    const QFileInfoList entries =
        source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &entry : entries) {
        const QString target = QDir(to).filePath(entry.fileName());
        const bool copied = entry.isDir() ? copyFolder(entry.filePath(), target)
                                          : QFile::copy(entry.filePath(), target);
        if (!copied)
            return false;
    }
    return true;
}

// Moves the file or folder 'from' to 'to': renamed, or across disks copied
// and the original removed.
bool moveEntry(const QString &from, const QString &to)
{
    if (QDir().rename(from, to))
        return true;
    if (QFileInfo(from).isDir()) {
        if (!copyFolder(from, to))
            return false;
        QDir(from).removeRecursively();
        return true;
    }
    return QFile::copy(from, to) && QFile::remove(from);
}

} // namespace

void AppSettings::initApplication()
{
    // No organization name on purpose: it would add a folder level to the
    // configuration and data paths (~/.config/<org>/PocketPCB).
    QCoreApplication::setApplicationName(QLatin1String(APP_NAME));
    QCoreApplication::setApplicationVersion(QLatin1String(appVersion()));
}

QString AppSettings::homeFolder()
{
    // Tests keep off the user's own.
    const QString home = QStandardPaths::isTestModeEnabled()
                             ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                             : QDir::homePath();
    return QDir(home).filePath(QLatin1String(HOME_FOLDER));
}

QString AppSettings::filePath()
{
    return QDir(homeFolder()).filePath(QLatin1String(SETTINGS_FILE));
}

AppSettings::OldFolders AppSettings::moveOldFolders(QString &error)
{
    if (QStandardPaths::isTestModeEnabled())
        return OldFolders::None;
    return moveFolders(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation),
                       QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
                       QStandardPaths::writableLocation(QStandardPaths::CacheLocation), error);
}

AppSettings::OldFolders AppSettings::moveFolders(const QString &oldConfig, const QString &oldData,
                                                 const QString &oldCache, QString &error)
{
    const QString home = homeFolder();
    if (QFileInfo::exists(home))
        return OldFolders::None;
    const QString oldSettings = QDir(oldConfig).filePath(QLatin1String(OLD_SETTINGS_FILE));
    const QString oldLibrary = QDir(oldData).filePath(QLatin1String(USER_LIBRARY_FOLDER));
    const QList<QString> oldFolders = {oldConfig, oldData, oldCache};
    if (std::none_of(oldFolders.cbegin(), oldFolders.cend(),
                     [](const QString &folder) { return QFileInfo::exists(folder); }))
        return OldFolders::None;
    if (!QDir().mkpath(home)) {
        error = home;
        return OldFolders::Failed;
    }
    // The library first: when it cannot be moved, nothing is, and the next
    // start tries again.
    if (QFileInfo(oldLibrary).isDir() && !moveEntry(oldLibrary, defaultLibraryFolder())) {
        QDir().rmdir(home);
        error = oldLibrary;
        return OldFolders::Failed;
    }
    if (QFileInfo::exists(oldSettings)) {
        if (!moveEntry(oldSettings, filePath())) {
            error = oldSettings;
            return OldFolders::Failed;
        }
        // The library the settings name, when it was the old default.
        QSettings settings(filePath(), QSettings::IniFormat);
        const QString library = settings.value(QLatin1String(KEY_USER_LIBRARY_FOLDER)).toString();
        if (!library.isEmpty() && isInside(library, oldLibrary))
            settings.setValue(QLatin1String(KEY_USER_LIBRARY_FOLDER), defaultLibraryFolder());
    }
    // The old folders go, but one the settings still keep the library in.
    const QString library = userLibraryFolder();
    for (const QString &folder : oldFolders) {
        if (QFileInfo(folder).fileName() == QLatin1String(APP_NAME) && !isInside(library, folder))
            QDir(folder).removeRecursively();
    }
    return OldFolders::Moved;
}

Alert AppSettings::oldFoldersAlert(OldFolders result, const QString &error)
{
    switch (result) {
    case OldFolders::None:
        break;
    case OldFolders::Moved:
        return Alert::warning(
            QCoreApplication::translate("ppcb::AppSettings",
                                        "Your settings and library were moved to %1, the old "
                                        "folders removed")
                .arg(QDir::toNativeSeparators(homeFolder())),
            QStringLiteral("oldFoldersMoved"));
    case OldFolders::Failed:
        return Alert::error(QCoreApplication::translate("ppcb::AppSettings",
                                                        "Your settings and library could not be "
                                                        "moved to %1")
                                .arg(QDir::toNativeSeparators(homeFolder()))
                            + QStringLiteral(": ") + QDir::toNativeSeparators(error));
    }
    return Alert::info(QString());
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

double AppSettings::screenDotsPerMm()
{
    return readValue(KEY_SCREEN_DOTS, 0.0).toDouble();
}

void AppSettings::setScreenDotsPerMm(double dots)
{
    writeValue(KEY_SCREEN_DOTS, dots);
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

bool AppSettings::drawingCoordinatesShown()
{
    return readValue(KEY_DRAWING_COORDINATES, true).toBool();
}

void AppSettings::setDrawingCoordinatesShown(bool on)
{
    writeValue(KEY_DRAWING_COORDINATES, on);
}

bool AppSettings::ratsnestArcs()
{
    return readValue(KEY_RATSNEST_ARCS, false).toBool();
}

void AppSettings::setRatsnestArcs(bool on)
{
    writeValue(KEY_RATSNEST_ARCS, on);
}

bool AppSettings::keepStepHistory()
{
    return readValue(KEY_STEP_HISTORY, false).toBool();
}

void AppSettings::setKeepStepHistory(bool on)
{
    writeValue(KEY_STEP_HISTORY, on);
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
    const QString byDefault = defaultLibraryFolder();
    const QString folder = readValueOrWriteDefault(KEY_USER_LIBRARY_FOLDER, byDefault).toString();
    return folder.isEmpty() ? byDefault : folder;
}

QString AppSettings::userLibraryArchiveFolder()
{
    const QString folder = QDir::cleanPath(userLibraryFolder());
    // The real home folder, or the test one.
    const QString home = QFileInfo(homeFolder()).absolutePath();
    if (isInside(folder, home) && folder != QDir::cleanPath(home))
        return QDir(home).relativeFilePath(folder);
    return QFileInfo(folder).fileName();
}

QString AppSettings::libraryArchiveName(const QDate &day)
{
    return QLatin1String(LIBRARY_ARCHIVE_PREFIX) + day.toString(QStringLiteral("yyyyMMdd"))
           + QLatin1String(".zip");
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

QString AppSettings::helpFile()
{
    const QString installed = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QLatin1String(INSTALLED_HELP_FILE));
    if (QFileInfo(installed).isFile())
        return QDir::cleanPath(installed);
    return QStringLiteral(PPCB_SOURCE_HELP_FILE);
}

QString AppSettings::lastProjectFolder()
{
    return readValue(KEY_LAST_PROJECT_FOLDER, QString()).toString();
}

void AppSettings::setLastProjectFolder(const QString &folder)
{
    writeValue(KEY_LAST_PROJECT_FOLDER, folder);
}

QString AppSettings::projectsFolder()
{
    const QString byDefault = QDir(programFolder()).filePath(QLatin1String(PROJECTS_FOLDER));
    const QString folder = readValue(KEY_PROJECTS_FOLDER, QString()).toString();
    return folder.isEmpty() ? byDefault : folder;
}

void AppSettings::setProjectsFolder(const QString &folder)
{
    writeValue(KEY_PROJECTS_FOLDER, folder);
}

QString AppSettings::projectDialogFolder()
{
    const QString last = lastProjectFolder();
    if (!last.isEmpty() && QFileInfo(last).isDir())
        return last;
    const QString projects = projectsFolder();
    QDir().mkpath(projects);
    return projects;
}

QStringList AppSettings::hiddenWarnings()
{
    QStringList keys = readValue(KEY_HIDDEN_WARNINGS, QStringList()).toStringList();
    keys.removeAll(QString());
    return keys;
}

void AppSettings::setHiddenWarnings(const QStringList &keys)
{
    writeValue(KEY_HIDDEN_WARNINGS, keys);
}

QStringList AppSettings::recentProjects()
{
    // An empty list may come back from the file as one empty string.
    QStringList paths = readValue(KEY_RECENT_PROJECTS, QStringList()).toStringList();
    paths.removeAll(QString());
    // A list kept when more were.
    return paths.mid(0, RECENT_PROJECTS_MAX);
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
