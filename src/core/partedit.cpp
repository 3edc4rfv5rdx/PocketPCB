#include "partedit.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>

namespace ppcb {

namespace {

const char CONTEXT[] = "ppcb::PartEdit";

} // namespace

QStringList pinsWithoutPad(const Symbol &symbol, const Footprint &footprint)
{
    QStringList missing;
    for (const Pin &pin : symbol.pins) {
        const bool found = std::any_of(footprint.pads.cbegin(), footprint.pads.cend(),
                                       [&pin](const Pad &pad) { return pad.number == pin.number; });
        if (!found && !missing.contains(pin.number))
            missing.append(pin.number);
    }
    return missing;
}

QStringList padsWithoutPin(const Symbol &symbol, const Footprint &footprint)
{
    QStringList extra;
    for (const Pad &pad : footprint.pads) {
        if (pad.number.isEmpty() || extra.contains(pad.number))
            continue;
        const bool found = std::any_of(symbol.pins.cbegin(), symbol.pins.cend(),
                                       [&pad](const Pin &pin) { return pin.number == pad.number; });
        if (!found)
            extra.append(pad.number);
    }
    return extra;
}

QStringList partMismatches(const Part &part, const Library &library)
{
    QStringList problems;
    // The pins of all its units, each number once: a number in two units
    // is one pin twice.
    Symbol all;
    bool allFound = true;
    QStringList twice;
    // The numbers of the units before the one read.
    QSet<QString> earlier;
    for (int unit = 0; unit < part.unitCount(); ++unit) {
        const QString name = part.unitSymbol(unit);
        const Symbol *symbol = library.findSymbol(name);
        if (symbol == nullptr) {
            allFound = false;
            if (!name.isEmpty())
                problems.append(
                    QCoreApplication::translate(CONTEXT, "the library has no symbol %1").arg(name));
            continue;
        }
        QSet<QString> own;
        for (const Pin &pin : symbol->pins) {
            if (earlier.contains(pin.number) && !twice.contains(pin.number))
                twice.append(pin.number);
            if (!own.contains(pin.number) && !earlier.contains(pin.number))
                all.pins.append(pin);
            own.insert(pin.number);
        }
        earlier.unite(own);
    }
    if (!twice.isEmpty())
        problems.append(QCoreApplication::translate(CONTEXT, "pins %1 are in two units")
                            .arg(twice.join(QStringLiteral(", "))));
    const Symbol *symbol = allFound ? &all : nullptr;
    for (const QString &name : part.footprints) {
        const Footprint *footprint = library.findFootprint(name);
        if (footprint == nullptr) {
            problems.append(
                QCoreApplication::translate(CONTEXT, "the library has no footprint %1").arg(name));
            continue;
        }
        if (symbol == nullptr)
            continue;
        const QStringList missing = pinsWithoutPad(*symbol, *footprint);
        if (!missing.isEmpty())
            problems.append(QCoreApplication::translate(CONTEXT, "%1 has no pad for pins %2")
                                .arg(name, missing.join(QStringLiteral(", "))));
        const QStringList extra = padsWithoutPin(*symbol, *footprint);
        if (!extra.isEmpty())
            problems.append(QCoreApplication::translate(CONTEXT, "%1 has pads %2 for no pin")
                                .arg(name, extra.join(QStringLiteral(", "))));
    }
    return problems;
}

QString partProblem(const Part &part)
{
    if (part.symbol.isEmpty())
        return QCoreApplication::translate(CONTEXT, "a part needs a symbol");
    if (part.prefix.isEmpty() && part.prefixGost.isEmpty())
        return QCoreApplication::translate(CONTEXT, "a part needs a designator prefix");
    if (part.name.value(QLatin1String(CODE_LANGUAGE)).trimmed().isEmpty())
        return QCoreApplication::translate(CONTEXT, "a part needs an English name");
    return QString();
}

Part withBothPrefixes(const Part &part)
{
    Part result = part;
    if (result.prefix.isEmpty())
        result.prefix = result.prefixGost;
    if (result.prefixGost.isEmpty())
        result.prefixGost = result.prefix;
    return result;
}

Part withFootprintMoved(const Part &part, int from, int to)
{
    Part result = part;
    const int count = int(result.footprints.size());
    if (from < 0 || from >= count || to < 0 || to >= count)
        return result;
    result.footprints.move(from, to);
    return result;
}

} // namespace ppcb
