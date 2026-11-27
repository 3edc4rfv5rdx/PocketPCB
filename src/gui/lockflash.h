#pragma once

#include "objectid.h"

#include <QObject>
#include <QSet>
#include <QTimer>

namespace ppcb {

// What an editor was told to move, turn or delete and kept because it is
// locked: shown red for a moment, so that it is seen why nothing moved.
class LockFlash : public QObject
{
    Q_OBJECT

public:
    explicit LockFlash(QObject *parent = nullptr);

    // Shows 'ids', and the board's outline with 'outline', for a moment;
    // nothing for neither.
    void flash(const QSet<ObjectId> &ids, bool outline = false);
    const QSet<ObjectId> &ids() const { return m_ids; }
    bool outline() const { return m_outline; }

signals:
    // What is shown changed: redraw.
    void changed();

private:
    QTimer m_timer;
    QSet<ObjectId> m_ids;
    bool m_outline = false;
};

} // namespace ppcb
