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

QString languagePreference() {
  const QString value =
      openSettings().value(QStringLiteral("language"), QStringLiteral("system")).toString();
  return value == QStringLiteral("zh") || value == QStringLiteral("en") ? value
                                                                        : QStringLiteral("system");
}

void setLanguagePreference(const QString& value) {
  QSettings store = openSettings();
  store.setValue(QStringLiteral("language"), value);
  store.sync();
}
bool copyImagesToAssets() {
  return openSettings().value(QStringLiteral("images/copyToAssets"), true).toBool();
}
void setCopyImagesToAssets(bool enabled) {
  QSettings store = openSettings();
  store.setValue(QStringLiteral("images/copyToAssets"), enabled);
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

QStringList recentFiles(int limit) {
  QSettings settings = openSettings();
  const QStringList stored = settings.value(QStringLiteral("recentFiles")).toStringList();
  QStringList out;
  for (const QString& path : stored) {
    if (path.isEmpty() || out.contains(path)) continue;
    out.append(path);
    if (limit > 0 && out.size() >= limit) break;
  }
  return out;
}

void addRecentFile(const QString& path) {
  if (path.isEmpty()) return;
  QSettings settings = openSettings();
  QStringList stored = settings.value(QStringLiteral("recentFiles")).toStringList();
  stored.removeOne(path);
  stored.prepend(path);
  while (stored.size() > 8) stored.removeLast();
  settings.setValue(QStringLiteral("recentFiles"), stored);
}

void removeRecentFile(const QString& path) {
  QSettings settings = openSettings();
  QStringList stored = settings.value(QStringLiteral("recentFiles")).toStringList();
  stored.removeOne(path);
  settings.setValue(QStringLiteral("recentFiles"), stored);
}

}  // namespace typist::platform
