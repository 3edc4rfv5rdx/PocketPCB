#include "bom.h"

#include <QMap>

#include <algorithm>
#include <cmath>

namespace ppcb {

namespace {

// What sets a row apart.
struct BomKey
{
    QString part;
    QString value;
    QString footprint;
    int powerMw = 0;
    QString comment;

    bool operator==(const BomKey &other) const
    {
        return part == other.part && value == other.value && footprint == other.footprint
               && powerMw == other.powerMw && comment == other.comment;
    }
};

// 'designator' split into its letters before the first digit and the
// number after them; -1 for no number.
QPair<QString, qint64> designatorParts(const QString &designator)
{
    int i = 0;
    while (i < designator.size() && !designator.at(i).isDigit())
        ++i;
    bool ok = false;
    const qint64 number = designator.mid(i).toLongLong(&ok);
    return qMakePair(designator.left(i), ok ? number : qint64(-1));
}

// An insulated link's wire: the way between its holes and this share more,
// as it goes round parts, a lead to solder at each end, rounded up to
// whole steps.
const double INSULATED_SPARE_SHARE = 0.2;
constexpr Coord INSULATED_LEAD = 5000000;
constexpr Coord INSULATED_ROUNDING = 5000000;

const QChar CSV_SEPARATOR = QLatin1Char(',');
const QChar CSV_QUOTE = QLatin1Char('"');
const char CSV_LINE_END[] = "\r\n";
const int COLUMN_GAP = 2;
const QChar HEADING_RULE = QLatin1Char('-');
// What a group heading starts with in plain text.
const char GROUP_MARK[] = "=== ";

} // namespace

bool designatorLess(const QString &a, const QString &b)
{
    const auto [prefixA, numberA] = designatorParts(a);
    const auto [prefixB, numberB] = designatorParts(b);
    if (prefixA != prefixB)
        return prefixA < prefixB;
    if (numberA != numberB)
        return numberA < numberB;
    return a < b;
}

QList<BomRow> bomRows(const Schematic &schematic, const Library &library)
{
    QList<BomRow> rows;
    QList<BomKey> keys;
    for (const Sheet &sheet : schematic.sheets) {
        for (const Component &component : sheet.components) {
            const BomKey key{component.part, component.value, component.footprint,
                             component.powerMw, component.comment};
            int index = int(keys.indexOf(key));
            if (index < 0) {
                index = int(keys.size());
                keys.append(key);
                BomRow row;
                if (const Part *part = library.findPart(key.part))
                    row.group = bomGroup(part->group);
                row.part = key.part;
                row.value = key.value;
                row.footprint = key.footprint;
                row.powerMw = key.powerMw;
                row.comment = key.comment;
                rows.append(row);
            }
            // The units of a part of several are one part of it.
            const Part *part = library.findPart(component.part);
            const bool unitOfOneListed = part != nullptr && part->unitCount() > 1
                                         && rows.at(index).designators.contains(component.designator);
            if (!unitOfOneListed)
                rows[index].designators.append(component.designator);
            rows[index].ids.insert(component.id);
        }
    }
    for (BomRow &row : rows)
        std::sort(row.designators.begin(), row.designators.end(), designatorLess);
    std::sort(rows.begin(), rows.end(), [](const BomRow &a, const BomRow &b) {
        return designatorLess(a.designators.value(0), b.designators.value(0));
    });
    // Groups by their first rows, no group last.
    QStringList groupOrder;
    for (const BomRow &row : rows) {
        if (!row.group.isEmpty() && !groupOrder.contains(row.group))
            groupOrder.append(row.group);
    }
    const auto rank = [&groupOrder](const BomRow &row) {
        return row.group.isEmpty() ? groupOrder.size() : groupOrder.indexOf(row.group);
    };
    std::stable_sort(rows.begin(), rows.end(), [&rank](const BomRow &a, const BomRow &b) {
        return rank(a) < rank(b);
    });
    return rows;
}

Coord insulatedLinkLength(Coord apart)
{
    const double wire =
        double(apart) * (1 + INSULATED_SPARE_SHARE) + 2.0 * double(INSULATED_LEAD);
    return Coord(std::ceil(wire / double(INSULATED_ROUNDING))) * INSULATED_ROUNDING;
}

QList<BomRow> linkRows(const Board &board)
{
    QList<BomRow> rows;
    for (const WireLink &link : board.links) {
        const Point way = link.to - link.from;
        const Coord apart = Coord(std::llround(std::hypot(double(way.x), double(way.y))));
        const Coord length = link.insulated ? insulatedLinkLength(apart) : apart;
        auto row = std::find_if(rows.begin(), rows.end(), [&](const BomRow &r) {
            return r.insulated == link.insulated && r.length == length;
        });
        if (row == rows.end()) {
            BomRow added;
            added.group = QLatin1String(LINK_GROUP);
            added.link = true;
            added.insulated = link.insulated;
            added.length = length;
            rows.append(added);
            row = rows.end() - 1;
        }
        row->designators.append(link.designator);
        row->ids.insert(link.id);
    }
    for (BomRow &row : rows)
        std::sort(row.designators.begin(), row.designators.end(), designatorLess);
    std::sort(rows.begin(), rows.end(), [](const BomRow &a, const BomRow &b) {
        return designatorLess(a.designators.value(0), b.designators.value(0));
    });
    return rows;
}

int commonPowerMw(const Schematic &schematic)
{
    QMap<int, qsizetype> counts;
    for (const Sheet &sheet : schematic.sheets) {
        for (const Component &component : sheet.components) {
            if (component.powerMw > 0)
                ++counts[component.powerMw];
        }
    }
    int common = 0;
    qsizetype most = 0;
    // From the lowest power up: a later one wins only with more.
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > most) {
            most = it.value();
            common = it.key();
        }
    }
    return common;
}

QString csvOf(const QList<QStringList> &table)
{
    QString text;
    for (const QStringList &row : table) {
        QStringList fields;
        for (QString field : row) {
            if (field.contains(CSV_SEPARATOR) || field.contains(CSV_QUOTE)
                || field.contains(QLatin1Char('\n')) || field.contains(QLatin1Char('\r'))) {
                field.replace(CSV_QUOTE, QString(2, CSV_QUOTE));
                field = CSV_QUOTE + field + CSV_QUOTE;
            }
            fields.append(field);
        }
        text += fields.join(CSV_SEPARATOR) + QLatin1String(CSV_LINE_END);
    }
    return text;
}

QString plainTextOf(const QList<QStringList> &table)
{
    const int columns = table.isEmpty() ? 0 : int(table.first().size());
    const auto isGroupHeading = [columns](int r, const QStringList &row) {
        return r > 0 && row.size() == 1 && columns > 1;
    };
    QList<int> widths;
    for (int r = 0; r < table.size(); ++r) {
        const QStringList &row = table.at(r);
        if (isGroupHeading(r, row))
            continue;
        for (int i = 0; i < row.size(); ++i) {
            if (i >= widths.size())
                widths.append(0);
            widths[i] = qMax(widths.at(i), int(row.at(i).size()));
        }
    }
    const auto line = [&widths](const QStringList &row) {
        QString text;
        for (int i = 0; i < row.size(); ++i) {
            const bool last = i + 1 == row.size();
            text += last ? row.at(i) : row.at(i).leftJustified(widths.at(i) + COLUMN_GAP);
        }
        return text + QLatin1Char('\n');
    };
    QString text;
    for (int r = 0; r < table.size(); ++r) {
        if (isGroupHeading(r, table.at(r))) {
            if (r > 1)
                text += QLatin1Char('\n');
            text += QLatin1String(GROUP_MARK) + table.at(r).first() + QLatin1Char('\n');
            continue;
        }
        text += line(table.at(r));
        if (r == 0) {
            int total = 0;
            for (int i = 0; i < widths.size(); ++i)
                total += widths.at(i) + (i + 1 < widths.size() ? COLUMN_GAP : 0);
            text += QString(total, HEADING_RULE) + QLatin1Char('\n');
        }
    }
    return text;
}

} // namespace ppcb
