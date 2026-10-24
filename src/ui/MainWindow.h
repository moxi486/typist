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

 private:
  QTextEdit* m_editor = nullptr;
  QTextDocument* m_document = nullptr;
};

}  // namespace typist
