#pragma once

#include <QStringList>
#include <atomic>

namespace typist::platform {

struct FileIndex {
  QStringList paths;
  bool limited = false;
  QString error;
};

// Prunes hidden/build directories and symlinks before descent. No file contents are read.
FileIndex indexMarkdownFiles(const QString& root, const std::atomic<bool>& cancelled,
                             int maxFiles = 10000, int maxEntries = 100000);

// Empty queries retain recent order; searches rank filename matches before path matches.
QStringList quickOpenMatches(const QStringList& paths, const QStringList& recent,
                             const QString& root, const QString& query, int limit = 100);

}  // namespace typist::platform
