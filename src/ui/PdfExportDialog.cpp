#include "PdfExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

namespace typist::ui {
PdfExportDialog::PdfExportDialog(const QTextDocument* document,
                                 const platform::PdfPageOptions& initial, QWidget* parent)
    : QDialog(parent), m_document(document), m_title(initial.title) {
  if (parent) setPalette(parent->palette());
  setObjectName(QStringLiteral("pdfExportDialog"));
  resize(980, 760);
  setMinimumSize(800, 580);
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(24, 22, 24, 22);
  outer->setSpacing(16);
  auto* row = new QHBoxLayout;
  row->setSpacing(24);
  auto* controls = new QWidget(this);
  controls->setFixedWidth(270);
  auto* form = new QFormLayout(controls);
  form->setContentsMargins(0, 0, 0, 0);
  form->setRowWrapPolicy(QFormLayout::WrapAllRows);
  form->setSpacing(12);
  m_paper = new QComboBox(controls);
  m_paper->setObjectName(QStringLiteral("pdfPaper"));
  m_paper->addItem(QStringLiteral("A4"), QStringLiteral("a4"));
  m_paper->addItem(QStringLiteral("Letter"), QStringLiteral("letter"));
  m_paper->setCurrentIndex(initial.paper == QLatin1String("letter") ? 1 : 0);
  m_orientation = new QComboBox(controls);
  m_orientation->setObjectName(QStringLiteral("pdfOrientation"));
  m_orientation->addItems({QString(), QString()});
  m_orientation->setCurrentIndex(initial.landscape ? 1 : 0);
  m_margin = new QDoubleSpinBox(controls);
  m_margin->setObjectName(QStringLiteral("pdfMargin"));
  m_margin->setRange(5, 60);
  m_margin->setDecimals(1);
  m_margin->setSuffix(QStringLiteral(" mm"));
  m_margin->setValue(initial.marginMm);
  m_header = new QLineEdit(initial.header, controls);
  m_header->setObjectName(QStringLiteral("pdfHeader"));
  m_footer = new QLineEdit(initial.footer, controls);
  m_footer->setObjectName(QStringLiteral("pdfFooter"));
  m_header->setMaxLength(200);
  m_footer->setMaxLength(200);
  m_paperLabel = new QLabel(controls);
  form->addRow(m_paperLabel, m_paper);
  m_orientationLabel = new QLabel(controls);
  form->addRow(m_orientationLabel, m_orientation);
  m_marginLabel = new QLabel(controls);
  form->addRow(m_marginLabel, m_margin);
  m_headerLabel = new QLabel(controls);
  form->addRow(m_headerLabel, m_header);
  m_footerLabel = new QLabel(controls);
  form->addRow(m_footerLabel, m_footer);
  m_hint = new QLabel(controls);
  m_hint->setWordWrap(true);
  form->addRow(m_hint);
  m_repeat = new QCheckBox(controls);
  m_repeat->setObjectName(QStringLiteral("pdfRepeatHeader"));
  m_repeat->setChecked(initial.repeatTableHeader);
  form->addRow(m_repeat);
  row->addWidget(controls, 0, Qt::AlignTop);
  auto* previewColumn = new QVBoxLayout;
  m_preview = new QLabel(this);
  m_preview->setObjectName(QStringLiteral("pdfPreview"));
  m_preview->setAlignment(Qt::AlignCenter);
  m_preview->setMinimumSize(1, 1);
  m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  m_preview->setStyleSheet(QStringLiteral("background:#525a65;border-radius:6px;"));
  previewColumn->addWidget(m_preview, 1);
  auto* navigation = new QHBoxLayout;
  m_previous = new QPushButton(this);
  m_previous->setObjectName(QStringLiteral("pdfPreviousPage"));
  m_next = new QPushButton(this);
  m_next->setObjectName(QStringLiteral("pdfNextPage"));
  m_pageLabel = new QLabel(this);
  m_pageLabel->setAlignment(Qt::AlignCenter);
  m_pageLabel->setObjectName(QStringLiteral("pdfPageCount"));
  navigation->addWidget(m_previous);
  navigation->addWidget(m_pageLabel, 1);
  navigation->addWidget(m_next);
  previewColumn->addLayout(navigation);
  row->addLayout(previewColumn, 1);
  outer->addLayout(row, 1);
  m_error = new QLabel(this);
  m_error->setTextFormat(Qt::PlainText);
  m_error->setWordWrap(true);
  outer->addWidget(m_error);
  auto* buttons = new QDialogButtonBox(this);
  m_export = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
  m_export->setObjectName(QStringLiteral("confirmPdfExport"));
  m_export->setEnabled(false);
  m_cancel = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
  m_cancel->setObjectName(QStringLiteral("cancelPdfExport"));
  outer->addWidget(buttons);
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  setStyleSheet(
      QStringLiteral(
          "QPushButton#confirmPdfExport{background:#3d72bb;color:white;border:0;border-radius:5px;"
          "padding:8px 20px;}"
          "QPushButton#confirmPdfExport:disabled{background:palette(mid);color:palette(text);}"
          "QComboBox,QDoubleSpinBox{background:%1;color:%2;border:1px solid "
          "%3;border-radius:5px;padding:6px;}"
          "QComboBox QAbstractItemView{background:%1;color:%2;selection-background-color:#3d639d;}")
          .arg(dark ? "#363b40" : "#ffffff", dark ? "#e0e0e0" : "#333333",
               dark ? "#484e55" : "#d2d8df"));
  m_refresh = new QTimer(this);
  m_refresh->setSingleShot(true);
  m_refresh->setInterval(150);
  connect(m_refresh, &QTimer::timeout, this, &PdfExportDialog::refreshPreview);
  const auto update = [this] {
    m_page = 0;
    m_export->setEnabled(false);
    m_refresh->start();
  };
  connect(m_paper, &QComboBox::currentIndexChanged, this, update);
  connect(m_orientation, &QComboBox::currentIndexChanged, this, update);
  connect(m_margin, &QDoubleSpinBox::valueChanged, this, update);
  connect(m_header, &QLineEdit::textChanged, this, update);
  connect(m_footer, &QLineEdit::textChanged, this, update);
  connect(m_repeat, &QCheckBox::toggled, this, update);
  connect(m_previous, &QPushButton::clicked, this, [this] {
    if (m_page > 0) {
      --m_page;
      refreshPreview();
    }
  });
  connect(m_next, &QPushButton::clicked, this, [this] {
    if (m_page + 1 < m_pages) {
      ++m_page;
      refreshPreview();
    }
  });
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  retranslate();
  m_refresh->start(0);
}
platform::PdfPageOptions PdfExportDialog::options() const {
  platform::PdfPageOptions result;
  result.paper = m_paper->currentData().toString();
  result.landscape = m_orientation->currentIndex() == 1;
  result.marginMm = m_margin->value();
  result.header = m_header->text();
  result.footer = m_footer->text();
  result.repeatTableHeader = m_repeat->isChecked();
  result.title = m_title;
  return result;
}
void PdfExportDialog::refreshPreview() {
  m_refresh->stop();
  QString error;
  m_image = platform::pdfPagePreview(m_document, options(), m_page, &m_pages, &error);
  m_page = qBound(0, m_page, qMax(0, m_pages - 1));
  m_export->setEnabled(!m_image.isNull());
  m_previous->setEnabled(m_page > 0);
  m_next->setEnabled(m_page + 1 < m_pages);
  m_error->setText(m_image.isNull() ? tr("Cannot preview this document: %1").arg(error)
                                    : QString());
  m_pageLabel->setText(tr("Page %1 of %2").arg(m_pages ? m_page + 1 : 0).arg(m_pages));
  showImage();
}
void PdfExportDialog::showImage() {
  if (!m_preview) return;
  if (m_image.isNull()) {
    m_preview->clear();
    return;
  }
  const auto size = m_preview->size() - QSize(24, 24);
  if (size.width() > 0 && size.height() > 0)
    m_preview->setPixmap(
        QPixmap::fromImage(m_image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}
void PdfExportDialog::retranslate() {
  setWindowTitle(tr("Export PDF"));
  m_paperLabel->setText(tr("Paper size"));
  m_orientationLabel->setText(tr("Orientation"));
  m_orientation->setItemText(0, tr("Portrait"));
  m_orientation->setItemText(1, tr("Landscape"));
  m_marginLabel->setText(tr("Margins"));
  m_headerLabel->setText(tr("Header"));
  m_footerLabel->setText(tr("Footer"));
  m_header->setPlaceholderText(tr("Leave empty to hide"));
  m_footer->setPlaceholderText(tr("Leave empty to hide"));
  m_hint->setText(tr("{title} inserts the document name. # inserts the page number."));
  m_repeat->setText(tr("Repeat table headers"));
  m_previous->setText(tr("Previous"));
  m_next->setText(tr("Next"));
  m_export->setText(tr("Export"));
  m_cancel->setText(tr("Cancel"));
  m_preview->setAccessibleName(tr("PDF page preview"));
  m_pageLabel->setText(tr("Page %1 of %2").arg(m_pages ? m_page + 1 : 0).arg(m_pages));
}
void PdfExportDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslate();
  QDialog::changeEvent(event);
}
void PdfExportDialog::resizeEvent(QResizeEvent* event) {
  QDialog::resizeEvent(event);
  showImage();
}
}  // namespace typist::ui
