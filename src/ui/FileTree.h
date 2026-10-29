#pragma once

#include "Settings.h"

#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTreeWidget>

class QFileSystemWatcher;
class QTimer;
class QMenu;

namespace typist::ui {

// Sidebar tree of directories and Markdown files; double-click opens, context menu edits files.
class FileTree : public QTreeWidget {
  Q_OBJECT

 public:
  explicit FileTree(QWidget* parent = nullptr);

  void setRoot(const QString& rootDir);
  QString rootPath() const { return m_root; }
  int entryCount() const;
  int directoryCount() const;
  QString entryPath(int index) const;
  void selectPath(const QString& path);
  QString selectedPath() const;
  const platform::FileBrowserOptions& options() const { return m_options; }
  void setOptions(const platform::FileBrowserOptions& options);
  QMenu* createOptionsMenu(QWidget* parent);

  // Rebuilds only when the directory listing differs from what the tree shows.
  bool refreshIfChanged();
  void refresh();

  // Watches directories under the root; changes are debounced into a refresh.
  void setWatchEnabled(bool enabled);

 signals:
  void entryActivated(const QString& path);

  // Emitted when the watcher sees a change under the root; the refresh itself is debounced.
  void directoryChanged();
  void fileRenamed(const QString& oldPath, const QString& newPath);
  void fileRemoved(const QString& path);
  void optionsChanged();

 protected:
  void contextMenuEvent(QContextMenuEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  QTreeWidgetItem* findLeaf(const QString& path) const;
  void rebuild();
  QList<QFileInfo> scan() const;
  QString signature(const QList<QFileInfo>& entries) const;
  void renderEntries();
  void rememberExpansion();
  void updateOptionsMenu();
  void collectDirectories(QStringList& dirs) const;
  void watchCurrentDirectories();
  void scheduleRefresh();
  void stopDebounce();
  void newFileIn(const QString& dir);
  void renameSelected();
  void deleteSelected();

  QString m_root;
  QString m_signature;
  bool m_watchEnabled = false;
  bool m_hasExpansionState = false;
  QSet<QString> m_expanded;
  QList<QFileInfo> m_entries;
  QHash<QString, QTreeWidgetItem*> m_items;
  platform::FileBrowserOptions m_options;
  QMenu* m_optionsMenu = nullptr;
  QFileSystemWatcher* m_watcher = nullptr;
  QTimer* m_debounce = nullptr;
};

}  // namespace typist::ui
