#include "objectid.h"

#include <QRandomGenerator>

namespace ppcb {

namespace {

const int HEX_BASE = 16;
const int BITS_PER_HEX_DIGIT = 4;
// The lowest OBJECT_ID_LENGTH hex digits of a 64-bit number.
const quint64 ID_MASK = (quint64(1) << (OBJECT_ID_LENGTH * BITS_PER_HEX_DIGIT)) - 1;

// Value of a lower-case hex digit, or -1.
int hexDigitValue(QChar character)
{
    const char16_t code = character.unicode();
    if (code >= u'0' && code <= u'9')
        return code - u'0';
    if (code >= u'a' && code <= u'f')
        return code - u'a' + 10;
    return -1;
}

} // namespace

ObjectId newObjectId()
{
    ObjectId id;
    // Zero means "no object", so it is drawn again (one chance in 2^48).
    while (id.value == 0)
        id.value = QRandomGenerator::global()->generate64() & ID_MASK;
    return id;
}

QString idToString(const ObjectId &id)
{
    return QStringLiteral("%1").arg(id.value & ID_MASK, OBJECT_ID_LENGTH, HEX_BASE, QLatin1Char('0'));
}

ObjectId idFromString(const QString &text)
{
    if (text.size() != OBJECT_ID_LENGTH)
        return ObjectId();
    ObjectId id;
    for (QChar character : text) {
        const int digit = hexDigitValue(character);
        if (digit < 0)
            return ObjectId();
        id.value = id.value * HEX_BASE + quint64(digit);
    }
    return id;
}

} // namespace ppcb
