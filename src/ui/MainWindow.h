#pragma once

#include <QMainWindow>
#include <QString>

class QAction;
class QTextDocument;
class QTextEdit;

namespace typist {

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  void openFile(const QString& path);

 private slots:
  void openDialog();
  void save();
  void saveAs();
  void undo();
  void redo();

 private:
  void updateTitleAndStatus();
  void markClean();

  QTextEdit* m_editor = nullptr;
  QTextDocument* m_document = nullptr;
  QAction* m_undoAction = nullptr;
  QAction* m_redoAction = nullptr;
  QString m_currentPath;
  QString m_savedMarkdown;
  bool m_dirty = false;
};

}  // namespace typist
