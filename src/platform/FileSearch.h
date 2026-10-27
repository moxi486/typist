#pragma once

#include <QString>
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

}  // namespace typist::platform
