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
  const bool dark = theme == Theme::Dark;
  const QString canvas = dark ? QStringLiteral("#363b40") : QStringLiteral("#ffffff");
  const QString surface = dark ? QStringLiteral("#2e3338") : QStringLiteral("#fafafa");
  const QString text = dark ? QStringLiteral("#e0e0e0") : QStringLiteral("#333333");
  const QString muted = dark ? QStringLiteral("#aab0b6") : QStringLiteral("#888888");
  const QString border = dark ? QStringLiteral("#484e55") : QStringLiteral("#e8e8e8");
  const QString hover = dark ? QStringLiteral("#42484f") : QStringLiteral("#eeeeee");
  const QString accent = dark ? QStringLiteral("#8ab9d8") : QStringLiteral("#4183c4");
  return QStringLiteral(R"qss(
    QMainWindow, QWidget { background-color: %1; color: %3; }
    QTextEdit { background-color: %1; color: %3; border: none; selection-background-color: #b5d5f2; selection-color: #222222; }
    QMenuBar { background: %1; color: %3; padding: 2px 8px; border: none; }
    QMenuBar::item { background: transparent; padding: 4px 10px; }
    QMenuBar::item:selected, QMenuBar::item:pressed { background: %6; color: %3; }
    QMenu { background: %2; color: %3; border: 1px solid %5; padding: 5px; }
    QMenu::item { padding: 6px 30px 6px 14px; }
    QMenu::item:selected { background: %6; color: %7; }
    QMenu::item:disabled { color: %4; }
    QMenu::separator { height: 1px; background: %5; margin: 5px 8px; }
    QToolButton, QPushButton { background: transparent; color: %3; border: 1px solid transparent; border-radius: 3px; padding: 5px 10px; }
    QToolButton:hover, QPushButton:hover { background: %6; }
    QToolButton:checked { background: %6; color: %7; border-color: %5; }
    QToolButton:disabled, QPushButton:disabled { color: %4; }
    QStatusBar { background: %1; color: %4; border: none; padding: 0 12px 3px 10px; font-size: 11px; }
    QStatusBar QToolButton { padding: 3px 6px; border: none; }
    QStatusBar QLabel { background: transparent; color: %4; }
    QStatusBar::item { border: none; }
    QDockWidget { color: %4; border: none; }
    QDockWidget::title { background: %2; padding: 10px; }
    QWidget#sidebarSurface { background: %2; border-right: 1px solid %5; }
    QLabel#workspaceName { background: %2; color: %3; font-size: 13px; font-weight: 600; padding: 18px 18px 12px; }
    QTabWidget#sidebarTabs { background: %2; }
    QTabWidget::pane { border: none; background: %2; }
    QTabBar { background: %2; }
    QTabBar::tab { background: %2; color: %4; padding: 9px 22px; border-bottom: 1px solid %5; }
    QTabBar::tab:selected { color: %3; border-bottom: 2px solid %4; }
    QTabBar::tab:hover { color: %3; }
    QTreeWidget, QListWidget { background: %2; color: %3; border: none; padding: 10px 6px; outline: 0; font-size: 13px; }
    QTreeWidget::item, QListWidget::item { padding: 6px 8px; }
    QTreeWidget::item:selected, QListWidget::item:selected { background: %6; color: %7; }
    QLineEdit { background: %1; color: %3; border: 1px solid %5; border-radius: 5px; padding: 7px; selection-background-color: #3d639d; }
    QCheckBox { color: %3; background: transparent; spacing: 6px; }
    QScrollBar:vertical { background: transparent; width: 7px; margin: 1px; }
    QScrollBar::handle:vertical { background: %5; border-radius: 2px; min-height: 30px; }
    QScrollBar::handle:vertical:hover { background: %4; }
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
    QToolTip { background: %2; color: %3; border: 1px solid %5; padding: 6px; }
  )qss")
      .arg(canvas, surface, text, muted, border, hover, accent);
}

QString htmlStyle(Theme theme) {
  QString style = QStringLiteral(
      "body { max-width: 68ch; margin: 2rem auto; padding: 0 1rem; line-height: 1.6; ");
  if (theme == Theme::Dark) {
    style += QStringLiteral("background: #1f2328; color: #e6e6e6; } a { color: #7fb3e8; } ");
  } else {
    style += QStringLiteral("background: #ffffff; color: #1f2328; } a { color: #2f6fb3; } ");
  }
  style += QStringLiteral("code, pre { font-family: monospace; } pre { overflow: auto; } ");
  style += QStringLiteral(
      "table { border-collapse: collapse; } th, td { border: 1px solid #bbb; padding: 0.4rem "
      "0.6rem; }");
  return style;
}

}  // namespace typist::ui
