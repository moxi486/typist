#include "Settings.h"

#include <QSettings>

namespace typist::platform {

namespace {
QSettings openSettings() { return QSettings(QStringLiteral("Typist"), QStringLiteral("Typist")); }
}  // namespace

QString themePreference(const QString& fallback) {
  return openSettings().value(QStringLiteral("theme"), fallback).toString();
}

void setThemePreference(const QString& value) {
  QSettings store = openSettings();
  store.setValue(QStringLiteral("theme"), value);
  store.sync();
}

}  // namespace typist::platform
