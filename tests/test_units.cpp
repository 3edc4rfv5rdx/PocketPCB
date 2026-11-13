#include "units.h"

#include <QtTest>

#include <cmath>
#include <limits>

using namespace ppcb;

class TestUnits : public QObject
{
    Q_OBJECT

private slots:
    void mmToNmExactValues()
    {
        QCOMPARE(mmToNm(0.0), Coord(0));
        QCOMPARE(mmToNm(1.0), NM_PER_MM);
        QCOMPARE(mmToNm(2.54), Coord(2540000));
        QCOMPARE(mmToNm(-2.54), Coord(-2540000));
        QCOMPARE(mmToNm(0.635), Coord(635000));
        QCOMPARE(mmToNm(10.16), Coord(10160000));
        QCOMPARE(mmToNm(0.0254), NM_PER_MIL);
    }

    void mmToNmRoundsToNearest()
    {
        QCOMPARE(mmToNm(0.0000004), Coord(0));
        QCOMPARE(mmToNm(0.0000006), Coord(1));
        QCOMPARE(mmToNm(-0.0000004), Coord(0));
        QCOMPARE(mmToNm(-0.0000006), Coord(-1));
        QCOMPARE(mmToNm(1.2345674), Coord(1234567));
        QCOMPARE(mmToNm(1.2345676), Coord(1234568));
    }

    void tryMmToNmRejectsInvalid()
    {
        Coord out = 42;
        QVERIFY(!tryMmToNm(std::numeric_limits<double>::quiet_NaN(), out));
        QVERIFY(!tryMmToNm(std::numeric_limits<double>::infinity(), out));
        QVERIFY(!tryMmToNm(-std::numeric_limits<double>::infinity(), out));
        QVERIFY(!tryMmToNm(MAX_ABS_MM * 2, out));
        QVERIFY(!tryMmToNm(-MAX_ABS_MM * 2, out));
        QCOMPARE(out, Coord(42)); // untouched on failure
    }

    void tryMmToNmAcceptsLimit()
    {
        Coord out = 0;
        QVERIFY(tryMmToNm(MAX_ABS_MM, out));
        QCOMPARE(out, Coord(1000000) * NM_PER_MM);
        QVERIFY(tryMmToNm(-MAX_ABS_MM, out));
        QCOMPARE(out, Coord(-1000000) * NM_PER_MM);
    }

    void nmToMmValues()
    {
        QCOMPARE(nmToMm(0), 0.0);
        QCOMPARE(nmToMm(2540000), 2.54);
        QCOMPARE(nmToMm(-635000), -0.635);
    }

    // nm -> mm -> nm must give back the same integer, across signs and scales.
    void roundTripNmMmNm()
    {
        const Coord samples[] = {
            0, 1, -1, 999, 25400, 635000, 2540000, 123456789,
            -123456789, Coord(999999) * NM_PER_MM + 1,
        };
        for (Coord nm : samples)
            QCOMPARE(mmToNm(nmToMm(nm)), nm);
    }
};

QTEST_APPLESS_MAIN(TestUnits)
#include "test_units.moc"
