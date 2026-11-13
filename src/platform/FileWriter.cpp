#include "FileWriter.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace typist::platform {

namespace {

QByteArray fileDigest(const QString& path) {
  QFile file(path);
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) return {};
  return hash.result();
}

}  // namespace

std::optional<LineEnding> detectLineEnding(QStringView text) {
  const auto lf = text.indexOf(u'\n');
  const auto cr = text.indexOf(u'\r');
  if (lf < 0 && cr < 0) return std::nullopt;
  return cr >= 0 && lf == cr + 1 ? LineEnding::Crlf : LineEnding::Lf;
}

QString normalizeLineEndings(QString text, LineEnding ending) {
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace(u'\r', u'\n');
  if (ending == LineEnding::Crlf) text.replace(u'\n', QStringLiteral("\r\n"));
  return text;
}

bool writeAtomic(const QString& path, const QString& contents, QString* error) {
  const QFileInfo info(path);
  const QDir dir = info.dir();
  if (!dir.exists()) {
    if (error != nullptr) *error = QStringLiteral("directory does not exist");
    return false;
  }

  const QString tempPath = dir.filePath(QStringLiteral(".typist-tmp-%1").arg(info.fileName()));
  QFile tempFile(tempPath);
  if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error != nullptr) *error = QStringLiteral("cannot write temporary file");
    return false;
  }

  const QByteArray bytes = contents.toUtf8();
  if (tempFile.write(bytes) != bytes.size()) {
    tempFile.close();
    tempFile.remove();
    if (error != nullptr) *error = QStringLiteral("short write");
    return false;
  }
  tempFile.flush();
  tempFile.close();

  QFile target(path);
  if (target.exists() && !target.remove()) {
    QFile::remove(tempPath);
    if (error != nullptr) *error = QStringLiteral("cannot replace existing file");
    return false;
  }

  if (!QFile::rename(tempPath, path)) {
    QFile::remove(tempPath);
    if (error != nullptr) *error = QStringLiteral("rename failed");
    return false;
  }
  return true;
}

ExportedFile ExportedFile::capture(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile() || info.isSymLink()) return {};
  return {info.absoluteFilePath(), info.canonicalFilePath(), fileDigest(info.absoluteFilePath())};
}

bool ExportedFile::unchanged() const {
  if (canonicalPath.isEmpty() || digest.isEmpty()) return false;
  const QFileInfo info(path);
  return info.isFile() && !info.isSymLink() && info.canonicalFilePath() == canonicalPath &&
         fileDigest(path) == digest;
}

ExportReplacementResult replaceExport(const ExportedFile& destination, const QString& stagedPath) {
  QFile staged(stagedPath);
  QSaveFile output(destination.path);
  if (!staged.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
    return ExportReplacementResult::WriteFailed;

  QByteArray buffer(64 * 1024, Qt::Uninitialized);
  for (;;) {
    const auto count = staged.read(buffer.data(), buffer.size());
    if (count < 0 || (count > 0 && output.write(buffer.constData(), count) != count))
      return ExportReplacementResult::WriteFailed;
    if (count == 0) break;
  }

  // Rendering and copying can take time. Recheck the previous output before replacing it.
  if (!destination.unchanged()) return ExportReplacementResult::Changed;
  return output.commit() ? ExportReplacementResult::Success : ExportReplacementResult::WriteFailed;
}

}  // namespace typist::platform
