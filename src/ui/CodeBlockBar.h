#pragma once
#include <QFrame>
class QComboBox;
class QPushButton;
namespace typist::ui {
class CodeBlockBar : public QFrame {
  Q_OBJECT
 public:
  explicit CodeBlockBar(QWidget* parent);
  void setLanguage(const QString& language);
  void setDark(bool dark);
  void showCopied();
 signals:
  void languageRequested(const QString& language);
  void copyRequested();
  void exitRequested();
  void returnToEditor();

 protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* object, QEvent* event) override;

 private:
  void retranslate();
  QComboBox* m_language = nullptr;
  QPushButton* m_copy = nullptr;
  QPushButton* m_exit = nullptr;
  QString m_current;
  bool m_copied = false;
};
}  // namespace typist::ui
