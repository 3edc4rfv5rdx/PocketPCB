#pragma once

#include "jsonio.h"

#include <QMap>
#include <QString>
#include <QStringList>

namespace ppcb {

// A part of the library: ties a symbol to the footprints it may be built
// with. Pins and pads are matched by their numbers.
struct Part
{
    // Library name of the symbol.
    QString symbol;
    // Library names of the allowed footprints; the first is the default.
    // Empty for a part that is never on the board (a panel jack, a battery).
    QStringList footprints;
    // Reference designator prefix in the international style (R, D, Q, U)
    // and in the GOST style (R, VD, VT, DD).
    QString prefix;
    QString prefixGost;
    // Value a new part starts with; may be empty.
    QString value;
    // Connectors and wire pads: they carry nets off the board (used by ERC).
    bool connector = false;
    // A part that needs no value (a button, a connector): the sheet shows no
    // mark in place of an empty one, and ERC asks for none.
    bool noValue = false;
    // Rated power a new component of the part starts with, in milliwatts;
    // 0 for a part that has none (only resistors have).
    int powerMw = 0;
    // The group the bill of materials puts the part under, by key
    // ("resistors"); partGroupName() gives its name. Empty for none.
    QString group;
    // Name shown in the library panel, by interface language code
    // ("en", "uk", "ru"); may be empty.
    QMap<QString, QString> name;

    // The name in the first of 'languages' that has one; 'fallback' (the
    // library name of the part) when none has.
    QString displayName(const QStringList &languages, const QString &fallback) const;
};

// Rated powers of resistors offered, in milliwatts, the least first.
QList<int> standardPowersMw();
// A rated power in watts, in the current locale: "0.125 W"; empty for 0.
QString formatPower(int powerMw);
// The largest power, in milliwatts, a component may be given.
constexpr int MAX_POWER_MW = 1000000;
// 'number' (1 to 3999) in Roman numerals: the mark of a power of 5 W and
// more. Empty for a number out of that range.
QString romanNumeral(int number);

// The name of the part group 'key' in the interface language: the key
// itself for one the program does not know, "Other" for an empty one.
QString partGroupName(const QString &key);
// The group no part has: the board's wire links in the bill of materials.
extern const char LINK_GROUP[];
// The place of the part group 'key' in the library panel: the program's
// groups in their order, then the groups it does not know, no group last.
int partGroupRank(const QString &key);

bool operator==(const Part &a, const Part &b);
inline bool operator!=(const Part &a, const Part &b) { return !(a == b); }

QJsonObject partToJson(const Part &part);
// 'where' is the path of the object for error messages. After a failure
// (see reader.ok()) the result is not meaningful.
Part partFromJson(const QJsonObject &object, const QString &where, JsonReader &reader);

} // namespace ppcb
