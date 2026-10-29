#pragma once

#include <QString>

namespace typist::platform {

// Minimal OOXML (.docx) writer: headings, paragraphs, lists, code, quotes, breaks, tables.
bool exportDocx(const QString& path, const QString& markdown, QString* error,
                const QString& baseDirectory = QString());
bool docxExportAvailable();

// Bytes written for the markdown, or -1 when the write failed.
qint64 docxSizeForBench(const QString& path, const QString& markdown);

}  // namespace typist::platform
