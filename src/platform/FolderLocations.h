#pragma once

#include <QStringList>

namespace typist::platform {
struct FolderLocations {
  QStringList recent;
  QStringList pinned;
};

// Uses native app settings by default; an INI path allows isolated stores in tests.
class FolderLocationsStore {
 public:
  explicit FolderLocationsStore(const QString& settingsFile = QString());
  FolderLocations read() const;
  void record(const QString& path);
  bool setPinned(const QString& path, bool pinned);
  void forgetRecent(const QString& path);
  void clearRecent();

 private:
  bool write(const FolderLocations& locations);
  QString m_settingsFile;
};
}  // namespace typist::platform
