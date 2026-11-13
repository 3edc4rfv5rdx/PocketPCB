#pragma once

#include <QHashFunctions>
#include <QString>

namespace ppcb {

// Identity of an object of the schematic or the board: a random 48-bit
// number, made once when the object is created, never changed or reused,
// kept in the files. A default-constructed id is "no object".
struct ObjectId
{
    quint64 value = 0;

    bool isNull() const { return value == 0; }
};

inline bool operator==(const ObjectId &a, const ObjectId &b) { return a.value == b.value; }
inline bool operator!=(const ObjectId &a, const ObjectId &b) { return a.value != b.value; }
inline bool operator<(const ObjectId &a, const ObjectId &b) { return a.value < b.value; }
inline size_t qHash(const ObjectId &id, size_t seed = 0) { return ::qHash(id.value, seed); }

// Number of characters of the text form.
constexpr int OBJECT_ID_LENGTH = 12;

// A new random id; never "no object".
ObjectId newObjectId();

// Text form used in the files: exactly 12 lower-case hexadecimal digits.
QString idToString(const ObjectId &id);

// Returns "no object" (isNull()) for text that is not an id in that form.
ObjectId idFromString(const QString &text);

} // namespace ppcb
