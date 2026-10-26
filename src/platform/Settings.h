#pragma once

#include <QString>

namespace typist::platform {

// User settings live under the Typist organization so each platform picks its own
// native store: registry on Windows, ini under XDG on Linux and macOS.
QString themePreference(const QString& fallback = QStringLiteral("system"));

void setThemePreference(const QString& value);

}  // namespace typist::platform
