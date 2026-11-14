#pragma once
#include <QFrame>
class QPlainTextEdit;
class QLabel;
class QTextDocument;
namespace typist::ui {
class ParagraphSourcePanel : public QFrame {
  Q_OBJECT
 public:
  explicit ParagraphSourcePanel(QWidget* parent);
  void setSource(const QString& source, QTextDocument* document);
  QString source() const;
  QPlainTextEdit* input() const { return m_input; }
  void setDark(bool dark);
 signals:
  void draftChanged();
  void applyRequested();
  void cancelRequested();
  void focusLeft();
  void imagePasteRequested();
  void clipboardRequested(bool cut);
  void indentRequested(bool outdent);
  void markdownFileDropped(const QString& path);
  void folderDropped(const QString& path);

 protected:
  bool eventFilter(QObject* object, QEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  QPlainTextEdit* m_input = nullptr;
  QTextDocument* m_document = nullptr;
};
}  // namespace typist::ui
