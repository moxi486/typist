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
    QFrame#editorToolbar { background: %2; border: 1px solid %5; border-radius: 18px; }
    QFrame#editorToolbar QToolButton { padding: 2px; border-radius: 6px; min-width: 26px; min-height: 24px; }
    QFrame#editorToolbar QToolButton[popupMode="2"] { padding: 2px 14px 2px 5px; }
    QFrame#editorToolbar QToolButton::menu-indicator { subcontrol-position: right center; right: 4px; }
    QFrame#editorToolbar QToolButton:focus { border-color: %7; }
    QFrame#editorToolbar QFrame#editorToolbarDivider { background: %5; }
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
    QScrollBar:horizontal { background: transparent; height: 7px; margin: 1px; }
    QScrollBar::handle:horizontal { background: %5; border-radius: 2px; min-width: 30px; }
    QScrollBar::handle:horizontal:hover { background: %4; }
    QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
    QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
    QToolTip { background: %2; color: %3; border: 1px solid %5; padding: 6px; }
  )qss")
      .arg(canvas, surface, text, muted, border, hover, accent);
}

QString htmlStyle(Theme theme, bool outline) {
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
  if (outline) {
    style += QStringLiteral(R"css(
      body { max-width: 74rem; }
      .export-layout {
        display: grid;
        grid-template-columns: minmax(11rem, 16rem) minmax(0, 1fr);
        gap: 2rem;
        align-items: start;
      }
      .export-content { max-width: 68ch; }
      .export-content img { max-width: 100%; height: auto; }
      .export-outline {
        position: sticky;
        top: 1rem;
        max-height: calc(100vh - 2rem);
        overflow: auto;
        border-inline-end: 1px solid #9996;
        padding-inline-end: 1rem;
        font: .9rem/1.5 Inter, system-ui, sans-serif;
        overflow-wrap: anywhere;
      }
      .export-outline h2 { font-size: 1rem; margin: 0 0 .65rem; }
      .export-outline ul { list-style: none; padding: 0; margin: 0; }
      .export-outline ul ul { padding-inline-start: 1rem; }
      .export-outline li { margin: .35rem 0; }
      .export-outline summary { cursor: pointer; }
      .export-outline.flat .outline-level-2 { padding-inline-start: .8rem; }
      .export-outline.flat .outline-level-3 { padding-inline-start: 1.6rem; }
      .export-outline.flat .outline-level-4 { padding-inline-start: 2.4rem; }
      .export-outline.flat .outline-level-5 { padding-inline-start: 3.2rem; }
      .export-outline.flat .outline-level-6 { padding-inline-start: 4rem; }
      @media (max-width: 48rem) {
        .export-layout { display: block; }
        .export-outline {
          position: static;
          max-height: none;
          border-inline-end: 0;
          border-bottom: 1px solid #9996;
          margin-bottom: 1.5rem;
          padding-bottom: 1rem;
        }
      }
      @media print {
        .export-layout { display: block; }
        .export-outline { display: none; }
        .export-content { max-width: none; }
      }
    )css");
  }
  return style;
}

}  // namespace typist::ui
