#include "libraryfile.h"
#include "version.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

namespace {

LibraryItem sampleSymbol()
{
    LibraryItem item;
    item.kind = LibraryKind::Symbol;
    Pin pin;
    pin.number = QStringLiteral("1");
    pin.at = Point{-2 * PIN_GRID, 0};
    item.symbol.pins.append(pin);
    item.symbol.refAt = Point{0, -PIN_GRID};
    return item;
}

LibraryItem sampleFootprint()
{
    LibraryItem item;
    item.kind = LibraryKind::Footprint;
    Pad pad;
    pad.number = QStringLiteral("1");
    pad.sizeX = 1800000;
    pad.sizeY = 1800000;
    pad.hole = 900000;
    item.footprint.pads.append(pad);
    return item;
}

LibraryItem samplePart()
{
    LibraryItem item;
    item.kind = LibraryKind::Part;
    item.part.symbol = QStringLiteral("resistor");
    item.part.footprints = QStringList({"resistor-7.62"});
    item.part.prefix = QStringLiteral("R");
    item.part.prefixGost = QStringLiteral("R");
    return item;
}

void writeText(const QString &path, const QByteArray &text)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text);
}

} // namespace

class TestLibraryFile : public QObject
{
    Q_OBJECT

private slots:
    void everyKindSurvivesTheFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString error;

        const LibraryItem symbol = sampleSymbol();
        const QString symbolPath = dir.filePath(QStringLiteral("s.json"));
        QVERIFY2(LibraryFile::write(symbolPath, symbol, error), qPrintable(error));
        LibraryItem back;
        back.kind = LibraryKind::Part;
        QVERIFY2(LibraryFile::read(symbolPath, back, error), qPrintable(error));
        QVERIFY(back.kind == LibraryKind::Symbol);
        QVERIFY(back.symbol == symbol.symbol);

        const LibraryItem footprint = sampleFootprint();
        const QString footprintPath = dir.filePath(QStringLiteral("f.json"));
        QVERIFY2(LibraryFile::write(footprintPath, footprint, error), qPrintable(error));
        QVERIFY2(LibraryFile::read(footprintPath, back, error), qPrintable(error));
        QVERIFY(back.kind == LibraryKind::Footprint);
        QVERIFY(back.footprint == footprint.footprint);

        const LibraryItem part = samplePart();
        const QString partPath = dir.filePath(QStringLiteral("p.json"));
        QVERIFY2(LibraryFile::write(partPath, part, error), qPrintable(error));
        QVERIFY2(LibraryFile::read(partPath, back, error), qPrintable(error));
        QVERIFY(back.kind == LibraryKind::Part);
        QVERIFY(back.part == part.part);
    }

    void writtenFileCarriesTheHeader()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("p.json"));
        QString error;
        QVERIFY2(LibraryFile::write(path, samplePart(), error), qPrintable(error));

        QJsonObject root;
        QVERIFY2(JsonFile::read(path, root, error), qPrintable(error));
        QCOMPARE(fileTypeOf(root), QString::fromLatin1(FILE_TYPE_PART));
        QCOMPARE(root.value(QStringLiteral("format")).toInt(), FORMAT_VERSION);
        QCOMPARE(root.value(QStringLiteral("app")).toString(), QString::fromLatin1(appVersion()));
    }

    void failuresLeaveTheItemAlone_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QString>("expectedInError");

        QTest::newRow("no type") << QByteArray(R"({"format":1,"pins":[]})") << "";
        QTest::newRow("a project file")
            << QByteArray(R"({"type":"pocketpcb-project","format":1})") << "";
        QTest::newRow("type is a number") << QByteArray(R"({"type":5,"format":1})") << "";
        QTest::newRow("newer format")
            << QByteArray(R"({"type":"pocketpcb-part","format":2,"symbol":"r","prefix":"R","prefixGost":"R"})")
            << "";
        QTest::newRow("no format")
            << QByteArray(R"({"type":"pocketpcb-part","symbol":"r","prefix":"R","prefixGost":"R"})")
            << "format: ";
        QTest::newRow("bad body")
            << QByteArray(R"({"type":"pocketpcb-symbol","format":1,"graphics":[],
                              "pins":[{"number":"1","at":[1,0],"side":"left"}],
                              "refAt":[0,0],"valueAt":[0,0]})")
            << "pins[0].at: ";
        QTest::newRow("damaged") << QByteArray("{\n\"type\": ,\n}") << "line 2";
    }

    void failuresLeaveTheItemAlone()
    {
        QFETCH(QByteArray, text);
        QFETCH(QString, expectedInError);

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("x.json"));
        writeText(path, text);

        LibraryItem item = samplePart();
        QString error;
        QVERIFY(!LibraryFile::read(path, item, error));
        QVERIFY2(error.startsWith(path + QStringLiteral(": ")), qPrintable(error));
        QVERIFY2(error.contains(expectedInError), qPrintable(error));
        QVERIFY(item.kind == LibraryKind::Part);
        QVERIFY(item.part == samplePart().part);
    }

    void missingFileFails()
    {
        QTemporaryDir dir;
        LibraryItem item;
        QString error;
        QVERIFY(!LibraryFile::read(dir.filePath(QStringLiteral("absent.json")), item, error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestLibraryFile)
#include "test_libraryfile.moc"
