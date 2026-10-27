#pragma once

#include <QString>
#include <QTreeWidget>

namespace typist::ui {

// Sidebar listing of Markdown files under a root directory; double-click opens an entry.
class FileTree : public QTreeWidget {
  Q_OBJECT

 public:
  explicit FileTree(QWidget* parent = nullptr);

  void setRoot(const QString& rootDir);
  int entryCount() const;
  QString entryPath(int index) const;
  void selectPath(const QString& path);
  QString selectedPath() const;

 signals:
  void entryActivated(const QString& path);

 private:
  QString m_root;
};

}  // namespace typist::ui
