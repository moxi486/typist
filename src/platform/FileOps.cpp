#include "FileOps.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace typist::platform {

namespace {

QString withMarkdownSuffix(const QString& name) {
  return name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ? name : name + QStringLiteral(".md");
}

}  // namespace

bool createMarkdownFile(const QString& dir, const QString& name, QString* error) {
  const QString fileName = withMarkdownSuffix(name.trimmed());
  if (fileName == QStringLiteral(".md")) {
    if (error) *error = QStringLiteral("File name is empty");
    return false;
  }
  const QString path = QDir(dir).filePath(fileName);
  if (QFileInfo::exists(path)) {
    if (error) *error = QStringLiteral("A file with that name already exists");
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = QStringLiteral("Cannot create file");
    return false;
  }
  file.close();
  return true;
}

bool renameFile(const QString& path, const QString& newName, QString* error) {
  const QString fileName = withMarkdownSuffix(newName.trimmed());
  const QString target = QDir(QFileInfo(path).absolutePath()).filePath(fileName);
  if (target == path) return true;
  if (QFileInfo::exists(target)) {
    if (error) *error = QStringLiteral("A file with that name already exists");
    return false;
  }
  if (!QFile::rename(path, target)) {
    if (error) *error = QStringLiteral("Rename failed");
    return false;
  }
  return true;
}

bool deleteFile(const QString& path, QString* error) {
  if (!QFile::remove(path)) {
    if (error) *error = QStringLiteral("Delete failed");
    return false;
  }
  return true;
}

QString firstMarkdownPath(const QList<QUrl>& urls) {
  for (const QUrl& url : urls) {
    const QString path = url.toLocalFile();
    if (path.isEmpty() || !path.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) continue;
    return path;
  }
  return QString();
}

QString firstImagePath(const QList<QUrl>& urls) {
  static const QStringList suffixes = {QStringLiteral(".png"), QStringLiteral(".jpg"),
                                       QStringLiteral(".jpeg"), QStringLiteral(".gif"),
                                       QStringLiteral(".webp"), QStringLiteral(".svg"),
                                       QStringLiteral(".bmp")};
  for (const QUrl& url : urls) {
    const QString path = url.toLocalFile();
    if (path.isEmpty()) continue;
    for (const QString& suffix : suffixes) {
      if (path.endsWith(suffix, Qt::CaseInsensitive)) return path;
    }
  }
  return QString();
}

}  // namespace typist::platform
