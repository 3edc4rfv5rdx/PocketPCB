#include "language.h"

#include "appsettings.h"
#include "part.h"

#include <QCoreApplication>
#include <QDir>
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

// The embedded translations: TRANSLATION_FOLDER/pocketpcb_<code>.qm.
const char TRANSLATION_FOLDER[] = ":/i18n";
const char TRANSLATION_PREFIX[] = "pocketpcb_";
const char TRANSLATION_SUFFIX[] = ".qm";

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
    // The language of the code, then one for every embedded translation
    // (":/i18n/pocketpcb_uk.qm"), in the order of their codes.
    static const QStringList languages = [] {
        initTranslationResources();
        QStringList result = {QLatin1String(CODE_LANGUAGE)};
        const QString prefix = QLatin1String(TRANSLATION_PREFIX);
        const QString suffix = QLatin1String(TRANSLATION_SUFFIX);
        const QStringList files =
            QDir(QLatin1String(TRANSLATION_FOLDER))
                .entryList({prefix + QLatin1Char('*') + suffix}, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const QString code = file.mid(prefix.size(), file.size() - prefix.size() - suffix.size());
            if (!code.isEmpty() && !result.contains(code))
                result.append(code);
        }
        return result;
    }();
    return languages;
}

QString languageName(const QString &code)
{
    const QLocale locale(code);
    if (locale.language() == QLocale::C)
        return code;
    // Qt names the English of the code by its country ("American English").
    if (code == QLatin1String(CODE_LANGUAGE))
        return QLocale::languageToString(locale.language());
    const QString name = locale.nativeLanguageName();
    if (name.isEmpty())
        return code;
    // "українська" -> "Українська", as a list of languages shows it.
    return locale.toUpper(name.left(1)) + name.mid(1);
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
    return translator.load(QLatin1String(TRANSLATION_FOLDER) + QLatin1Char('/')
                           + QLatin1String(TRANSLATION_PREFIX) + language
                           + QLatin1String(TRANSLATION_SUFFIX));
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
