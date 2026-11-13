#include "appsettings.h"
#include "format.h"

#include <QSettings>
#include <QStandardPaths>
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

    void settingsDefaults()
    {
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
