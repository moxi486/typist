#include "ImageExportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace typist::ui {

ImageExportDialog::ImageExportDialog(const platform::ImageExportOptions& options, QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("imageExportSettings"));
  setWindowTitle(tr("Export Image"));
  auto* layout = new QVBoxLayout(this);
  auto* hint = new QLabel(tr("Create one PNG image from the entire document."), this);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  auto* form = new QFormLayout;
  layout->addLayout(form);
  m_width = new QSpinBox(this);
  m_width->setObjectName(QStringLiteral("imageWidth"));
  m_width->setRange(platform::kMinImageWidth, platform::kMaxImageWidth);
  m_width->setValue(options.width);
  m_width->setSuffix(QStringLiteral(" px"));
  form->addRow(tr("Width"), m_width);
  m_font = new QSpinBox(this);
  m_font->setObjectName(QStringLiteral("imageFontSize"));
  m_font->setRange(platform::kMinImageFont, platform::kMaxImageFont);
  m_font->setValue(options.fontPixels);
  m_font->setSuffix(QStringLiteral(" px"));
  form->addRow(tr("Font Size"), m_font);

  m_scale = new QComboBox(this);
  m_scale->setObjectName(QStringLiteral("imageScale"));
  for (int scale = 1; scale <= platform::kMaxImageScale; ++scale)
    m_scale->addItem(QStringLiteral("%1×").arg(scale));
  m_scale->setCurrentIndex(qBound(1, options.scale, platform::kMaxImageScale) - 1);
  form->addRow(tr("Resolution Scale"), m_scale);
  m_theme = new QComboBox(this);
  m_theme->setObjectName(QStringLiteral("imageTheme"));
  m_theme->addItems({tr("Current Theme"), tr("Light"), tr("Dark")});
  m_theme->setCurrentIndex(qBound(0, int(options.theme), m_theme->count() - 1));
  form->addRow(tr("Theme"), m_theme);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)->setText(tr("Export…"));
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

platform::ImageExportOptions ImageExportDialog::options() const {
  return {m_width->value(), m_font->value(), m_scale->currentIndex() + 1,
          platform::ImageExportOptions::Theme(m_theme->currentIndex())};
}

}  // namespace typist::ui
