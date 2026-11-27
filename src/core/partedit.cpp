#include "partedit.h"

#include <QCoreApplication>

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

QStringList partMismatches(const Part &part, const Library &library)
{
    QStringList problems;
    const Symbol *symbol = library.findSymbol(part.symbol);
    if (!part.symbol.isEmpty() && symbol == nullptr)
        problems.append(
            QCoreApplication::translate(CONTEXT, "the library has no symbol %1").arg(part.symbol));
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
    }
    return problems;
}

QString partProblem(const Part &part)
{
    if (part.symbol.isEmpty())
        return QCoreApplication::translate(CONTEXT, "a part needs a symbol");
    if (part.prefix.isEmpty() || part.prefixGost.isEmpty())
        return QCoreApplication::translate(CONTEXT, "a part needs both designator prefixes");
    return QString();
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
