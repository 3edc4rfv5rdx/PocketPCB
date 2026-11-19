#pragma once

#include "schematic.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

namespace ppcb {

// The bill of materials: the components of the schematic, those of one part
// with the same value, footprint, rated power and comment in one row. The
// parts not on the board are there too.
struct BomRow
{
    // The designators, in the order designatorLess() gives.
    QStringList designators;
    QString part;
    QString value;
    QString footprint;
    int powerMw = 0;
    QString comment;
    // The components of the row, on whatever sheet.
    QSet<ObjectId> ids;
};

// The rows of 'schematic', ordered by their first designators.
QList<BomRow> bomRows(const Schematic &schematic);

// True when designator 'a' comes before 'b': by their letters, then by the
// number after them, so that R2 comes before R10.
bool designatorLess(const QString &a, const QString &b);

// A table, its first row the heading, as comma separated values: a field
// holding a comma, a quote or a line break is quoted, its quotes doubled;
// lines end with "\r\n".
QString csvOf(const QList<QStringList> &table);
// The same as plain text: each column as wide as its widest field, two
// spaces between, a line of dashes under the heading.
QString plainTextOf(const QList<QStringList> &table);

} // namespace ppcb
