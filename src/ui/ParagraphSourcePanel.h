#pragma once
#include <QFrame>
class QPlainTextEdit;
class QLabel;
namespace typist::ui {
class ParagraphSourcePanel : public QFrame {
  Q_OBJECT
 public:
  explicit ParagraphSourcePanel(QWidget* parent);
  void setSource(const QString& source);
  QString source() const;
  QPlainTextEdit* input() const { return m_input; }
  void setDark(bool dark);
 signals:
  void draftChanged();
  void applyRequested();
  void cancelRequested();
  void focusLeft();

 protected:
  bool eventFilter(QObject* object, QEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  QPlainTextEdit* m_input = nullptr;
};
}  // namespace typist::ui
