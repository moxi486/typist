#pragma once

#include <QString>
#include <QStringList>
#include <QTranslator>

namespace typist::ui {

// Ordered .qm lookup paths for a locale; empty for English, which is the source language.
QStringList translationCandidates(const QString& localeName);

// Loads the first existing catalog for the locale and installs it on the application.
bool installTranslatorForLocale(QTranslator* translator, const QString& localeName);

}  // namespace typist::ui
