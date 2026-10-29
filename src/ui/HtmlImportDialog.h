#pragma once
#include <QDialog>
namespace typist::editor {
struct HtmlImportResult;
}
class QLabel;
class QPushButton;
class QTextEdit;
namespace typist::ui {
class HtmlImportDialog : public QDialog {
  Q_OBJECT
 public:
  HtmlImportDialog(const editor::HtmlImportResult& result, QWidget* parent = nullptr);

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void retranslate();
  QLabel *m_heading, *m_hint;
  QTextEdit* m_preview;
  QPushButton *m_import, *m_cancel;
};
}  // namespace typist::ui
