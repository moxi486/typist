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
  return name.startsWith(QChar(46)) || name == QStringLiteral("build") ||
         name == QStringLiteral("node_modules");
}

QString previewOf(const QString& text, int column, int length) {
  const int start = std::max(0, column - 35);
  const int count = std::min(512, std::max(120, length + 35));
  return (start ? QStringLiteral("…") : QString()) + text.mid(start, count) +
         (start + count < text.size() ? QStringLiteral("…") : QString());
}
}  // namespace

QVector<SearchHit> scanFiles(const SearchRequest& request, QString* error,
                             const std::atomic<bool>* cancelled, bool* wasCancelled,
                             SearchStats* outputStats) {
  QVector<SearchHit> hits;
  SearchStats localStats;
  auto& stats = outputStats ? *outputStats : localStats;
  stats = {};
  if (error) error->clear();
  if (wasCancelled) *wasCancelled = false;
  const auto stopped = [&] {
    const bool stop = cancelled && cancelled->load(std::memory_order_relaxed);
    if (stop && wasCancelled) *wasCancelled = true;
    return stop;
  };
  if (stopped()) return hits;
  if (request.pattern.isEmpty() || request.pattern.size() > 512 || request.pattern.contains('\n')) {
    if (error) *error = QStringLiteral("invalid_pattern");
    return hits;
  }
  if (!QFileInfo(request.rootDir).isDir() || !QFileInfo(request.rootDir).isReadable()) {
    if (error) *error = QStringLiteral("folder_unavailable");
    return hits;
  }
  const qint64 fileLimit = qBound(qint64(1), request.maxFileBytes, qint64(4 * 1024 * 1024));
  QStringList pending{QFileInfo(request.rootDir).absoluteFilePath()};
  int entries = 0;
  qint64 bytes = 0;
  while (!pending.isEmpty()) {
    if (stopped()) return hits;
    QDirIterator it(pending.takeLast(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                                            QDir::NoSymLinks | QDir::Readable);
    while (it.hasNext()) {
      if (stopped()) return hits;
      if (++entries > request.maxEntries) {
        stats.limited = true;
        return hits;
      }
      it.next();
      const auto info = it.fileInfo();
      if (info.isHidden() || info.fileName().startsWith('.')) continue;
      if (info.isDir()) {
        if (!skippedDirectory(info.fileName())) pending.append(info.absoluteFilePath());
        continue;
      }
      if (info.suffix().compare(QLatin1String("md"), Qt::CaseInsensitive) != 0) continue;
      if (stats.scannedFiles + stats.skippedFiles >= request.maxFiles) {
        stats.limited = true;
        return hits;
      }
      QFile file(info.absoluteFilePath());
      if (info.size() > fileLimit || !file.open(QIODevice::ReadOnly)) {
        ++stats.skippedFiles;
        continue;
      }
      if (bytes + info.size() > request.maxTotalBytes) {
        stats.limited = true;
        return hits;
      }
      const auto data = file.read(fileLimit + 1);
      bytes += data.size();
      if (bytes > request.maxTotalBytes) {
        stats.limited = true;
        return hits;
      }
      if (data.size() > fileLimit || file.error() != QFileDevice::NoError || data.contains('\0')) {
        ++stats.skippedFiles;
        continue;
      }
      ++stats.scannedFiles;
      const QString text = QString::fromUtf8(data);
      int line = 0, offset = 0;
      while (offset < text.size()) {
        if (stopped()) return hits;
        const int newline = text.indexOf('\n', offset);
        QString content = newline < 0 ? text.mid(offset) : text.mid(offset, newline - offset);
        if (content.endsWith('\r')) content.chop(1);
        ++line;
        const int column = content.indexOf(
            request.pattern, 0, request.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive);
        if (column >= 0) {
          hits.append({info.absoluteFilePath(), line,
                       previewOf(content, column, int(request.pattern.size())), column, content,
                       int(request.pattern.size())});
          if (request.maxHits > 0 && hits.size() >= request.maxHits) {
            stats.limited = true;
            return hits;
          }
        }
        if (newline < 0) break;
        offset = newline + 1;
      }
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

QVector<SearchHit> searchFiles(const SearchRequest& request, QString* error, SearchStats* stats) {
  return scanFiles(request, error, nullptr, nullptr, stats);
}

QVector<SearchHit> searchFilesCancellable(const SearchRequest& request, QString* error,
                                          const std::atomic<bool>& cancelled, bool* wasCancelled,
                                          SearchStats* stats) {
  return scanFiles(request, error, &cancelled, wasCancelled, stats);
}

}  // namespace typist::platform
