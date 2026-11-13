#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>
#include <optional>

namespace typist::platform {

enum class LineEnding { Lf, Crlf };

// Mixed files use the first line ending. A file without line breaks has no detected style.
std::optional<LineEnding> detectLineEnding(QStringView text);
QString normalizeLineEndings(QString text, LineEnding ending = LineEnding::Lf);

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
