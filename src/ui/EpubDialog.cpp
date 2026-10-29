#include "EpubDialog.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>

namespace typist::ui {
EpubDialog::EpubDialog(const QString& title, const QString& author, const QString& language,
                       QWidget* parent)
    : QDialog(parent) {
  if (parent) setPalette(parent->palette());
  setObjectName(QStringLiteral("epubDialog"));
  setMinimumWidth(480);
  auto* form = new QFormLayout(this);
  form->setContentsMargins(24, 22, 24, 22);
  form->setSpacing(14);
  m_titleCaption = new QLabel(this);
  m_authorCaption = new QLabel(this);
  m_languageCaption = new QLabel(this);
  m_title = new QLineEdit(title, this);
  m_title->setObjectName(QStringLiteral("epubTitle"));
  m_author = new QLineEdit(author, this);
  m_author->setObjectName(QStringLiteral("epubAuthor"));
  m_language = new QLineEdit(language, this);
  m_language->setObjectName(QStringLiteral("epubLanguage"));
  form->addRow(m_titleCaption, m_title);
  form->addRow(m_authorCaption, m_author);
  form->addRow(m_languageCaption, m_language);
  m_hint = new QLabel(this);
  m_hint->setWordWrap(true);
  form->addRow(m_hint);
  auto* buttons = new QDialogButtonBox(this);
  m_export = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
  m_export->setObjectName(QStringLiteral("confirmEpub"));
  m_cancel = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
  m_cancel->setObjectName(QStringLiteral("cancelEpub"));
  form->addRow(buttons);
  setStyleSheet(QStringLiteral(
      "QPushButton#confirmEpub{background:#3d72bb;color:white;border:0;border-radius:5px;"
      "padding:8px "
      "20px;}QPushButton#confirmEpub:disabled{background:palette(mid);color:palette(text);}"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_title, &QLineEdit::textChanged, this, &EpubDialog::validate);
  connect(m_language, &QLineEdit::textChanged, this, &EpubDialog::validate);
  retranslate();
  m_title->selectAll();
  m_title->setFocus();
}
QString EpubDialog::bookTitle() const { return m_title->text().trimmed(); }
QString EpubDialog::author() const { return m_author->text().trimmed(); }
QString EpubDialog::language() const { return m_language->text().trimmed(); }
void EpubDialog::validate() {
  static const QRegularExpression tag(QStringLiteral("^[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*$"));
  m_export->setEnabled(!bookTitle().isEmpty() && tag.match(language()).hasMatch());
}
void EpubDialog::retranslate() {
  setWindowTitle(tr("Export EPUB"));
  m_titleCaption->setText(tr("Book title"));
  m_authorCaption->setText(tr("Author"));
  m_author->setPlaceholderText(tr("Optional"));
  m_languageCaption->setText(tr("Book language"));
  m_language->setPlaceholderText(QStringLiteral("zh-CN / en"));
  m_hint->setText(tr(
      "Use a language tag such as zh-CN or en. Headings become the reader's table of contents."));
  m_export->setText(tr("Export"));
  m_cancel->setText(tr("Cancel"));
  validate();
}
void EpubDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslate();
  QDialog::changeEvent(event);
}
}  // namespace typist::ui
