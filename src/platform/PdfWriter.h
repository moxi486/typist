#pragma once

#include <QRectF>
#include <QString>

class QTextBlock;
class QTextDocument;

namespace typist::platform {

// Page controls for the PDF writer: paper size, margins and header/footer bands.
struct PdfPageOptions {
  QString paper = QStringLiteral("a4");  // a4 or letter
  double marginMm = 20.0;
  QString header;  // empty leaves no header band
  QString footer;  // a # in the template is replaced by the page number
  bool repeatTableHeader = true;  // continuation pages redraw the first row of a split table
};

// Renders the document to a PDF file. Printing lays out the whole document, so callers
// pass a document that reflects Markdown semantics rather than whatever view mode the
// editor is currently in. Returns false and leaves the target untouched when the output
// cannot be produced.
bool exportPdf(const QString& path, const QTextDocument* document, QString* error);
bool exportPdf(const QString& path, const QTextDocument* document, const PdfPageOptions& options, QString* error);

// Rect of the first row of the table that block belongs to; empty when the block is
// outside a table or already sits in that row. Continuation pages redraw this rect so a
// table split across pages keeps its column names.
QRectF tableHeaderRectFor(QTextDocument* document, const QTextBlock& block);

// False when the build cannot write PDF at all.
bool pdfExportAvailable();

}  // namespace typist::platform
