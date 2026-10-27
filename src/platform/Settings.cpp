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

QString pdfPaper(const QString& fallback) {
  const QString value = openSettings().value(QStringLiteral("pdfPaper"), fallback).toString();
  return value == QStringLiteral("letter") ? value : QStringLiteral("a4");
}

void setPdfPaper(const QString& value) {
  QSettings store = openSettings();
  store.setValue(QStringLiteral("pdfPaper"), value);
  store.sync();
}

double pdfMarginMm(double fallback) {
  const QVariant value = openSettings().value(QStringLiteral("pdfMarginMm"), QVariant(fallback));
  bool ok = false;
  const double margin = value.toDouble(&ok);
  return ok && margin >= 5.0 && margin <= 60.0 ? margin : fallback;
}

void setPdfMarginMm(double value) {
  QSettings store = openSettings();
  store.setValue(QStringLiteral("pdfMarginMm"), value);
  store.sync();
}

}  // namespace typist::platform
