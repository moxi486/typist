#pragma once

#include <QImage>
#include <QPair>
#include <QRectF>
#include <QString>
#include <QVector>

class QTextBlock;
class QTextDocument;

namespace typist::platform {

// Page controls for the PDF writer: paper size, margins and header/footer bands.
struct PdfPageOptions {
  QString paper = QStringLiteral("a4");  // a4 or letter
  double marginMm = 20.0;
  QString header;                 // empty leaves no header band
  QString footer;                 // a # in the template is replaced by the page number
  bool repeatTableHeader = true;  // continuation pages redraw the first row of a split table
  bool landscape = false;
  QString title;  // substitutes {title} in header/footer templates; not persisted
};

// Renders the document to a PDF file. Printing lays out the whole document, so callers
// pass a document that reflects Markdown semantics rather than whatever view mode the
// editor is currently in. Returns false and leaves the target untouched when the output
// cannot be produced.
bool exportPdf(const QString& path, const QTextDocument* document, QString* error);
bool exportPdf(const QString& path, const QTextDocument* document, const PdfPageOptions& options,
               QString* error);

// One 96 DPI page, rendered with the same layout and painting path as export.
// page is zero-based and clamped; pageCount receives the full count without rasterizing all pages.
QImage pdfPagePreview(const QTextDocument* document, const PdfPageOptions& options, int page,
                      int* pageCount, QString* error);

// Rect of the first row of the table that block belongs to; empty when the block is
// outside a table or already sits in that row. Continuation pages redraw this rect so a
// table split across pages keeps its column names.
QRectF tableHeaderRectFor(QTextDocument* document, const QTextBlock& block);

// Line ranges for a paragraph taller than a page: chunks are contiguous and cover every
// line, a chunk holds at most linesPerPage lines, and orphan control keeps a lone trailing
// line from landing alone on its own page (a single-line paragraph stays one chunk).
QVector<QPair<int, int>> splitParagraphChunks(int lineCount, int linesPerPage);

// False when the build cannot write PDF at all.
bool pdfExportAvailable();

}  // namespace typist::platform
