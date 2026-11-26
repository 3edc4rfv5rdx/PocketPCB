#pragma once

#include <QCoreApplication>
#include <QString>

#include <functional>

class QWidget;

namespace ppcb {

// What the program tells the user: an error (something could not be done:
// a file not read or written), a warning (something done that may be
// wrong) or anything else, for the status bar.
enum class AlertLevel { Info, Warning, Error };

struct Alert
{
    AlertLevel level = AlertLevel::Info;
    QString text;
    // The kind of a warning, under which the user may ask not to see it
    // again; empty for one always shown.
    QString key;

    bool isEmpty() const { return text.isEmpty(); }

    static Alert info(const QString &text) { return Alert{AlertLevel::Info, text, QString()}; }
    static Alert warning(const QString &text, const QString &key)
    {
        return Alert{AlertLevel::Warning, text, key};
    }
    static Alert error(const QString &text) { return Alert{AlertLevel::Error, text, QString()}; }
};

class Alerts
{
    Q_DECLARE_TR_FUNCTIONS(ppcb::Alerts)

public:
    // Tells 'alert': its text through 'status' (the status bar) always, and
    // an error or a warning in a box over 'parent' as well, a warning with
    // "Do not show again" unless the user asked so. Nothing for an empty one.
    static void show(QWidget *parent, const Alert &alert,
                     const std::function<void(const QString &)> &status);
    // Whether the boxes show: tests, which no one answers, turn them off.
    static void setBoxesShown(bool shown);
    // The menu item that brings back the warnings the user hid.
    static QString showWarningsAgainTitle();
};

} // namespace ppcb
