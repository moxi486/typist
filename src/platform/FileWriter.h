#pragma once

#include <QByteArray>
#include <QString>

namespace typist::platform {

// Writes contents to path by writing a sibling temporary file first and then
// renaming it over the target. The temporary file is removed on any failure.
bool writeAtomic(const QString& path, const QString& contents, QString* error = nullptr);

struct ExportedFile {
  QString path;
  QString canonicalPath;
  QByteArray digest;

  static ExportedFile capture(const QString& path);
  bool unchanged() const;
};

enum class ExportReplacementResult { Success, Changed, WriteFailed };
ExportReplacementResult replaceExport(const ExportedFile& destination, const QString& stagedPath);

}  // namespace typist::platform
