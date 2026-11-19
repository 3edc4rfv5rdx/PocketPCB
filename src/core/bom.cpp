#include "bom.h"

#include <algorithm>

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

const QChar CSV_SEPARATOR = QLatin1Char(',');
const QChar CSV_QUOTE = QLatin1Char('"');
const char CSV_LINE_END[] = "\r\n";
const int COLUMN_GAP = 2;
const QChar HEADING_RULE = QLatin1Char('-');

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

QList<BomRow> bomRows(const Schematic &schematic)
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
                row.part = key.part;
                row.value = key.value;
                row.footprint = key.footprint;
                row.powerMw = key.powerMw;
                row.comment = key.comment;
                rows.append(row);
            }
            rows[index].designators.append(component.designator);
            rows[index].ids.insert(component.id);
        }
    }
    for (BomRow &row : rows)
        std::sort(row.designators.begin(), row.designators.end(), designatorLess);
    std::sort(rows.begin(), rows.end(), [](const BomRow &a, const BomRow &b) {
        return designatorLess(a.designators.value(0), b.designators.value(0));
    });
    return rows;
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
    QList<int> widths;
    for (const QStringList &row : table) {
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
