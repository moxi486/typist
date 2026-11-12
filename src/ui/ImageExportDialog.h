#pragma once

#include "ImageWriter.h"

#include <QDialog>

class QComboBox;
class QSpinBox;

namespace typist::ui {

class ImageExportDialog : public QDialog {
  Q_OBJECT
 public:
  explicit ImageExportDialog(const platform::ImageExportOptions& options,
                             QWidget* parent = nullptr);
  platform::ImageExportOptions options() const;

 private:
  QSpinBox *m_width, *m_font;
  QComboBox *m_scale, *m_theme;
};

}  // namespace typist::ui
