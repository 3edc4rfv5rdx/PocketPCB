#include "namefield.h"

#include "language.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLineEdit>

namespace ppcb {

NameField::NameField(const char *name, QWidget *parent)
    : QWidget(parent)
{
    m_language = new QComboBox(this);
    m_language->setObjectName(QLatin1String(name) + QLatin1String("Language"));
    for (const QString &language : supportedLanguages())
        m_language->addItem(languageName(language), language);
    m_shown = m_language->itemData(0).toString();
    m_field = new QLineEdit(this);
    m_field->setObjectName(QLatin1String(name));
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_language);
    layout->addWidget(m_field, 1);

    connect(m_field, &QLineEdit::editingFinished, this, &NameField::edited);
    connect(m_language, &QComboBox::activated, this, [this](int index) {
        emit edited();
        m_shown = m_language->itemData(index).toString();
        m_field->setText(m_names.value(m_shown));
    });
}

QString NameField::title()
{
    return tr("Name");
}

void NameField::setNames(const QMap<QString, QString> &names, bool force)
{
    m_names = names;
    if (force || !m_field->hasFocus())
        m_field->setText(m_names.value(m_shown));
}

QMap<QString, QString> NameField::taken(QMap<QString, QString> names) const
{
    const QString name = m_field->text().trimmed();
    if (name.isEmpty())
        names.remove(m_shown);
    else
        names.insert(m_shown, name);
    return names;
}

} // namespace ppcb
