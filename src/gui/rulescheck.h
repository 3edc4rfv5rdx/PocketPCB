#pragma once

#include <QObject>

#include <functional>

namespace ppcb {

class Document;

// When a rules check (ERC, DRC) runs: after a change, while it is wanted
// (its panel open and its page shown), else as soon as it is wanted again;
// after a project is opened or started, not until a change or check(), as
// a check of a large project is slow.
class RulesCheck : public QObject
{
    Q_OBJECT

public:
    RulesCheck(Document *document, std::function<bool()> wanted, std::function<void()> run,
               QObject *parent = nullptr);

    // Runs the check now, whatever it was waiting for.
    void check();
    // What makes the check wanted may have changed: it runs when the design
    // changed since it last ran and it is not waiting for check().
    void poke();
    // A project was opened or started and the check has not run since.
    bool waiting() const { return m_waiting; }

signals:
    // waiting() changed.
    void waitingChanged();

private:
    std::function<bool()> m_wanted;
    std::function<void()> m_run;
    // The design changed since the check last ran.
    bool m_stale = true;
    bool m_waiting = false;
    // designReset() came; its designChanged() is next.
    bool m_resetPending = false;
};

} // namespace ppcb
