#include "lockflash.h"

namespace ppcb {

namespace {

// How long it stays red, in milliseconds.
const int LOCK_FLASH_MS = 1000;

} // namespace

LockFlash::LockFlash(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_ids.clear();
        m_outline = false;
        emit changed();
    });
}

void LockFlash::flash(const QSet<ObjectId> &ids, bool outline)
{
    if (ids.isEmpty() && !outline)
        return;
    m_ids = ids;
    m_outline = outline;
    m_timer.start(LOCK_FLASH_MS);
    emit changed();
}

} // namespace ppcb
