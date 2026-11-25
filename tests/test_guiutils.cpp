#include "appsettings.h"
#include "format.h"
#include "gerberexport.h"
#include "language.h"

#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>
#include <QtTest>

using namespace ppcb;

class TestGuiUtils : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keeps the test away from the user's real settings.
        QStandardPaths::setTestModeEnabled(true);
        AppSettings::initApplication();
        QLocale::setDefault(QLocale::c());
    }

    void init()
    {
        // clear(), not deleting the file: QSettings caches files inside a process.
        QSettings(AppSettings::filePath(), QSettings::IniFormat).clear();
    }

    void formatFixed()
    {
        QCOMPARE(formatMmFixed(0), QStringLiteral("0.000"));
        QCOMPARE(formatMmFixed(2540000), QStringLiteral("2.540"));
        QCOMPARE(formatMmFixed(-635000), QStringLiteral("-0.635"));
        QCOMPARE(formatMmFixed(1234567890), QStringLiteral("1234.568")); // no group separator
        QCOMPARE(formatMmFixed(-400), QStringLiteral("0.000")); // not "-0.000"
    }

    // The revision and the day in the archive's name, the revision made
    // safe for a file name and left out when empty.
    void gerberArchiveNamed()
    {
        const QDate day(2026, 11, 25);
        QCOMPARE(gerberArchiveName(QStringLiteral("amp"), QString(), day),
                 QStringLiteral("amp-20261125-gerber.zip"));
        QCOMPARE(gerberArchiveName(QStringLiteral("amp"), QStringLiteral(" 1.2/b "), day),
                 QStringLiteral("amp-rev1.2_b-20261125-gerber.zip"));
    }

    void formatShort()
    {
        QCOMPARE(formatMmShort(635000), QStringLiteral("0.635"));
        QCOMPARE(formatMmShort(1270000), QStringLiteral("1.27"));
        QCOMPARE(formatMmShort(2540000), QStringLiteral("2.54"));
        QCOMPARE(formatMmShort(10160000), QStringLiteral("10.16"));
        QCOMPARE(formatMmShort(5000000), QStringLiteral("5"));
        QCOMPARE(formatMmShort(100000000), QStringLiteral("100")); // integer zeros stay
        QCOMPARE(formatMmShort(0), QStringLiteral("0"));
    }

    void formatUsesLocaleDecimalPoint()
    {
        QLocale::setDefault(QLocale(QLocale::Russian, QLocale::Russia));
        const QString fixedText = formatMmFixed(2540000);
        const QString shortText = formatMmShort(5000000);
        QLocale::setDefault(QLocale::c());
        QCOMPARE(fixedText, QStringLiteral("2,540"));
        QCOMPARE(shortText, QStringLiteral("5"));
    }

    void languagesAreNamedInThemselves()
    {
        QCOMPARE(languageName(QStringLiteral("en")), QStringLiteral("English"));
        QCOMPARE(languageName(QStringLiteral("uk")), QStringLiteral("Українська"));
        QCOMPARE(languageName(QStringLiteral("ru")), QStringLiteral("Русский"));
        for (const QString &code : supportedLanguages())
            QVERIFY(languageName(code) != code);
    }

    void languageFollowsTheSystem()
    {
        QCOMPARE(chooseLanguage(QString(), {"ru-RU", "en-US"}), QStringLiteral("ru"));
        QCOMPARE(chooseLanguage(QString(), {"uk_UA"}), QStringLiteral("uk"));
        QCOMPARE(chooseLanguage(QString(), {"en"}), QStringLiteral("en"));
        // The first supported language of the list wins.
        QCOMPARE(chooseLanguage(QString(), {"de-DE", "ru", "uk"}), QStringLiteral("ru"));
        // Nothing supported, an empty list, the "C" locale: English.
        QCOMPARE(chooseLanguage(QString(), {"de-DE", "fr"}), QStringLiteral("en"));
        QCOMPARE(chooseLanguage(QString(), {}), QStringLiteral("en"));
        QCOMPARE(chooseLanguage(QString(), {"C"}), QStringLiteral("en"));
    }

    void languageSettingWinsOverTheSystem()
    {
        QCOMPARE(chooseLanguage(QStringLiteral("uk"), {"ru-RU"}), QStringLiteral("uk"));
        QCOMPARE(chooseLanguage(QStringLiteral("en"), {"ru-RU"}), QStringLiteral("en"));
        QCOMPARE(chooseLanguage(QStringLiteral(" RU_ru "), {"en"}), QStringLiteral("ru"));
        // An unknown setting falls back to the system.
        QCOMPARE(chooseLanguage(QStringLiteral("xx"), {"ru-RU"}), QStringLiteral("ru"));
    }

    void everyTranslationLoads()
    {
        const QStringList languages = supportedLanguages();
        QCOMPARE(languages.first(), QStringLiteral("en"));
        for (const QString &language : languages) {
            QTranslator translator;
            // The source language has no translation file; all others must.
            const bool expected = language != languages.first();
            QVERIFY2(loadTranslation(translator, language) == expected, qPrintable(language));
        }
    }

    void translationsAreComplete()
    {
        // One of each kind of string: a menu, a choice, the unit.
        const QList<QPair<const char *, const char *>> samples = {
            {"ppcb::MainWindow", "&File"},
            {"ppcb::MainWindow", "Lines"},
            {"ppcb::MainWindow", "Full-window cross"},
            {"ppcb::Format", "mm"}};
        for (const QString &language : {QStringLiteral("uk"), QStringLiteral("ru")}) {
            QTranslator translator;
            QVERIFY(loadTranslation(translator, language));
            for (const auto &[context, sample] : samples) {
                const QString text = translator.translate(context, sample);
                QVERIFY2(!text.isEmpty(), sample);
                QVERIFY2(text != QLatin1String(sample), sample);
            }
        }
    }

    void settingsDefaults()
    {
        QVERIFY(AppSettings::language().isEmpty());
        QCOMPARE(AppSettings::gridStyle(), GridStyle::Lines);
        QCOMPARE(AppSettings::cursorStyle(), CursorStyle::SmallCross);
        QCOMPARE(AppSettings::zoomCenterAndWarp(), true);
        QVERIFY(AppSettings::windowGeometry().isEmpty());
    }

    void settingsRoundTrip()
    {
        AppSettings::setGridStyle(GridStyle::Dots);
        QCOMPARE(AppSettings::gridStyle(), GridStyle::Dots);
        AppSettings::setGridStyle(GridStyle::Lines);
        QCOMPARE(AppSettings::gridStyle(), GridStyle::Lines);

        AppSettings::setCursorStyle(CursorStyle::Arrow);
        QCOMPARE(AppSettings::cursorStyle(), CursorStyle::Arrow);
        AppSettings::setCursorStyle(CursorStyle::FullCross);
        QCOMPARE(AppSettings::cursorStyle(), CursorStyle::FullCross);
        AppSettings::setCursorStyle(CursorStyle::SmallCross);
        QCOMPARE(AppSettings::cursorStyle(), CursorStyle::SmallCross);

        AppSettings::setZoomCenterAndWarp(false);
        QCOMPARE(AppSettings::zoomCenterAndWarp(), false);

        AppSettings::setLanguage(QStringLiteral("uk"));
        QCOMPARE(AppSettings::language(), QStringLiteral("uk"));

        const QByteArray bytes("\x00\x01\xff geometry", 12);
        AppSettings::setWindowGeometry(bytes);
        QCOMPARE(AppSettings::windowGeometry(), bytes);
    }

    // A hand-edited or damaged file must give defaults, not garbage.
    void settingsUnknownValuesFallBack()
    {
        {
            QSettings raw(AppSettings::filePath(), QSettings::IniFormat);
            raw.setValue(QStringLiteral("view/gridStyle"), QStringLiteral("hexagons"));
            raw.setValue(QStringLiteral("view/cursorStyle"), 42);
        }
        QCOMPARE(AppSettings::gridStyle(), GridStyle::Lines);
        QCOMPARE(AppSettings::cursorStyle(), CursorStyle::SmallCross);
    }
};

QTEST_GUILESS_MAIN(TestGuiUtils)
#include "test_guiutils.moc"
