#pragma once

#include <QMap>
#include <QString>
#include <QWidget>

class QComboBox;
class QLineEdit;

namespace ppcb {

// A name given in every supported language: a box chooses the language,
// the field beside it holds the name in it, English at first. What was
// typed for one language is handed on (edited()) before the field shows
// another.
class NameField : public QWidget
{
    Q_OBJECT

public:
    // The field is named 'name', the box 'name' + "Language".
    NameField(const char *name, QWidget *parent = nullptr);

    // Shows 'names': the field the one in its language, left alone while
    // typed in unless 'force'.
    void setNames(const QMap<QString, QString> &names, bool force = true);
    // 'names' with what the field holds put in for its language; an empty
    // field removes that language.
    QMap<QString, QString> taken(QMap<QString, QString> names) const;
    QLineEdit *field() const { return m_field; }
    // The label of the field in a form.
    static QString title();

signals:
    // The field was left, or another language is about to be shown: the
    // owner takes what was typed (taken()).
    void edited();

private:
    QComboBox *m_language = nullptr;
    QLineEdit *m_field = nullptr;
    // The language whose name the field shows.
    QString m_shown;
    QMap<QString, QString> m_names;
};

} // namespace ppcb
