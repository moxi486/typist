#include "PdfWriter.h"

#include <QByteArray>
#include <QFile>
#include <QPrinter>
#include <QTextDocument>

namespace typist::platform {

#ifdef TYPIST_HAS_PRINTSUPPORT

bool pdfExportAvailable() { return true; }

bool exportPdf(const QString& path, const QTextDocument* document, QString* error) {
  if (document == nullptr) {
    if (error != nullptr) *error = QStringLiteral("No document to print");
    return false;
  }

  QPrinter printer(QPrinter::HighResolution);
  printer.setOutputFormat(QPrinter::PdfFormat);
  printer.setOutputFileName(path);
  document->print(&printer);

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) *error = QStringLiteral("PDF was not written");
    return false;
  }
  const QByteArray head = file.read(5);
  file.close();
  if (head != QByteArrayLiteral("%PDF-")) {
    if (error != nullptr) *error = QStringLiteral("PDF output is not a PDF file");
    QFile::remove(path);
    return false;
  }
  return true;
}

#else

bool pdfExportAvailable() { return false; }

bool exportPdf(const QString&, const QTextDocument*, QString* error) {
  if (error != nullptr) *error = QStringLiteral("PDF export needs Qt PrintSupport, which this build lacks");
  return false;
}

#endif

}  // namespace typist::platform
