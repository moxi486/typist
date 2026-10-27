#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace typist::platform {

struct SearchHit {
  QString path;
  int line = 0;
  QString preview;
};

struct SearchRequest {
  QString rootDir;
  QString pattern;
  bool caseSensitive = false;
  int maxHits = 200;
  qint64 maxFileBytes = 4 * 1024 * 1024;
};

// Scans Markdown files under rootDir; hidden entries and VCS/build directories are skipped.
QVector<SearchHit> searchFiles(const SearchRequest& request, QString* error);

// Lists Markdown files under rootDir as absolute paths, sorted by relative path.
QStringList listMarkdownFiles(const QString& rootDir, int maxEntries = 500, qint64 maxFileBytes = 4 * 1024 * 1024);

}  // namespace typist::platform
