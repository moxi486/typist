#pragma once

#include <QString>

namespace typist::platform {

enum class DarkHint { Unknown, Light, Dark };

// OS-level dark-mode signal; Unknown when nothing readable on this platform.
DarkHint nativeDarkHint();

// Pure conversions keep the parsing rules testable without a desktop session.
DarkHint darkHintFromAppsUseLightTheme(int value);
DarkHint darkHintFromGtkTheme(const QString& themeValue);

}  // namespace typist::platform
