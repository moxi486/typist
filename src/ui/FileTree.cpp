#include "FileTree.h"

#include "FileOps.h"

#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QContextMenuEvent>
#include <QTreeWidgetItem>

namespace typist::ui {

namespace {

constexpr int kPathRole = Qt::UserRole;
constexpr int kDirRole = Qt::UserRole + 1;

bool skippedDirectory(const QString& name) {
  return name.startsWith(QChar(46)) || name == QStringLiteral("build") || name == QStringLiteral("node_modules");
}

}  // namespace

FileTree::FileTree(QWidget* parent) : QTreeWidget(parent) {
  setHeaderHidden(true);
  setRootIsDecorated(true);
  setUniformRowHeights(true);
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    if (item == nullptr || item->data(0, kDirRole).toBool()) return;
    emit entryActivated(item->data(0, kPathRole).toString());
  });
}

void FileTree::setRoot(const QString& rootDir) {
  m_root = rootDir;
  m_directories = 0;
  clear();
  buildDirectory(nullptr, rootDir, QString());
  expandAll();
}

void FileTree::buildDirectory(QTreeWidgetItem* parent, const QString& absDir, const QString& relPath) {
  QDir dir(absDir);
  const QStringList files = dir.entryList(QStringList() << QStringLiteral("*.md"), QDir::Files, QDir::Name);
  for (const QString& file : files) {
    QTreeWidgetItem* item = parent == nullptr ? new QTreeWidgetItem(this) : new QTreeWidgetItem(parent);
    const QString path = dir.filePath(file);
    item->setText(0, file);
    item->setData(0, kPathRole, path);
    item->setData(0, kDirRole, false);
  }

  const QStringList subdirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString& sub : subdirs) {
    if (skippedDirectory(sub)) continue;
    QTreeWidgetItem* item = parent == nullptr ? new QTreeWidgetItem(this) : new QTreeWidgetItem(parent);
    item->setText(0, sub);
    item->setData(0, kDirRole, true);
    ++m_directories;
    buildDirectory(item, dir.filePath(sub), relPath + sub + QLatin1Char('/'));
  }
}

int FileTree::entryCount() const {
  int count = 0;
  for (int i = 0; i < topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = topLevelItem(i);
    if (item->data(0, kDirRole).toBool()) count += item->childCount();
    else ++count;
  }
  return count;
}

int FileTree::directoryCount() const { return m_directories; }

QString FileTree::entryPath(int index) const {
  int seen = 0;
  for (int i = 0; i < topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = topLevelItem(i);
    if (!item->data(0, kDirRole).toBool()) {
      if (seen == index) return item->data(0, kPathRole).toString();
      ++seen;
      continue;
    }
    for (int c = 0; c < item->childCount(); ++c) {
      QTreeWidgetItem* child = item->child(c);
      if (child->data(0, kDirRole).toBool()) continue;
      if (seen == index) return child->data(0, kPathRole).toString();
      ++seen;
    }
  }
  return QString();
}

QTreeWidgetItem* FileTree::findLeaf(const QString& path) const {
  for (int i = 0; i < topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = topLevelItem(i);
    if (!item->data(0, kDirRole).toBool() && item->data(0, kPathRole).toString() == path) return item;
    for (int c = 0; c < item->childCount(); ++c) {
      QTreeWidgetItem* child = item->child(c);
      if (!child->data(0, kDirRole).toBool() && child->data(0, kPathRole).toString() == path) return child;
    }
  }
  return nullptr;
}

void FileTree::selectPath(const QString& path) {
  QTreeWidgetItem* item = findLeaf(path);
  if (item != nullptr) setCurrentItem(item);
}

QString FileTree::selectedPath() const {
  QTreeWidgetItem* item = currentItem();
  if (item == nullptr || item->data(0, kDirRole).toBool()) return QString();
  return item->data(0, kPathRole).toString();
}

void FileTree::rebuild() {
  const QString current = selectedPath();
  setRoot(m_root);
  if (!current.isEmpty()) selectPath(current);
}

void FileTree::contextMenuEvent(QContextMenuEvent* event) {
  QTreeWidgetItem* item = currentItem();
  QMenu menu(this);
  const bool onDirectory = item == nullptr || item->data(0, kDirRole).toBool();
  const QString dir = onDirectory ? (item == nullptr ? m_root : item->data(0, kPathRole).toString())
                                  : QFileInfo(item->data(0, kPathRole).toString()).absolutePath();
  menu.addAction(QStringLiteral("New Markdown File"), this, [this, dir] { newFileIn(dir); });
  if (!onDirectory) {
    menu.addAction(QStringLiteral("Rename"), this, &FileTree::renameSelected);
    menu.addAction(QStringLiteral("Delete"), this, &FileTree::deleteSelected);
  }
  menu.exec(event->globalPos());
}

void FileTree::newFileIn(const QString& dir) {
  bool ok = false;
  const QString name = QInputDialog::getText(this, QStringLiteral("New Markdown File"), QStringLiteral("File name"),
                                             QLineEdit::Normal, QStringLiteral("untitled.md"), &ok);
  if (!ok || name.isEmpty()) return;

  QString error;
  if (!platform::createMarkdownFile(dir, name, &error)) {
    QMessageBox::warning(this, QStringLiteral("New Markdown File"), error);
    return;
  }
  rebuild();
  emit entryActivated(QDir(dir).filePath(name.endsWith(QStringLiteral(".md")) ? name : name + QStringLiteral(".md")));
}

void FileTree::renameSelected() {
  const QString path = selectedPath();
  if (path.isEmpty()) return;
  bool ok = false;
  const QString name = QInputDialog::getText(this, QStringLiteral("Rename File"), QStringLiteral("New name"),
                                             QLineEdit::Normal, QFileInfo(path).fileName(), &ok);
  if (!ok || name.isEmpty()) return;

  QString error;
  if (!platform::renameFile(path, name, &error)) {
    QMessageBox::warning(this, QStringLiteral("Rename File"), error);
    return;
  }
  const QString newPath = QDir(QFileInfo(path).absolutePath()).filePath(name.endsWith(QStringLiteral(".md")) ? name : name + QStringLiteral(".md"));
  rebuild();
  selectPath(newPath);
  emit fileRenamed(path, newPath);
}

void FileTree::deleteSelected() {
  const QString path = selectedPath();
  if (path.isEmpty()) return;
  if (QMessageBox::question(this, QStringLiteral("Delete File"), QStringLiteral("Delete %1?").arg(QFileInfo(path).fileName()))
      != QMessageBox::Yes) {
    return;
  }

  QString error;
  if (!platform::deleteFile(path, &error)) {
    QMessageBox::warning(this, QStringLiteral("Delete File"), error);
    return;
  }
  rebuild();
  emit fileRemoved(path);
}

}  // namespace typist::ui
