#pragma once

#include <QString>

namespace typist::ui {

enum class Theme { Light, Dark };

// "system" follows the host palette hint; anything else is taken literally, and an
// unknown value falls back to light.
Theme resolveTheme(const QString& preference, bool systemPrefersDark);

// Preference name written to settings: system, light or dark.
QString themeName(Theme theme);

// QSS for the editor widget in the given theme.
QString editorStyleSheet(Theme theme);

// CSS embedded in exported HTML so an exported page keeps the same look as the editor.
QString htmlStyle(Theme theme);

}  // namespace typist::ui
