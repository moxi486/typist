#include "FileTree.h"

#include "FileSearch.h"

#include <QDir>
#include <QHeaderView>
#include <QTreeWidgetItem>

namespace typist::ui {

FileTree::FileTree(QWidget* parent) : QTreeWidget(parent) {
  setHeaderHidden(true);
  setRootIsDecorated(false);
  setUniformRowHeights(true);
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    if (item == nullptr) return;
    emit entryActivated(item->data(0, Qt::UserRole).toString());
  });
}

void FileTree::setRoot(const QString& rootDir) {
  m_root = rootDir;
  clear();
  const QDir root(rootDir);
  const QStringList entries = platform::listMarkdownFiles(rootDir);
  for (const QString& path : entries) {
    QTreeWidgetItem* item = new QTreeWidgetItem(this);
    item->setText(0, root.relativeFilePath(path));
    item->setData(0, Qt::UserRole, path);
    addTopLevelItem(item);
  }
}

int FileTree::entryCount() const { return topLevelItemCount(); }

QString FileTree::entryPath(int index) const {
  QTreeWidgetItem* item = topLevelItem(index);
  return item == nullptr ? QString() : item->data(0, Qt::UserRole).toString();
}

void FileTree::selectPath(const QString& path) {
  const QDir root(m_root);
  const QString relative = root.relativeFilePath(path);
  for (int i = 0; i < topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = topLevelItem(i);
    if (item->text(0) == relative) {
      setCurrentItem(item);
      return;
    }
  }
}

QString FileTree::selectedPath() const {
  QTreeWidgetItem* item = currentItem();
  return item == nullptr ? QString() : item->data(0, Qt::UserRole).toString();
}

}  // namespace typist::ui
