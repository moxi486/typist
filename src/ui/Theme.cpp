#include "Theme.h"

namespace typist::ui {

Theme resolveTheme(const QString& preference, bool systemPrefersDark) {
  if (preference == QStringLiteral("light")) return Theme::Light;
  if (preference == QStringLiteral("dark")) return Theme::Dark;
  return systemPrefersDark ? Theme::Dark : Theme::Light;
}

QString themeName(Theme theme) {
  return theme == Theme::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

QString editorStyleSheet(Theme theme) {
  if (theme == Theme::Dark) {
    return QStringLiteral("QTextEdit { background-color: #1f2328; color: #e6e6e6; } ");
  }
  return QStringLiteral("QTextEdit { background-color: #ffffff; color: #1f2328; } ");
}

QString htmlStyle(Theme theme) {
  QString style = QStringLiteral("body { max-width: 68ch; margin: 2rem auto; padding: 0 1rem; line-height: 1.6; ");
  if (theme == Theme::Dark) {
    style += QStringLiteral("background: #1f2328; color: #e6e6e6; } a { color: #7fb3e8; } ");
  } else {
    style += QStringLiteral("background: #ffffff; color: #1f2328; } a { color: #2f6fb3; } ");
  }
  style += QStringLiteral("code, pre { font-family: monospace; } pre { overflow: auto; } ");
  style += QStringLiteral("table { border-collapse: collapse; } th, td { border: 1px solid #bbb; padding: 0.4rem 0.6rem; }");
  return style;
}

}  // namespace typist::ui
