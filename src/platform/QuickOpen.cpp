#include "QuickOpen.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace typist::platform {
namespace {
QString key(const QString& path) {
  const auto clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
  return clean.toCaseFolded();
#else
  return clean;
#endif
}

int score(const QString& text, const QString& token) {
  if (text == token) return 0;
  if (text.startsWith(token)) return 10;
  const int substring = text.indexOf(token);
  if (substring >= 0) return 30 + std::min(substring, 30);
  int next = 0, first = -1, last = 0;
  for (const auto ch : token) {
    const int found = text.indexOf(ch, next);
    if (found < 0) return -1;
    if (first < 0) first = found;
    last = found;
    next = found + 1;
  }
  return 100 + std::min(first, 50) + std::min(last - first - int(token.size()), 100);
}
}  // namespace

FileIndex indexMarkdownFiles(const QString& root, const std::atomic<bool>& cancelled, int maxFiles,
                             int maxEntries) {
  FileIndex result;
  if (!QFileInfo(root).isDir() || !QFileInfo(root).isReadable()) {
    result.error = QStringLiteral("unavailable");
    return result;
  }
  QStringList pending{QFileInfo(root).absoluteFilePath()};
  int visited = 0;
  while (!pending.isEmpty() && !cancelled.load(std::memory_order_relaxed)) {
    QDirIterator it(pending.takeLast(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                                            QDir::NoSymLinks | QDir::Readable);
    while (it.hasNext()) {
      if (cancelled.load(std::memory_order_relaxed)) return result;
      if (++visited > maxEntries) {
        result.limited = true;
        return result;
      }
      it.next();
      const auto info = it.fileInfo();
      const auto name = info.fileName();
      if (name.startsWith('.') || info.isHidden()) continue;
      if (info.isDir()) {
        if (name != QLatin1String("build") && name != QLatin1String("node_modules"))
          pending.append(info.absoluteFilePath());
      } else if (info.suffix().compare(QLatin1String("md"), Qt::CaseInsensitive) == 0) {
        if (result.paths.size() >= maxFiles) {
          result.limited = true;
          return result;
        }
        result.paths.append(info.absoluteFilePath());
      }
    }
  }
  return result;
}

QStringList quickOpenMatches(const QStringList& paths, const QStringList& recent,
                             const QString& root, const QString& query, int limit) {
  struct Match {
    QString path, relative;
    int score, recent;
  };
  const auto tokens = QDir::fromNativeSeparators(query.trimmed())
                          .toCaseFolded()
                          .split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
  QStringList all = recent;
  all.append(paths);
  QHash<QString, int> recentOrder;
  for (int i = 0; i < recent.size(); ++i)
    if (!recentOrder.contains(key(recent.at(i)))) recentOrder.insert(key(recent.at(i)), i);
  QSet<QString> seen;
  QList<Match> matches;
  for (const auto& path : all) {
    const auto pathKey = key(path);
    if (seen.contains(pathKey)) continue;
    seen.insert(pathKey);
    const QString relative = QDir(root).relativeFilePath(path);
    const auto folded = relative.toCaseFolded();
    const auto name = QFileInfo(path).completeBaseName().toCaseFolded();
    const auto fileName = QFileInfo(path).fileName().toCaseFolded();
    int total = 0;
    for (const auto& token : tokens) {
      int filenameScore = score(name, token);
      const int fullNameScore = score(fileName, token);
      if (fullNameScore >= 0 && (filenameScore < 0 || fullNameScore < filenameScore))
        filenameScore = fullNameScore;
      const int pathScore = score(folded, token);
      if (filenameScore < 0 && pathScore < 0) {
        total = -1;
        break;
      }
      total += filenameScore >= 0 ? filenameScore : 400 + pathScore;
    }
    if (total >= 0)
      matches.append({path, relative, total, recentOrder.value(pathKey, int(recent.size()))});
  }
  std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
    if (a.score != b.score) return a.score < b.score;
    if (a.recent != b.recent) return a.recent < b.recent;
    return a.relative < b.relative;
  });
  QStringList result;
  for (const auto& match : matches) {
    if (result.size() >= limit) break;
    result.append(match.path);
  }
  return result;
}
}  // namespace typist::platform
