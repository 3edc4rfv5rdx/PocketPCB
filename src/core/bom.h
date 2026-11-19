#pragma once

#include "library.h"
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
    // The group of the part (Part::group); empty for a part without one or
    // missing from the library.
    QString group;
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

// The rows of 'schematic', their groups taken from 'library': the rows of
// a group together, the groups in the order of their first designators,
// those without a group last; within a group, rows by their first
// designators.
QList<BomRow> bomRows(const Schematic &schematic, const Library &library);

// The rated power, in milliwatts, the most components of 'schematic' have:
// the lower of those as many have; 0 when none has a power.
int commonPowerMw(const Schematic &schematic);

// True when designator 'a' comes before 'b': by their letters, then by the
// number after them, so that R2 comes before R10.
bool designatorLess(const QString &a, const QString &b);

// A table, its first row the heading; a later row of one field, when the
// heading has more, is the heading of a group.
// As comma separated values: a field
// holding a comma, a quote or a line break is quoted, its quotes doubled;
// lines end with "\r\n".
QString csvOf(const QList<QStringList> &table);
// The same as plain text: each column as wide as its widest field (group
// headings left out), two spaces between, a line of dashes under the
// heading, a group heading after "=== " and an empty line before it unless
// it follows the rule.
QString plainTextOf(const QList<QStringList> &table);

} // namespace ppcb
