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
    // Name shown in the library panel, by interface language code
    // ("en", "uk", "ru"); may be empty.
    QMap<QString, QString> name;

    // The name in the first of 'languages' that has one; 'fallback' (the
    // library name of the part) when none has.
    QString displayName(const QStringList &languages, const QString &fallback) const;
};

bool operator==(const Part &a, const Part &b);
inline bool operator!=(const Part &a, const Part &b) { return !(a == b); }

QJsonObject partToJson(const Part &part);
// 'where' is the path of the object for error messages. After a failure
// (see reader.ok()) the result is not meaningful.
Part partFromJson(const QJsonObject &object, const QString &where, JsonReader &reader);

} // namespace ppcb
