#include "HtmlImportDialog.h"

#include "HtmlImporter.h"
#include "editor_document.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

namespace typist::ui {
HtmlImportDialog::HtmlImportDialog(const editor::HtmlImportResult& result, QWidget* parent)
    : QDialog(parent) {
  if (parent) setPalette(parent->palette());
  setObjectName(QStringLiteral("htmlImportDialog"));
  resize(760, 640);
  setMinimumSize(480, 420);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 22);
  layout->setSpacing(14);
  m_heading = new QLabel(this);
  layout->addWidget(m_heading);
  m_preview = new QTextEdit(this);
  m_preview->setObjectName(QStringLiteral("htmlImportPreview"));
  m_preview->setReadOnly(true);
  m_preview->setToolTip(result.title);
  editor::loadMarkdown(result.markdown, m_preview->document(), nullptr);
  layout->addWidget(m_preview, 1);
  auto* notes = new QLabel(result.notices.join('\n'), this);
  notes->setObjectName(QStringLiteral("htmlImportNotes"));
  notes->setTextFormat(Qt::PlainText);
  notes->setWordWrap(true);
  layout->addWidget(notes);
  m_hint = new QLabel(this);
  m_hint->setWordWrap(true);
  layout->addWidget(m_hint);
  auto* buttons = new QDialogButtonBox(this);
  m_import = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
  m_import->setObjectName(QStringLiteral("confirmHtmlImport"));
  m_cancel = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
  m_cancel->setObjectName(QStringLiteral("cancelHtmlImport"));
  layout->addWidget(buttons);
  setStyleSheet(QStringLiteral(
      "QPushButton#confirmHtmlImport{background:#3d72bb;color:white;border:0;border-radius:5px;"
      "padding:8px 20px;}"));
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  retranslate();
}
void HtmlImportDialog::retranslate() {
  setWindowTitle(tr("Import HTML"));
  m_heading->setText(tr("Preview converted Markdown"));
  m_preview->setAccessibleName(tr("Converted document preview"));
  m_hint->setText(
      tr("Import creates an unsaved Markdown document. The HTML source stays unchanged."));
  m_import->setText(tr("Import"));
  m_cancel->setText(tr("Cancel"));
}
void HtmlImportDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslate();
  QDialog::changeEvent(event);
}
}  // namespace typist::ui
