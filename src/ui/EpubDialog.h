#pragma once
#include <QDialog>
class QLineEdit;
class QLabel;
class QPushButton;
class QCheckBox;
class QSpinBox;
class QTabWidget;
class QPlainTextEdit;
namespace typist::platform {
struct EpubPublicationDetails;
}
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
  int tocDepth() const;
  void setTocDepth(int depth);
  platform::EpubPublicationDetails publicationDetails() const;

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
  QLabel* m_tocDepthCaption;
  QSpinBox* m_tocDepth;
  QTabWidget* m_tabs;
  QLineEdit *m_subtitle, *m_publisher, *m_identifier, *m_rights;
  QPlainTextEdit* m_description;
  QLabel *m_subtitleCaption, *m_publisherCaption, *m_identifierCaption, *m_rightsCaption,
      *m_descriptionCaption;
  bool m_coverValid = true;
};
}  // namespace typist::ui
