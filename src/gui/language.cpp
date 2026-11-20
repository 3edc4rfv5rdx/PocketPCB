#include "language.h"

#include "appsettings.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

// Resources of a static library are not registered by themselves on every
// toolchain. Q_INIT_RESOURCE must be used outside any namespace.
static void initTranslationResources()
{
    Q_INIT_RESOURCE(pocketpcb_gui_translations);
}

namespace ppcb {

namespace {

// "ru-RU", "ru_RU", " RU " -> "ru"
QString languagePart(const QString &name)
{
    QString part = name.trimmed().toLower();
    part.replace(QLatin1Char('_'), QLatin1Char('-'));
    const int cut = part.indexOf(QLatin1Char('-'));
    if (cut >= 0)
        part.truncate(cut);
    return part;
}

} // namespace

QStringList supportedLanguages()
{
    return {QStringLiteral("en"), QStringLiteral("uk"), QStringLiteral("ru")};
}

QString languageName(const QString &code)
{
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    if (code == QLatin1String("uk"))
        return QStringLiteral("Українська");
    if (code == QLatin1String("ru"))
        return QStringLiteral("Русский");
    return code;
}

QString chooseLanguage(const QString &preferred, const QStringList &systemLanguages)
{
    const QStringList supported = supportedLanguages();

    const QString wanted = languagePart(preferred);
    if (supported.contains(wanted))
        return wanted;

    for (const QString &name : systemLanguages) {
        const QString part = languagePart(name);
        if (supported.contains(part))
            return part;
    }
    return supported.first();
}

bool loadTranslation(QTranslator &translator, const QString &language)
{
    initTranslationResources();
    return translator.load(QStringLiteral(":/i18n/pocketpcb_%1.qm").arg(language));
}

QString currentLanguage()
{
    return chooseLanguage(AppSettings::language(), QLocale::system().uiLanguages());
}

QStringList nameLanguages()
{
    QStringList languages = {currentLanguage(), supportedLanguages().first()};
    languages.removeDuplicates();
    return languages;
}

QString installLanguage()
{
    const QString language = currentLanguage();
    QTranslator *translator = new QTranslator(QCoreApplication::instance());
    if (loadTranslation(*translator, language))
        QCoreApplication::installTranslator(translator);
    else
        delete translator;
    // Qt's own words (OK, Cancel, the file dialog) come from Qt's translations
    // folder: the system's, or the one packed into the AppImage beside Qt.
    QTranslator *qtTranslator = new QTranslator(QCoreApplication::instance());
    if (qtTranslator->load(QStringLiteral("qtbase_") + language,
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(qtTranslator);
    else
        delete qtTranslator;
    return language;
}

} // namespace ppcb
