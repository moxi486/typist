#pragma once

#include "FolderLocations.h"

#include <QMenu>

namespace typist::ui {
class FolderLocationsMenu : public QMenu {
  Q_OBJECT
 public:
  explicit FolderLocationsMenu(QWidget* parent = nullptr, const QString& settingsFile = QString());
  void setRoot(const QString& path);
  void recordFolder(const QString& path);
  platform::FolderLocations locations() const { return m_store.read(); }
  void refresh();

 signals:
  void folderActivated(const QString& path);
  void browseRequested();
  void pinLimitReached();

 protected:
  void changeEvent(QEvent* event) override;

 private:
  platform::FolderLocationsStore m_store;
  QString m_root;
};
}  // namespace typist::ui
