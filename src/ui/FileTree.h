#pragma once

#include <QString>
#include <QTreeWidget>

class QFileSystemWatcher;
class QTimer;

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

  // Rebuilds only when the directory listing differs from what the tree shows.
  bool refreshIfChanged();

  // Watches directories under the root; changes are debounced into a refresh.
  void setWatchEnabled(bool enabled);

 signals:
  void entryActivated(const QString& path);

  // Emitted when the watcher sees a change under the root; the refresh itself is debounced.
  void directoryChanged();
  void fileRenamed(const QString& oldPath, const QString& newPath);
  void fileRemoved(const QString& path);

 protected:
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void buildDirectory(QTreeWidgetItem* parent, const QString& absDir, const QString& relPath);
  QTreeWidgetItem* findLeaf(const QString& path) const;
  void rebuild();
  QString signature() const;
  void collectDirectories(QStringList& dirs) const;
  void collectUnder(const QTreeWidgetItem* item, QStringList& dirs) const;
  void watchCurrentDirectories();
  void scheduleRefresh();
  void stopDebounce();
  void newFileIn(const QString& dir);
  void renameSelected();
  void deleteSelected();

  QString m_root;
  QString m_signature;
  int m_directories = 0;
  bool m_watchEnabled = false;
  bool m_hasExpansionState = false;
  QFileSystemWatcher* m_watcher = nullptr;
  QTimer* m_debounce = nullptr;
};

}  // namespace typist::ui
