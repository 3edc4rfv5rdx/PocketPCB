#include "rulescheck.h"

#include "document.h"

namespace ppcb {

RulesCheck::RulesCheck(Document *document, std::function<bool()> wanted,
                       std::function<void()> run, QObject *parent)
    : QObject(parent)
    , m_wanted(std::move(wanted))
    , m_run(std::move(run))
{
    connect(document, &Document::designReset, this, [this] { m_resetPending = true; });
    connect(document, &Document::designChanged, this, [this] {
        m_stale = true;
        if (m_resetPending) {
            m_resetPending = false;
            m_waiting = true;
            emit waitingChanged();
            return;
        }
        if (m_waiting) {
            m_waiting = false;
            emit waitingChanged();
        }
        poke();
    });
}

void RulesCheck::check()
{
    const bool waited = m_waiting;
    m_waiting = false;
    m_stale = false;
    m_run();
    if (waited)
        emit waitingChanged();
}

void RulesCheck::poke()
{
    if (!m_stale || m_waiting || !m_wanted())
        return;
    m_stale = false;
    m_run();
}

} // namespace ppcb
