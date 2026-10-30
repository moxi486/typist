#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>

namespace typist::platform {

struct SearchHit {
  QString path;
  int line = 0;
  QString preview;
  int column = 0;  // UTF-16 offset in the original source line.
  QString sourceLine;
  int length = 0;
};

struct SearchRequest {
  QString rootDir;
  QString pattern;
  bool caseSensitive = false;
  int maxHits = 200;
  qint64 maxFileBytes = 4 * 1024 * 1024;
  int maxFiles = 10000;
  int maxEntries = 100000;
  qint64 maxTotalBytes = 64 * 1024 * 1024;
  bool regularExpression = false;
  bool wholeWords = false;
};

struct SearchStats {
  int scannedFiles = 0;
  int skippedFiles = 0;
  bool limited = false;
};

// Scans Markdown files under rootDir; hidden entries and VCS/build directories are skipped.
QVector<SearchHit> searchFiles(const SearchRequest& request, QString* error,
                               SearchStats* stats = nullptr);

// Same scan with a cancellation flag checked between files; partial hits are returned and
// wasCancelled reports whether the scan stopped early.
QVector<SearchHit> searchFilesCancellable(const SearchRequest& request, QString* error,
                                          const std::atomic<bool>& cancelled, bool* wasCancelled,
                                          SearchStats* stats = nullptr);

// Lists Markdown files under rootDir as absolute paths, sorted by relative path.
QStringList listMarkdownFiles(const QString& rootDir, int maxEntries = 500,
                              qint64 maxFileBytes = 4 * 1024 * 1024);

}  // namespace typist::platform
