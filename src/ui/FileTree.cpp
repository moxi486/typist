#include "FileTree.h"

#include "FileOps.h"
#include <QFileSystemWatcher>
#include <QTimer>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
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
  m_signature = signature();
  if (m_watchEnabled) watchCurrentDirectories();
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
    const QString subPath = dir.filePath(sub);
    item->setText(0, sub);
    item->setData(0, kDirRole, true);
    item->setData(0, kPathRole, subPath);
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


QString FileTree::signature() const {
  QDir root(m_root);
  QStringList parts;
  QDirIterator it(m_root, QStringList() << QStringLiteral("*.md"), QDir::Files, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString path = it.next();
    const QStringList segments = root.relativeFilePath(path).split(QChar(47));
    bool skip = false;
    for (int i = 0; i < segments.size() - 1; ++i) {
      if (skippedDirectory(segments.at(i))) {
        skip = true;
        break;
      }
    }
    if (skip) continue;
    const QFileInfo info(path);
    parts << QStringLiteral("%1|%2|%3").arg(root.relativeFilePath(path)).arg(info.size()).arg(info.birthTime().toMSecsSinceEpoch());
  }
  parts.sort();
  return parts.join(QLatin1Char('#'));
}

void FileTree::collectDirectories(QStringList& dirs) const {
  dirs << m_root;
  for (int i = 0; i < topLevelItemCount(); ++i) {
    QTreeWidgetItem* item = topLevelItem(i);
    if (!item->data(0, kDirRole).toBool()) continue;
    collectUnder(item, dirs);
  }
}

void FileTree::collectUnder(const QTreeWidgetItem* item, QStringList& dirs) const {
  dirs << item->data(0, kPathRole).toString();
  for (int c = 0; c < item->childCount(); ++c) {
    QTreeWidgetItem* child = item->child(c);
    if (child->data(0, kDirRole).toBool()) collectUnder(child, dirs);
  }
}

bool FileTree::refreshIfChanged() {
  const QString current = signature();
  if (current == m_signature) return false;
  const QString selected = selectedPath();
  m_signature = current;
  setRoot(m_root);
  if (!selected.isEmpty()) selectPath(selected);
  return true;
}

void FileTree::watchCurrentDirectories() {
  if (m_watcher == nullptr) return;
  QStringList dirs;
  collectDirectories(dirs);
  const QStringList watched = m_watcher->directories();
  QStringList missing;
  for (const QString& dir : dirs) {
    if (!watched.contains(dir)) missing << dir;
  }
  if (!missing.isEmpty()) m_watcher->addPaths(missing);
}

void FileTree::scheduleRefresh() {
  if (m_debounce == nullptr) {
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    connect(m_debounce, &QTimer::timeout, this, [this] {
      refreshIfChanged();
      emit directoryChanged();
      watchCurrentDirectories();
    });
  }
  m_debounce->start(250);
}

void FileTree::stopDebounce() {
  if (m_debounce != nullptr) m_debounce->stop();
}

void FileTree::setWatchEnabled(bool enabled) {
  m_watchEnabled = enabled;
  if (!enabled) {
    stopDebounce();
    if (m_watcher != nullptr) {
      const QStringList watched = m_watcher->directories();
      if (!watched.isEmpty()) m_watcher->removePaths(watched);
    }
    return;
  }
  if (m_watcher == nullptr) {
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleRefresh(); });
  }
  watchCurrentDirectories();
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
