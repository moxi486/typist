#include "FolderLocations.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <algorithm>
#include <memory>

namespace typist::platform {
namespace {
std::unique_ptr<QSettings> settings(const QString& file) {
  return file.isEmpty()
             ? std::make_unique<QSettings>(QStringLiteral("Typist"), QStringLiteral("Typist"))
             : std::make_unique<QSettings>(file, QSettings::IniFormat);
}
QString folder(const QString& path) {
  if (path.isEmpty()) return {};
  const QFileInfo info(path);
  const auto canonical = info.isDir() ? info.canonicalFilePath() : QString();
  return canonical.isEmpty() ? QString() : QDir::cleanPath(canonical);
}
QString key(const QString& path) {
#ifdef Q_OS_WIN
  return path.toCaseFolded();
#else
  return path;
#endif
}
QStringList clean(const QStringList& paths, int limit) {
  QStringList result;
  QSet<QString> seen;
  for (const auto& path : paths) {
    const auto normalized = folder(path);
    if (normalized.isEmpty() || seen.contains(key(normalized))) continue;
    seen.insert(key(normalized));
    result.append(normalized);
    if (result.size() == limit) break;
  }
  return result;
}
void remove(QStringList& paths, const QString& path) {
  paths.erase(std::remove_if(paths.begin(), paths.end(),
                             [&](const QString& item) { return key(item) == key(path); }),
              paths.end());
}
}  // namespace

FolderLocationsStore::FolderLocationsStore(const QString& settingsFile)
    : m_settingsFile(settingsFile) {}
FolderLocations FolderLocationsStore::read() const {
  const auto store = settings(m_settingsFile);
  return {clean(store->value(QStringLiteral("folders/recent")).toStringList(), 12),
          clean(store->value(QStringLiteral("folders/pinned")).toStringList(), 16)};
}
bool FolderLocationsStore::write(const FolderLocations& locations) {
  auto store = settings(m_settingsFile);
  store->setValue(QStringLiteral("folders/recent"), locations.recent);
  store->setValue(QStringLiteral("folders/pinned"), locations.pinned);
  store->sync();
  return store->status() == QSettings::NoError;
}
void FolderLocationsStore::record(const QString& path) {
  const auto normalized = folder(path);
  if (normalized.isEmpty()) return;
  auto locations = read();
  remove(locations.recent, normalized);
  locations.recent.prepend(normalized);
  while (locations.recent.size() > 12) locations.recent.removeLast();
  write(locations);
}
bool FolderLocationsStore::setPinned(const QString& path, bool pinned) {
  const auto normalized = folder(path);
  if (normalized.isEmpty()) return false;
  auto locations = read();
  const auto existing = locations.pinned;
  remove(locations.pinned, normalized);
  if (pinned) {
    if (locations.pinned.size() >= 16) return false;
    if (existing.size() != locations.pinned.size()) return true;
    locations.pinned.append(normalized);
  }
  return write(locations);
}
void FolderLocationsStore::forgetRecent(const QString& path) {
  auto locations = read();
  remove(locations.recent, folder(path));
  write(locations);
}
void FolderLocationsStore::clearRecent() {
  auto locations = read();
  locations.recent.clear();
  write(locations);
}
}  // namespace typist::platform
