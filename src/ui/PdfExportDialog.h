#pragma once

#include "PdfWriter.h"

#include <QDialog>
#include <QImage>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;
class QTextDocument;
namespace typist::ui {
class PdfExportDialog : public QDialog {
  Q_OBJECT
 public:
  PdfExportDialog(const QTextDocument* document, const platform::PdfPageOptions& options,
                  QWidget* parent = nullptr);
  platform::PdfPageOptions options() const;

 protected:
  void changeEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void retranslate();
  void refreshPreview();
  void showImage();
  const QTextDocument* m_document;
  QString m_title;
  QComboBox *m_paper, *m_orientation;
  QDoubleSpinBox* m_margin;
  QLineEdit *m_header, *m_footer;
  QCheckBox* m_repeat;
  QLabel *m_paperLabel, *m_orientationLabel, *m_marginLabel, *m_headerLabel, *m_footerLabel;
  QLabel *m_hint, *m_pageLabel, *m_error;
  QLabel* m_preview = nullptr;
  QPushButton *m_previous, *m_next, *m_export, *m_cancel;
  QTimer* m_refresh;
  QImage m_image;
  int m_page = 0, m_pages = 0;
};
}  // namespace typist::ui
