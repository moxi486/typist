#pragma once

#include <QString>

class QTextDocument;

namespace typist::platform {

// Renders the document to a PDF file through QPrinter. Printing lays out the whole
// document, so callers pass a document that reflects Markdown semantics rather than
// whatever view mode the editor is currently in. Returns false and leaves the target
// untouched when the output cannot be produced.
bool exportPdf(const QString& path, const QTextDocument* document, QString* error);

// False when the build has no Qt PrintSupport module.
bool pdfExportAvailable();

}  // namespace typist::platform
