#include "EpubDialog.h"

#include "ImageResources.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>

namespace typist::ui {
EpubDialog::EpubDialog(const QString& title, const QString& author, const QString& language,
                       QWidget* parent)
    : QDialog(parent) {
  if (parent) setPalette(parent->palette());
  setObjectName(QStringLiteral("epubDialog"));
  setMinimumWidth(560);
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
  m_coverCaption = new QLabel(this);
  m_cover = new QLineEdit(this);
  m_cover->setObjectName(QStringLiteral("epubCover"));
  m_browseCover = new QPushButton(this);
  m_browseCover->setObjectName(QStringLiteral("browseEpubCover"));
  m_clearCover = new QPushButton(this);
  m_clearCover->setObjectName(QStringLiteral("clearEpubCover"));
  auto* coverRow = new QHBoxLayout;
  coverRow->addWidget(m_cover, 1);
  coverRow->addWidget(m_browseCover);
  coverRow->addWidget(m_clearCover);
  form->addRow(m_coverCaption, coverRow);
  auto* coverDetails = new QHBoxLayout;
  m_coverPreview = new QLabel(this);
  m_coverPreview->setObjectName(QStringLiteral("epubCoverPreview"));
  m_coverPreview->setFixedSize(96, 128);
  m_coverPreview->setAlignment(Qt::AlignCenter);
  m_coverHint = new QLabel(this);
  m_coverHint->setWordWrap(true);
  coverDetails->addWidget(m_coverPreview);
  coverDetails->addWidget(m_coverHint, 1);
  form->addRow(coverDetails);
  m_split = new QCheckBox(this);
  m_split->setObjectName(QStringLiteral("epubSplitChapters"));
  form->addRow(m_split);
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
  connect(m_cover, &QLineEdit::textChanged, this, &EpubDialog::updateCover);
  connect(m_clearCover, &QPushButton::clicked, m_cover, &QLineEdit::clear);
  connect(m_browseCover, &QPushButton::clicked, this, [this] {
    const auto path = QFileDialog::getOpenFileName(this, tr("Choose Cover Image"), coverImage(),
                                                   tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (!path.isEmpty()) m_cover->setText(path);
  });
  retranslate();
  m_title->selectAll();
  m_title->setFocus();
}
QString EpubDialog::bookTitle() const { return m_title->text().trimmed(); }
QString EpubDialog::author() const { return m_author->text().trimmed(); }
QString EpubDialog::language() const { return m_language->text().trimmed(); }
QString EpubDialog::coverImage() const { return m_cover->text().trimmed(); }
bool EpubDialog::splitChapters() const { return m_split->isChecked(); }
void EpubDialog::setSplitChapters(bool enabled) { m_split->setChecked(enabled); }
void EpubDialog::updateCover() {
  m_coverPreview->clear();
  m_clearCover->setEnabled(!coverImage().isEmpty());
  m_coverPreview->setVisible(!coverImage().isEmpty());
  m_coverValid = coverImage().isEmpty();
  if (!coverImage().isEmpty()) {
    const QFileInfo file(coverImage());
    const auto image = file.isAbsolute() && file.isFile()
                           ? render::imagePreview(coverImage(), QSize(96, 128))
                           : QImage();
    m_coverValid = !image.isNull();
    if (m_coverValid) m_coverPreview->setPixmap(QPixmap::fromImage(image));
  }
  m_coverHint->setText(m_coverValid ? tr("Optional local cover image. It is embedded in the book; "
                                         "the original stays unchanged.")
                                    : tr("Choose a readable local image, or clear the cover."));
  validate();
}
void EpubDialog::validate() {
  static const QRegularExpression tag(QStringLiteral("^[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*$"));
  m_export->setEnabled(!bookTitle().isEmpty() && tag.match(language()).hasMatch() && m_coverValid);
}
void EpubDialog::retranslate() {
  setWindowTitle(tr("Export EPUB"));
  m_titleCaption->setText(tr("Book title"));
  m_authorCaption->setText(tr("Author"));
  m_author->setPlaceholderText(tr("Optional"));
  m_languageCaption->setText(tr("Book language"));
  m_language->setPlaceholderText(QStringLiteral("zh-CN / en"));
  m_coverCaption->setText(tr("Cover image"));
  m_cover->setPlaceholderText(tr("No cover"));
  m_browseCover->setText(tr("Browse..."));
  m_clearCover->setText(tr("Clear"));
  m_coverPreview->setAccessibleName(tr("Cover preview"));
  m_split->setText(tr("Split chapters at level-one headings"));
  m_hint->setText(tr(
      "Use a language tag such as zh-CN or en. Headings become the reader's table of contents."));
  m_export->setText(tr("Export"));
  m_cancel->setText(tr("Cancel"));
  updateCover();
}
void EpubDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslate();
  QDialog::changeEvent(event);
}
}  // namespace typist::ui
