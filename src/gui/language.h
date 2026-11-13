#pragma once

#include <QString>
#include <QStringList>

class QTranslator;

namespace ppcb {

// Language codes of the interface. The first one is the language of the
// strings in the code: it is the fallback and needs no translation file.
QStringList supportedLanguages();

// Picks the interface language. 'preferred' is the user's setting (empty:
// follow the system); 'systemLanguages' is the system's list, most wanted
// first, in any of the forms "ru", "ru-RU", "ru_RU".
QString chooseLanguage(const QString &preferred, const QStringList &systemLanguages);

// Loads the embedded translation of 'language' into 'translator'.
// Returns false when there is none; the source language has none by design.
bool loadTranslation(QTranslator &translator, const QString &language);

// Applies the language chosen from the settings and the system to the
// running application. Returns the language code used.
QString installLanguage();

} // namespace ppcb
