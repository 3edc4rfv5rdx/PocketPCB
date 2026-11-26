#include "alerts.h"

#include "appsettings.h"

#include <QCheckBox>
#include <QMessageBox>

namespace ppcb {

namespace {

bool boxesShown = true;

} // namespace

void Alerts::show(QWidget *parent, const Alert &alert,
                  const std::function<void(const QString &)> &status)
{
    if (alert.isEmpty())
        return;
    if (status)
        status(alert.text);
    if (alert.level == AlertLevel::Info || !boxesShown)
        return;
    const QString title = QCoreApplication::applicationName();
    if (alert.level == AlertLevel::Error) {
        QMessageBox::critical(parent, title, alert.text);
        return;
    }
    QStringList hidden = AppSettings::hiddenWarnings();
    if (!alert.key.isEmpty() && hidden.contains(alert.key))
        return;
    QMessageBox box(QMessageBox::Warning, title, alert.text, QMessageBox::Ok, parent);
    box.setObjectName(QStringLiteral("warningBox"));
    QCheckBox *again = nullptr;
    if (!alert.key.isEmpty()) {
        again = new QCheckBox(tr("Do not show again"), &box);
        box.setCheckBox(again);
    }
    box.exec();
    if (again != nullptr && again->isChecked()) {
        hidden.append(alert.key);
        AppSettings::setHiddenWarnings(hidden);
    }
}

void Alerts::setBoxesShown(bool shown)
{
    boxesShown = shown;
}

QString Alerts::showWarningsAgainTitle()
{
    return tr("Show all warnings again");
}

} // namespace ppcb
