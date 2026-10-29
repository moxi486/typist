#pragma once
#include <QDialog>
class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
namespace typist::ui {
class EpubDialog : public QDialog {
  Q_OBJECT
 public:
  EpubDialog(const QString& title, const QString& author, const QString& language,
             QWidget* parent = nullptr);
  QString bookTitle() const;
  QString author() const;
  QString language() const;
  QString coverImage() const;
  bool splitChapters() const;
  void setSplitChapters(bool enabled);

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void retranslate();
  void validate();
  void updateCover();
  QLineEdit *m_title, *m_author, *m_language;
  QLabel *m_titleCaption, *m_authorCaption, *m_languageCaption, *m_hint;
  QPushButton *m_export, *m_cancel;
  QLineEdit* m_cover;
  QLabel *m_coverCaption, *m_coverPreview, *m_coverHint;
  QPushButton *m_browseCover, *m_clearCover;
  QCheckBox* m_split;
  bool m_coverValid = true;
};
}  // namespace typist::ui
