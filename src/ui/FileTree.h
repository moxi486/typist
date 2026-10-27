#pragma once

#include <QString>
#include <QTreeWidget>

namespace typist::ui {

// Sidebar tree of directories and Markdown files; double-click opens, context menu edits files.
class FileTree : public QTreeWidget {
  Q_OBJECT

 public:
  explicit FileTree(QWidget* parent = nullptr);

  void setRoot(const QString& rootDir);
  int entryCount() const;
  int directoryCount() const;
  QString entryPath(int index) const;
  void selectPath(const QString& path);
  QString selectedPath() const;

 signals:
  void entryActivated(const QString& path);
  void fileRenamed(const QString& oldPath, const QString& newPath);
  void fileRemoved(const QString& path);

 protected:
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  void buildDirectory(QTreeWidgetItem* parent, const QString& absDir, const QString& relPath);
  QTreeWidgetItem* findLeaf(const QString& path) const;
  void rebuild();
  void newFileIn(const QString& dir);
  void renameSelected();
  void deleteSelected();

  QString m_root;
  int m_directories = 0;
};

}  // namespace typist::ui
