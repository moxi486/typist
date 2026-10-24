#pragma once

#include <QMainWindow>
#include <QString>

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

 private:
  void updateTitleAndStatus();

  QTextEdit* m_editor = nullptr;
  QTextDocument* m_document = nullptr;
  QString m_currentPath;
  bool m_dirty = false;
};

}  // namespace typist
