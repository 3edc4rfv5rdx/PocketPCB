#pragma once

#include <QString>
#include <QStringList>

class QTranslator;

namespace ppcb {

// Language codes of the interface. The first one is the language of the
// strings in the code: it is the fallback and needs no translation file;
// the others are those of the embedded translations, in the order of
// their codes.
QStringList supportedLanguages();

// The name of the language 'code' in that language itself ("Українська"),
// as a list of languages shows it; 'code' itself for one Qt does not know.
QString languageName(const QString &code);

// Picks the interface language. 'preferred' is the user's setting (empty:
// follow the system); 'systemLanguages' is the system's list, most wanted
// first, in any of the forms "ru", "ru-RU", "ru_RU".
QString chooseLanguage(const QString &preferred, const QStringList &systemLanguages);

// Loads the embedded translation of 'language' into 'translator'.
// Returns false when there is none; the source language has none by design.
bool loadTranslation(QTranslator &translator, const QString &language);

// The language chosen from the settings and the system.
QString currentLanguage();

// The languages a name given in several languages is looked up in, most
// wanted first: the interface language, then the language of the code.
QStringList nameLanguages();

// Applies currentLanguage() to the running application and returns it.
QString installLanguage();

} // namespace ppcb
