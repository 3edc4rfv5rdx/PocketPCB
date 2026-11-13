#include "language.h"

#include "appsettings.h"

#include <QCoreApplication>
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

QString installLanguage()
{
    const QString language = chooseLanguage(AppSettings::language(), QLocale::system().uiLanguages());
    QTranslator *translator = new QTranslator(QCoreApplication::instance());
    if (loadTranslation(*translator, language))
        QCoreApplication::installTranslator(translator);
    else
        delete translator;
    return language;
}

} // namespace ppcb
