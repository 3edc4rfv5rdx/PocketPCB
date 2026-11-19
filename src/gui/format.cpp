#include "format.h"

#include <QCoreApplication>
#include <QLocale>

#include <cmath>

namespace ppcb {

namespace {

const int MM_DECIMALS = 3;
// Half of the last shown decimal: below it the value is displayed as zero.
const double MM_SHOWN_AS_ZERO = 0.0005;

} // namespace

QString formatMmFixed(Coord nm)
{
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    double mm = nmToMm(nm);
    // Avoids "-0.000" for a tiny negative value.
    if (std::fabs(mm) < MM_SHOWN_AS_ZERO)
        mm = 0.0;
    return locale.toString(mm, 'f', MM_DECIMALS);
}

QString formatMmShort(Coord nm)
{
    // The fixed form always has a decimal point, so cutting zeros from the
    // right can never eat into the integer part.
    QString text = formatMmFixed(nm);
    while (text.endsWith(QLatin1Char('0')))
        text.chop(1);
    const QString point = QLocale().decimalPoint();
    if (text.endsWith(point))
        text.chop(point.size());
    return text;
}

QString formatPower(int powerMw)
{
    if (powerMw <= 0)
        return QString();
    const int milliwattsPerWatt = 1000;
    QLocale locale;
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    const QString watts = locale.toString(double(powerMw) / milliwattsPerWatt, 'g', 6);
    return QCoreApplication::translate("ppcb::Format", "%1 W").arg(watts);
}

} // namespace ppcb
