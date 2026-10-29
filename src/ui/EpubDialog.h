#pragma once
#include <QDialog>
class QLineEdit;
class QLabel;
class QPushButton;
namespace typist::ui {
class EpubDialog : public QDialog {
  Q_OBJECT
 public:
  EpubDialog(const QString& title, const QString& author, const QString& language,
             QWidget* parent = nullptr);
  QString bookTitle() const;
  QString author() const;
  QString language() const;

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void retranslate();
  void validate();
  QLineEdit *m_title, *m_author, *m_language;
  QLabel *m_titleCaption, *m_authorCaption, *m_languageCaption, *m_hint;
  QPushButton *m_export, *m_cancel;
};
}  // namespace typist::ui
