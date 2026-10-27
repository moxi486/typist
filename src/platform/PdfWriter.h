#pragma once

#include <QString>

class QTextDocument;

namespace typist::platform {

// Page controls for the PDF writer: paper size, margins and header/footer bands.
struct PdfPageOptions {
  QString paper = QStringLiteral("a4");  // a4 or letter
  double marginMm = 20.0;
  QString header;  // empty leaves no header band
  QString footer;  // a # in the template is replaced by the page number
};

// Renders the document to a PDF file. Printing lays out the whole document, so callers
// pass a document that reflects Markdown semantics rather than whatever view mode the
// editor is currently in. Returns false and leaves the target untouched when the output
// cannot be produced.
bool exportPdf(const QString& path, const QTextDocument* document, QString* error);
bool exportPdf(const QString& path, const QTextDocument* document, const PdfPageOptions& options, QString* error);

// False when the build cannot write PDF at all.
bool pdfExportAvailable();

}  // namespace typist::platform
