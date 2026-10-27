#include "SystemTheme.h"

#include <QByteArray>
#include <QFile>
#include <QSettings>
#include <QStringList>
#include <QVariant>

#include <cstdlib>

#if defined(Q_OS_MACOS)
#include <QProcess>
#endif

namespace typist::platform {

DarkHint darkHintFromAppsUseLightTheme(int value) { return value == 0 ? DarkHint::Dark : DarkHint::Light; }

DarkHint darkHintFromGtkTheme(const QString& themeValue) {
  if (themeValue.isEmpty()) return DarkHint::Unknown;
  const QString lowered = themeValue.toLower();
  if (lowered.contains(QStringLiteral("dark"))) return DarkHint::Dark;
  return DarkHint::Light;
}

namespace {

DarkHint hintFromGtkSettingsFile() {
  const QByteArray home = qgetenv("HOME");
  if (home.isEmpty()) return DarkHint::Unknown;
  QFile file(QString::fromUtf8(home) + QStringLiteral("/.config/gtk-3.0/settings.ini"));
  if (!file.open(QIODevice::ReadOnly)) return DarkHint::Unknown;
  const QString text = QString::fromUtf8(file.readAll());
  file.close();
  for (const QString& line : text.split(QChar(10))) {
    if (!line.contains(QStringLiteral("gtk-theme-name"))) continue;
    return darkHintFromGtkTheme(line.section(QChar(61), 1).trimmed());
  }
  return DarkHint::Unknown;
}

}  // namespace

DarkHint nativeDarkHint() {
#if defined(Q_OS_WIN)
  QSettings settings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("Microsoft"),
                    QStringLiteral("Windows"));
  // QSettings already prefixes Software/<org>/<app>, so the key is relative to that tree.
  const QVariant value = settings.value(QStringLiteral("CurrentVersion/Themes/Personalize/AppsUseLightTheme"));
  if (!value.isValid()) return DarkHint::Unknown;
  return darkHintFromAppsUseLightTheme(value.toInt());
#elif defined(Q_OS_MACOS)
  // The key is absent when the session uses the light appearance, so a failed read means light.
  const int status = QProcess::execute(QStringLiteral("defaults"),
                                      {QStringLiteral("read"), QStringLiteral("-g"), QStringLiteral("AppleInterfaceStyle")});
  if (status != 0) return DarkHint::Light;
  return DarkHint::Dark;
#elif defined(Q_OS_LINUX)
  const QByteArray env = qgetenv("GTK_THEME");
  if (!env.isEmpty()) return darkHintFromGtkTheme(QString::fromUtf8(env));
  return hintFromGtkSettingsFile();
#else
  return DarkHint::Unknown;
#endif
}

}  // namespace typist::platform
