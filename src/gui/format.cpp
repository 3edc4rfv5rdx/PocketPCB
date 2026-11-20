#include "format.h"

#include "project.h"

#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QLocale>

#include <cmath>

namespace ppcb {

namespace {

const int MM_DECIMALS = 3;
// Half of the last shown decimal: below it the value is displayed as zero.
const double MM_SHOWN_AS_ZERO = 0.0005;
// What a size box keeps: the length it was given, in nanometres.
const char SIZE_PROPERTY[] = "nm";

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

QString formatPosition(Coord x, Coord y)
{
    return QStringLiteral("X %1   Y %2 ").arg(formatMmFixed(x), formatMmFixed(y)) + mmUnit();
}

bool parseMm(const QString &text, Coord &nm)
{
    const QString trimmed = text.trimmed();
    // Group separators are refused, so that a comma is always a decimal point.
    QLocale locale;
    locale.setNumberOptions(QLocale::RejectGroupSeparator);
    bool ok = false;
    double mm = locale.toDouble(trimmed, &ok);
    if (!ok) {
        const QString pointed = QString(trimmed).replace(QLatin1Char(','), QLatin1Char('.'));
        mm = QLocale::c().toDouble(pointed, &ok);
    }
    return ok && tryMmToNm(mm, nm);
}

QDoubleSpinBox *mmSizeBox(Coord nm, double minMm, double maxMm, double stepMm, QWidget *parent)
{
    QDoubleSpinBox *box = new QDoubleSpinBox(parent);
    box->setDecimals(MM_DECIMALS);
    box->setRange(minMm, maxMm);
    box->setSingleStep(stepMm);
    box->setValue(nmToMm(nm));
    box->setProperty(SIZE_PROPERTY, QVariant::fromValue(qint64(nm)));
    return box;
}

Coord mmSizeOf(const QDoubleSpinBox *box)
{
    const Coord given = box->property(SIZE_PROPERTY).toLongLong();
    const double scale = std::pow(10.0, MM_DECIMALS);
    const double shown = std::round(nmToMm(given) * scale) / scale;
    if (std::abs(box->value() - shown) < 0.5 / scale)
        return given;
    Coord nm = given;
    tryMmToNm(box->value(), nm);
    return nm;
}

QString mmUnit()
{
    return QCoreApplication::translate("ppcb::Format", "mm");
}

QString symbolWord()
{
    return QCoreApplication::translate("ppcb::Format", "symbol");
}

QString footprintsWord()
{
    return QCoreApplication::translate("ppcb::Format", "footprints");
}

QString projectOutputFile(const QString &projectPath, const QString &suffix,
                          const QString &extension, QString &problem)
{
    if (projectPath.isEmpty()) {
        problem = QCoreApplication::translate("ppcb::Format", "Save the project first");
        return QString();
    }
    const QString folder = DesignFiles::outputPath(projectPath);
    if (!QDir().mkpath(folder)) {
        problem = QCoreApplication::translate("ppcb::Format", "Cannot make the folder")
                  + QLatin1Char(' ') + QDir::toNativeSeparators(folder);
        return QString();
    }
    return QDir(folder).filePath(QFileInfo(projectPath).completeBaseName() + suffix
                                 + QLatin1Char('.') + extension);
}

QString writtenMessage(const QString &path)
{
    return QCoreApplication::translate("ppcb::Format", "Written") + QLatin1Char(' ')
           + QDir::toNativeSeparators(path);
}

QString cannotWriteMessage(const QString &path)
{
    return QCoreApplication::translate("ppcb::Format", "Cannot write") + QLatin1Char(' ')
           + QDir::toNativeSeparators(path);
}

} // namespace ppcb
