#pragma once

#include <QMainWindow>
#include <QString>
#include <QPalette>

class QAction;
class QCloseEvent;
class QTextDocument;
class QTextEdit;

namespace typist::ui {
class AutoSave;
class ViewSwitch;
}

namespace typist {

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  void openFile(const QString& path);

  // Typing-to-repaint latency for the performance bench: one edit plus a synchronous repaint.
  qint64 measureTypingRepaint();

  // Lets the bench confirm a file actually loaded before timing keystrokes.
  int documentBlockCount() const;

  // Bench seam: an edit, a switch to source and back must leave undo available.
  bool viewSwitchKeepsUndoForBench();

  // Synthetic keystrokes in bench mode must not be persisted to the file under test.
  void setBenchMode(bool enabled);

 private slots:
  void openDialog();
  void save();
  void saveAs();
  void undo();
  void redo();
  void toggleSourceView();
  void exportHtml();
  void exportPdf();
  void cycleTheme();
  void autoSaveNow();

 private:
  void updateTitleAndStatus();
  void markClean();
  QString currentMarkdown() const;
  QString themePreference() const;
  void applyTheme();
  void applyThemePalette(const QPalette& palette);
  void closeEvent(QCloseEvent* event) override;

  QTextEdit* m_editor = nullptr;
  QTextDocument* m_document = nullptr;
  QAction* m_undoAction = nullptr;
  QAction* m_redoAction = nullptr;
  QAction* m_sourceAction = nullptr;
  QAction* m_themeAction = nullptr;
  ui::AutoSave* m_autoSave = nullptr;
  ui::ViewSwitch* m_view = nullptr;
  bool m_benchMode = false;
  QString m_currentPath;
  QString m_savedMarkdown;
  QString m_themePreference;
  bool m_dirty = false;
  bool m_sourceMode = false;
};

}  // namespace typist
