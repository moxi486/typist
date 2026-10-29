#pragma once
#include "LinkNavigation.h"

#include <QDialog>
class QLineEdit;
class QComboBox;
class QLabel;
class QPushButton;
class QDialogButtonBox;
namespace typist::ui {
class LinkDialog : public QDialog {
  Q_OBJECT
 public:
  LinkDialog(const QString& label, const QString& target, const QList<HeadingLink>& headings,
             const QUrl& base, QWidget* parent = nullptr);
  QString destination() const;
  QString label() const;

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void retranslate();
  QLineEdit *m_label, *m_target;
  QLabel *m_labelCaption, *m_targetCaption, *m_hint;
  QComboBox* m_headings;
  QPushButton* m_browse;
  QPushButton *m_apply, *m_cancel;
  QDialogButtonBox* m_buttons;
};
}  // namespace typist::ui
