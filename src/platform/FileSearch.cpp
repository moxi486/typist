#include "FileSearch.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>

namespace typist::platform {

namespace {

bool skippedDirectory(const QString& name) {
  return name.startsWith(QChar(46)) || name == QStringLiteral("build") || name == QStringLiteral("node_modules");
}

QString previewOf(const QString& text) {
  const int limit = 120;
  if (text.size() <= limit) return text;
  return text.left(limit) + QStringLiteral("...");
}

}  // namespace

QVector<SearchHit> scanFiles(const SearchRequest& request, QString* error, const std::atomic<bool>* cancelled,
                               bool* wasCancelled) {
  QVector<SearchHit> hits;
  if (request.pattern.isEmpty()) {
    if (error) *error = QStringLiteral("Search pattern is empty");
    return hits;
  }

  const QDir root(request.rootDir);
  QDirIterator it(request.rootDir, QStringList() << QStringLiteral("*.md"),
                  QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    if (cancelled && cancelled->load(std::memory_order_relaxed)) {
      if (wasCancelled) *wasCancelled = true;
      return hits;
    }
    const QString path = it.next();
    const QStringList parts = root.relativeFilePath(path).split(QChar(47));
    // Every directory component before the file name must be a visible, non-VCS directory.
    bool skip = false;
    for (int i = 0; i < parts.size() - 1; ++i) {
      if (skippedDirectory(parts.at(i))) {
        skip = true;
        break;
      }
    }
    if (skip) continue;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) continue;
    if (file.size() > request.maxFileBytes) {
      file.close();
      continue;
    }
    const QString text = QString::fromUtf8(file.readAll());
    file.close();

    int line = 0;
    int offset = 0;
    while (offset < text.size()) {
      const int newline = text.indexOf(QChar(10), offset);
      QString content = newline == -1 ? text.mid(offset) : text.mid(offset, newline - offset);
      if (content.endsWith(QChar(13))) content.chop(1);
      ++line;
      if (content.contains(request.pattern, request.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive)) {
        hits.push_back(SearchHit{path, line, previewOf(content)});
        if (request.maxHits > 0 && hits.size() >= request.maxHits) return hits;
      }
      if (newline == -1) break;
      offset = newline + 1;
    }
  }
  return hits;
}


QStringList listMarkdownFiles(const QString& rootDir, int maxEntries, qint64 maxFileBytes) {
  const QDir root(rootDir);
  QStringList entries;
  QDirIterator it(rootDir, QStringList() << QStringLiteral("*.md"),
                  QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    const QStringList parts = root.relativeFilePath(path).split(QChar(47));
    bool skip = false;
    for (int i = 0; i < parts.size() - 1; ++i) {
      if (skippedDirectory(parts.at(i))) {
        skip = true;
        break;
      }
    }
    if (skip) continue;

    const QFileInfo info(path);
    if (info.size() > maxFileBytes) continue;
    entries.append(path);
    if (maxEntries > 0 && entries.size() >= maxEntries) break;
  }
  std::sort(entries.begin(), entries.end(), [&root](const QString& left, const QString& right) {
    return root.relativeFilePath(left) < root.relativeFilePath(right);
  });
  return entries;
}

QVector<SearchHit> searchFiles(const SearchRequest& request, QString* error) {
  return scanFiles(request, error, nullptr, nullptr);
}

QVector<SearchHit> searchFilesCancellable(const SearchRequest& request, QString* error,
                                          const std::atomic<bool>& cancelled, bool* wasCancelled) {
  return scanFiles(request, error, &cancelled, wasCancelled);
}

}  // namespace typist::platform
