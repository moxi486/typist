#include "FileWriter.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace typist::platform {

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

}  // namespace typist::platform
