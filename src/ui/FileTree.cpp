#include "FileTree.h"

#include "FileOps.h"
#include "LinkNavigation.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCollator>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QDragEnterEvent>
#include <QEvent>
#include <QFileSystemWatcher>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QStyle>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <algorithm>

namespace typist::ui {
namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kDirRole = Qt::UserRole + 1;
constexpr int kOrderRole = Qt::UserRole + 2;

const auto kLocalFileMime = QStringLiteral("text/uri-list");

bool isLocalDropUrl(const QUrl& url) {
  if (!url.isValid() || !url.isLocalFile()) return false;
  const auto path = url.toLocalFile();
  return !path.contains(QChar::Null) && QDir::isAbsolutePath(path);
}

bool isExternalLocalDrop(const QDropEvent* event) {
  if (event->mimeData()->hasFormat(kSidebarLinkMime)) return false;
  const auto urls = event->mimeData()->urls();
  return std::any_of(urls.cbegin(), urls.cend(), isLocalDropUrl);
}

class SidebarEntryDrag : public QMimeData {
 public:
  SidebarEntryDrag(const FileTree* origin, const QString& entryPath)
      : tree(origin), root(origin->rootPath()), path(entryPath) {
    setData(kSidebarLinkMime, QUrl::fromLocalFile(path).toEncoded());
  }

  const FileTree* const tree;
  const QString root;
  const QString path;
};

bool dateSort(platform::FileBrowserSort sort) {
  return sort == platform::FileBrowserSort::Modified || sort == platform::FileBrowserSort::Created;
}
QDateTime createdDate(const QFileInfo& info) {
  const auto birth = info.birthTime();
  return birth.isValid() ? birth : info.lastModified();
}

bool skippedDirectory(const QString& name) {
  return name.compare(QLatin1String(".git"), Qt::CaseInsensitive) == 0 ||
         name == QLatin1String("build") || name == QLatin1String("node_modules");
}

struct FileGlob {
  QRegularExpression expression;
  bool relativePath = false;
};

FileGlob fileGlob(const QString& pattern) {
  const auto parts = pattern.split('/');
  QString expression;
  for (qsizetype index = 0; index < parts.size(); ++index) {
    const auto& part = parts[index];
    if (part == QLatin1String("**")) {
      if (index == parts.size() - 1)
        expression += QStringLiteral(".*");
      else if (parts[index + 1] != QLatin1String("**"))
        expression += QStringLiteral("(?:[^/]+/)*");
      continue;
    }
    auto segment = QRegularExpression::wildcardToRegularExpression(
        part, QRegularExpression::UnanchoredWildcardConversion);
    if (part.isEmpty() ||
        !QRegularExpression(QRegularExpression::anchoredPattern(segment)).isValid())
      return {QRegularExpression(QStringLiteral("[")), false};
    // A negated character class must also stay within its path component.
    segment.replace(QStringLiteral("[^"), QStringLiteral("[^/"));
    expression += segment;
    if (index < parts.size() - 1) expression += '/';
  }
  return {QRegularExpression(QRegularExpression::anchoredPattern(expression),
                             QRegularExpression::CaseInsensitiveOption |
                                 QRegularExpression::DotMatchesEverythingOption),
          parts.size() > 1};
}

QList<FileGlob> fileGlobs(const QStringList& patterns) {
  QList<FileGlob> globs;
  for (const auto& pattern : patterns) {
    const auto trimmed = pattern.trimmed();
    if (trimmed.isEmpty()) continue;
    const auto glob = fileGlob(trimmed);
    if (glob.expression.isValid()) globs.append(glob);
  }
  return globs;
}

bool matchesGlob(const QList<FileGlob>& globs, const QString& name, const QString& relativePath) {
  return std::any_of(globs.begin(), globs.end(), [&](const auto& glob) {
    return glob.expression.match(glob.relativePath ? relativePath : name).hasMatch();
  });
}

QStringList globLines(const QPlainTextEdit& input) {
  QStringList lines;
  for (const auto& line : input.toPlainText().split('\n')) {
    const auto trimmed = line.trimmed();
    if (!trimmed.isEmpty()) lines.append(trimmed);
  }
  return lines;
}

class FileItem : public QTreeWidgetItem {
 public:
  using QTreeWidgetItem::QTreeWidgetItem;
  bool operator<(const QTreeWidgetItem& other) const override {
    return data(0, kOrderRole).toInt() < other.data(0, kOrderRole).toInt();
  }
};
}  // namespace

FileTree::FileTree(QWidget* parent) : QTreeWidget(parent) {
  setHeaderHidden(true);
  setUniformRowHeights(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setTextElideMode(Qt::ElideMiddle);
  setDragDropMode(QAbstractItemView::DragDrop);
  setDragDropOverwriteMode(true);
  m_undoFileOperation = new QAction(tr("Undo File Operation"), this);
  m_undoFileOperation->setObjectName(QStringLiteral("undoFileOperation"));
  m_undoFileOperation->setEnabled(false);
  connect(m_undoFileOperation, &QAction::triggered, this, &FileTree::undoFileOperation);
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    if (item && !item->data(0, kDirRole).toBool())
      emit entryActivated(item->data(0, kPathRole).toString());
  });
}

QStringList FileTree::mimeTypes() const { return {kSidebarLinkMime, kLocalFileMime}; }

QMimeData* FileTree::mimeData(const QList<QTreeWidgetItem*>& items) const {
  if (items.isEmpty()) return nullptr;
  const QFileInfo entry(items.first()->data(0, kPathRole).toString());
  if (entry.isSymLink() || (!entry.isFile() && !entry.isDir())) return nullptr;
  return new SidebarEntryDrag(this, entry.absoluteFilePath());
}

Qt::DropActions FileTree::supportedDropActions() const {
  return m_options.view == platform::FileBrowserView::Tree ? Qt::CopyAction | Qt::MoveAction
                                                           : Qt::CopyAction;
}

void FileTree::startDrag(Qt::DropActions actions) {
  const auto items = selectedItems();
  auto* data = mimeData(items);
  if (!data) return;
  // The drop target moves the filesystem entry. The default item view drag would
  // try to remove the selected row again after a successful move.
  auto* drag = new QDrag(this);
  drag->setMimeData(data);
  drag->setPixmap(items.first()->icon(0).pixmap(24, 24));
  drag->exec(actions, Qt::CopyAction);
}

QString FileTree::draggedPath(const QDropEvent* event) const {
  if (m_options.view != platform::FileBrowserView::Tree ||
      !(event->possibleActions() & Qt::MoveAction))
    return {};
  // Only the live drag object from this tree can initiate a filesystem move.
  const auto* drag = dynamic_cast<const SidebarEntryDrag*>(event->mimeData());
  return drag && drag->tree == this && drag->root == m_root && m_items.contains(drag->path)
             ? drag->path
             : QString();
}

QString FileTree::dropFolder(const QPoint& position, const QString& source) const {
  const auto* item = itemAt(position);
  auto folder = item ? item->data(0, kPathRole).toString() : m_root;
  if (item && !item->data(0, kDirRole).toBool()) {
    if (!source.isEmpty()) return {};
    folder = QFileInfo(folder).absolutePath();
  }
  if (source.isEmpty()) return folder;
  if (folder == QFileInfo(source).absolutePath() || folder == source ||
      folder.startsWith(source + '/'))
    return {};
  return folder;
}

void FileTree::dragEnterEvent(QDragEnterEvent* event) {
  if (isExternalLocalDrop(event)) {
    const bool copy = !m_root.isEmpty() && (event->possibleActions() & Qt::CopyAction);
    event->setDropAction(copy ? Qt::CopyAction : Qt::IgnoreAction);
    if (copy) QTreeWidget::dragEnterEvent(event);
    event->accept();
  } else if (!draggedPath(event).isEmpty()) {
    QTreeWidget::dragEnterEvent(event);
  } else {
    event->ignore();
  }
}

void FileTree::dragMoveEvent(QDragMoveEvent* event) {
  if (isExternalLocalDrop(event)) {
    const bool copy = !m_root.isEmpty() && (event->possibleActions() & Qt::CopyAction);
    event->setDropAction(copy ? Qt::CopyAction : Qt::IgnoreAction);
    if (copy) {
      QTreeWidget::dragMoveEvent(event);
      event->setDropAction(Qt::CopyAction);
    }
    event->accept();
    return;
  }
  const auto source = draggedPath(event);
  const auto folder =
      source.isEmpty() ? QString() : dropFolder(event->position().toPoint(), source);
  QTreeWidget::dragMoveEvent(event);
  if (folder.isEmpty()) {
    event->ignore();
    return;
  }
  event->setDropAction(Qt::MoveAction);
  event->accept();
}

void FileTree::dropEvent(QDropEvent* event) {
  const auto source = draggedPath(event);
  // Let Qt clear the drop indicator and scrolling without applying a model drop.
  event->accept();
  QAbstractItemView::dropEvent(event);
  event->ignore();
  if (source.isEmpty() && !isExternalLocalDrop(event)) return;
  const auto folder = dropFolder(event->position().toPoint(), source);
  if (source.isEmpty()) {
    if (folder.isEmpty() || !(event->possibleActions() & Qt::CopyAction)) {
      event->setDropAction(Qt::IgnoreAction);
      event->accept();
    } else {
      importDroppedEntries(event->mimeData()->urls(), folder, event);
    }
    return;
  }
  if (folder.isEmpty()) return;
  QString error;
  const auto target = platform::moveWorkspaceEntry(m_root, source, folder, &error);
  if (target.isEmpty()) {
    if (!error.isEmpty()) emit fileOperationFailed(operationError(error));
    return;
  }
  recordFileOperation(source, target);
  refreshAfterRelocation(source, target);
  event->setDropAction(Qt::MoveAction);
  event->accept();
}

void FileTree::importDroppedEntries(const QList<QUrl>& urls, const QString& destination,
                                    QDropEvent* event) {
  QString firstFailure;
  int copied = 0;
  QSet<QString> seen;
  for (const auto& url : urls) {
    if (!isLocalDropUrl(url)) continue;
    const auto path = url.toLocalFile();
    if (seen.contains(path)) continue;
    seen.insert(path);
    QString error;
    if (!platform::copyIntoWorkspace(m_root, path, destination, &error).isEmpty()) {
      ++copied;
    } else if (firstFailure.isEmpty()) {
      firstFailure =
          QStringLiteral("%1 — %2").arg(QFileInfo(path).fileName(), operationError(error));
    }
  }
  if (copied > 0) {
    clearFileOperation();
    rebuild();
  }
  // Claim the drop even if all copies failed, so the window cannot open a
  // dropped document or insert an image after a sidebar import failure.
  event->setDropAction(copied > 0 ? Qt::CopyAction : Qt::IgnoreAction);
  event->accept();
  if (!firstFailure.isEmpty())
    emit fileOperationFailed(
        tr("Copied %1 of %2 entries. %3").arg(copied).arg(seen.size()).arg(firstFailure));
}

QList<QFileInfo> FileTree::scan() const {
  QList<QFileInfo> entries;
  if (m_root.isEmpty() || !QFileInfo(m_root).isDir()) return entries;
  auto filters =
      QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Readable;
  if (m_options.showHidden) filters |= QDir::Hidden;
  const auto includes = fileGlobs(m_options.includeGlobs);
  const auto excludes = fileGlobs(m_options.excludeGlobs);
  const QDir root(m_root);
  QStringList pending{m_root};
  while (!pending.isEmpty()) {
    QDirIterator iterator(pending.takeLast(), filters);
    while (iterator.hasNext()) {
      iterator.next();
      const auto info = iterator.fileInfo();
      const auto name = info.fileName();
      if (!m_options.showHidden && (info.isHidden() || name.startsWith('.'))) continue;
      const auto relative = includes.isEmpty() && excludes.isEmpty()
                                ? QString()
                                : root.relativeFilePath(info.absoluteFilePath());
      if (matchesGlob(excludes, name, relative)) continue;
      if (info.isDir()) {
        if (skippedDirectory(name)) continue;
        entries.append(info);
        pending.append(info.absoluteFilePath());
      } else if (includes.isEmpty() ? (m_options.showNonMarkdown || platform::isMarkdownPath(name))
                                    : matchesGlob(includes, name, relative)) {
        entries.append(info);
      }
    }
  }
  // Parents precede descendants; the order also makes the refresh signature deterministic.
  std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) {
    return a.absoluteFilePath() < b.absoluteFilePath();
  });
  return entries;
}

void FileTree::rememberExpansion() {
  if (m_options.view == platform::FileBrowserView::List && m_options.groupByFolder) {
    m_collapsedGroups.clear();
    for (int i = 0; i < topLevelItemCount(); ++i) {
      const auto* group = topLevelItem(i);
      if (!group->isExpanded()) m_collapsedGroups.insert(group->data(0, kPathRole).toString());
    }
    return;
  }
  if (m_options.view != platform::FileBrowserView::Tree) return;
  m_expanded.clear();
  QTreeWidgetItemIterator iterator(this);
  while (*iterator) {
    const auto* item = *iterator;
    if (item->data(0, kDirRole).toBool() && item->isExpanded())
      m_expanded.insert(item->data(0, kPathRole).toString());
    ++iterator;
  }
  m_hasExpansionState = true;
}

void FileTree::setRoot(const QString& rootDir) {
  const QString root = rootDir.isEmpty() ? QString() : QDir(rootDir).absolutePath();
  const bool sameRoot = root == m_root;
  if (sameRoot)
    rememberExpansion();
  else {
    clearFileOperation();
    m_expanded.clear();
    m_collapsedGroups.clear();
    m_hasExpansionState = false;
  }
  const auto selected =
      sameRoot && currentItem() ? currentItem()->data(0, kPathRole).toString() : QString();
  m_root = root;
  m_entries = scan();
  m_signature = signature(m_entries);
  renderEntries();
  if (auto* item = m_items.value(selected)) setCurrentItem(item);
  if (m_watchEnabled) watchCurrentDirectories();
}

void FileTree::renderEntries() {
  setUpdatesEnabled(false);
  clear();
  m_items.clear();
  const bool list = m_options.view == platform::FileBrowserView::List;
  const bool grouped = list && m_options.groupByFolder;
  setUniformRowHeights(!grouped);
  setRootIsDecorated(!list || grouped);
  setIndentation(grouped ? 12 : list ? 0 : 16);
  const QDir root(m_root);
  QList<QFileInfo> sorted = m_entries;
  // A fixed collation locale makes numeric order work even on C-locale CI hosts.
  QCollator collator(QLocale::English);
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  collator.setNumericMode(true);
  std::sort(sorted.begin(), sorted.end(), [&](const QFileInfo& a, const QFileInfo& b) {
    if (!list && a.isDir() != b.isDir()) return m_options.foldersFirst ? a.isDir() : !a.isDir();
    int comparison = 0;
    if (dateSort(m_options.sort)) {
      const bool created = m_options.sort == platform::FileBrowserSort::Created;
      const auto left = (created ? createdDate(a) : a.lastModified()).toMSecsSinceEpoch();
      const auto right = (created ? createdDate(b) : b.lastModified()).toMSecsSinceEpoch();
      comparison = left < right ? -1 : left > right ? 1 : 0;
    } else if (m_options.sort == platform::FileBrowserSort::Natural) {
      comparison = collator.compare(a.fileName(), b.fileName());
    } else {
      comparison = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive);
    }
    if (!comparison)
      comparison = QString::compare(root.relativeFilePath(a.absoluteFilePath()),
                                    root.relativeFilePath(b.absoluteFilePath()), Qt::CaseSensitive);
    return m_options.descending ? comparison > 0 : comparison < 0;
  });
  QHash<QString, int> ranks;
  for (int i = 0; i < sorted.size(); ++i) ranks.insert(sorted.at(i).absoluteFilePath(), i);
  const auto directoryIcon = style()->standardIcon(QStyle::SP_DirClosedIcon);
  const auto fileIcon = style()->standardIcon(QStyle::SP_FileIcon);
  if (grouped) {
    QSet<QString> uniqueFolders;
    for (const auto& info : m_entries)
      if (!info.isDir()) uniqueFolders.insert(info.absolutePath());
    QStringList folders = uniqueFolders.values();
    std::sort(folders.begin(), folders.end(), [&](const QString& a, const QString& b) {
      if (a == m_root || b == m_root) return a == m_root && b != m_root;
      const auto left = root.relativeFilePath(a), right = root.relativeFilePath(b);
      const int order = collator.compare(left, right);
      return order ? order < 0 : left < right;
    });
    for (int i = 0; i < folders.size(); ++i) {
      const auto& path = folders.at(i);
      auto* group = new FileItem(this);
      group->setText(0, path == m_root ? tr("Workspace Root")
                                       : QDir::toNativeSeparators(root.relativeFilePath(path)));
      group->setData(0, kPathRole, path);
      group->setData(0, kDirRole, true);
      group->setData(0, kOrderRole, i);
      group->setToolTip(0, QDir::toNativeSeparators(path));
      group->setIcon(0, directoryIcon);
      auto font = group->font(0);
      font.setBold(true);
      group->setFont(0, font);
      group->setExpanded(!m_collapsedGroups.contains(path));
      m_items.insert(path, group);
    }
  }
  for (const auto& info : m_entries) {
    if (list && info.isDir()) continue;
    const auto path = info.absoluteFilePath();
    auto* parent = list && !grouped ? nullptr : m_items.value(info.absolutePath());
    auto* item = parent ? new FileItem(parent) : new FileItem(this);
    QString text = info.fileName();
    if (list) {
      const auto folder = root.relativeFilePath(info.absolutePath());
      text +=
          '\n' + QDir::toNativeSeparators(folder == QLatin1String(".") ? root.dirName() : folder);
    }
    item->setText(0, text);
    item->setToolTip(0, QDir::toNativeSeparators(path));
    if (m_options.sort == platform::FileBrowserSort::Created) {
      const auto label = info.birthTime().isValid() ? tr("Created: %1")
                                                    : tr("Creation time unavailable. Modified: %1");
      item->setToolTip(0,
                       item->toolTip(0) + '\n' +
                           label.arg(QLocale().toString(createdDate(info), QLocale::ShortFormat)));
    }
    item->setData(0, kPathRole, path);
    item->setData(0, kDirRole, info.isDir());
    item->setData(0, kOrderRole, ranks.value(path));
    item->setIcon(0, info.isDir() ? directoryIcon : fileIcon);
    if (info.isDir()) item->setExpanded(!m_hasExpansionState || m_expanded.contains(path));
    m_items.insert(path, item);
  }
  sortItems(0, Qt::AscendingOrder);
  setUpdatesEnabled(true);
}

void FileTree::setOptions(const platform::FileBrowserOptions& options) {
  const bool filterChanged = options.showHidden != m_options.showHidden ||
                             options.showNonMarkdown != m_options.showNonMarkdown ||
                             options.includeGlobs != m_options.includeGlobs ||
                             options.excludeGlobs != m_options.excludeGlobs;
  if (options.view == m_options.view && options.sort == m_options.sort &&
      options.descending == m_options.descending &&
      options.foldersFirst == m_options.foldersFirst &&
      options.groupByFolder == m_options.groupByFolder && !filterChanged)
    return;
  const auto selected = currentItem() ? currentItem()->data(0, kPathRole).toString() : QString();
  rememberExpansion();
  const bool refreshDates = options.sort != m_options.sort && dateSort(options.sort);
  m_options = options;
  if (refreshDates || filterChanged) {
    m_entries = scan();
    m_signature = signature(m_entries);
  }
  renderEntries();
  if (auto* item = m_items.value(selected)) setCurrentItem(item);
  if (m_watchEnabled) watchCurrentDirectories();
  updateOptionsMenu();
  emit optionsChanged();
}

int FileTree::entryCount() const {
  return int(std::count_if(m_entries.begin(), m_entries.end(),
                           [](const QFileInfo& info) { return !info.isDir(); }));
}
int FileTree::directoryCount() const { return int(m_entries.size()) - entryCount(); }
QString FileTree::entryPath(int index) const {
  QTreeWidgetItemIterator iterator(const_cast<FileTree*>(this));
  while (*iterator) {
    if (!(*iterator)->data(0, kDirRole).toBool() && index-- == 0)
      return (*iterator)->data(0, kPathRole).toString();
    ++iterator;
  }
  return {};
}
QTreeWidgetItem* FileTree::findLeaf(const QString& path) const {
  auto* item = m_items.value(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
  if (!item) {
    // A workspace can use a canonical directory while an opened file uses an alias
    // (notably /var vs /private/var on macOS). Keep the displayed root unchanged.
    const auto canonical = QFileInfo(path).canonicalFilePath();
    const auto root = QFileInfo(m_root).canonicalFilePath();
    if (!canonical.isEmpty() && !root.isEmpty()) {
      const auto relative = QDir(root).relativeFilePath(canonical);
      item = m_items.value(QDir::cleanPath(QDir(m_root).filePath(relative)));
    }
  }
  return item && !item->data(0, kDirRole).toBool() ? item : nullptr;
}
void FileTree::selectPath(const QString& path) {
  if (auto* item = findLeaf(path)) {
    if (m_options.view == platform::FileBrowserView::List && m_options.groupByFolder &&
        item->parent())
      item->parent()->setExpanded(true);
    setCurrentItem(item);
  }
}
QString FileTree::selectedPath() const {
  const auto* item = currentItem();
  return item && !item->data(0, kDirRole).toBool() ? item->data(0, kPathRole).toString()
                                                   : QString();
}
void FileTree::rebuild() { setRoot(m_root); }
QString FileTree::signature(const QList<QFileInfo>& entries) const {
  QCryptographicHash hash(QCryptographicHash::Sha256);
  for (const auto& info : entries) {
    hash.addData(info.absoluteFilePath().toUtf8());
    hash.addData(QByteArray(1, '\0'));
    if (dateSort(m_options.sort)) {
      hash.addData(QByteArray::number((m_options.sort == platform::FileBrowserSort::Created
                                           ? createdDate(info)
                                           : info.lastModified())
                                          .toMSecsSinceEpoch()));
      hash.addData(QByteArray(1, '\0'));
    }
    hash.addData(info.isDir() ? QByteArray("dir")
                              : QByteArray::number(info.size()) + ':' +
                                    QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    hash.addData(QByteArray(1, '\0'));
  }
  return QString::fromLatin1(hash.result().toHex());
}
void FileTree::collectDirectories(QStringList& dirs) const {
  if (!m_root.isEmpty() && QFileInfo(m_root).isDir()) dirs.append(m_root);
  for (const auto& info : m_entries)
    if (info.isDir()) dirs.append(info.absoluteFilePath());
}
bool FileTree::refreshIfChanged() {
  auto entries = scan();
  const auto current = signature(entries);
  if (current == m_signature) return false;
  const auto selected = currentItem() ? currentItem()->data(0, kPathRole).toString() : QString();
  rememberExpansion();
  m_entries = std::move(entries);
  m_signature = current;
  renderEntries();
  if (auto* item = m_items.value(selected)) setCurrentItem(item);
  if (m_watchEnabled) watchCurrentDirectories();
  return true;
}
void FileTree::refresh() {
  refreshIfChanged();
  // Atomic replacement can drop a file watch even when size and timestamp are unchanged.
  if (m_watchEnabled) watchCurrentDirectories();
  emit directoryChanged();
}
void FileTree::watchCurrentDirectories() {
  if (!m_watcher || !m_watchEnabled) return;
  QStringList dirs;
  collectDirectories(dirs);
  if (dateSort(m_options.sort))
    for (const auto& info : m_entries)
      if (!info.isDir()) dirs.append(info.absoluteFilePath());
  const QSet<QString> wanted(dirs.begin(), dirs.end());
  const auto watched = m_watcher->directories() + m_watcher->files();
  const QSet<QString> existing(watched.begin(), watched.end());
  QStringList stale, missing;
  for (const auto& dir : watched)
    if (!wanted.contains(dir)) stale.append(dir);
  for (const auto& dir : dirs)
    if (!existing.contains(dir)) missing.append(dir);
  if (!stale.isEmpty()) m_watcher->removePaths(stale);
  if (!missing.isEmpty()) m_watcher->addPaths(missing);
}
void FileTree::scheduleRefresh() {
  if (!m_debounce) {
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    connect(m_debounce, &QTimer::timeout, this, &FileTree::refresh);
  }
  m_debounce->start(250);
}
void FileTree::stopDebounce() {
  if (m_debounce) m_debounce->stop();
}
void FileTree::setWatchEnabled(bool enabled) {
  const bool wasEnabled = m_watchEnabled;
  m_watchEnabled = enabled;
  if (!enabled) {
    stopDebounce();
    if (m_watcher) {
      const auto watched = m_watcher->directories() + m_watcher->files();
      if (!watched.isEmpty()) m_watcher->removePaths(watched);
    }
    return;
  }
  if (!m_watcher) {
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { scheduleRefresh(); });
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { scheduleRefresh(); });
  }
  if (!wasEnabled) refreshIfChanged();
  watchCurrentDirectories();
}

QMenu* FileTree::createOptionsMenu(QWidget* parent) {
  if (m_optionsMenu) return m_optionsMenu;
  m_optionsMenu = new QMenu(parent);
  m_optionsMenu->setObjectName(QStringLiteral("fileBrowserMenu"));
  const auto add = [this](const char* id, const char* source, bool checkable) {
    auto* action = m_optionsMenu->addAction(tr(source));
    action->setObjectName(QString::fromLatin1(id));
    action->setProperty("fileBrowserSource", source);
    action->setCheckable(checkable);
    return action;
  };
  auto* views = new QActionGroup(m_optionsMenu);
  for (const auto& pair : {qMakePair("browserTree", QT_TR_NOOP("File Tree")),
                           qMakePair("browserList", QT_TR_NOOP("File List"))}) {
    auto* action = add(pair.first, pair.second, true);
    views->addAction(action);
    connect(action, &QAction::triggered, this, [this, action] {
      auto options = m_options;
      options.view = action->objectName() == QLatin1String("browserList")
                         ? platform::FileBrowserView::List
                         : platform::FileBrowserView::Tree;
      setOptions(options);
    });
  }
  m_optionsMenu->addSeparator();
  auto* sorting = new QActionGroup(m_optionsMenu);
  for (const auto& pair : {qMakePair("browserName", QT_TR_NOOP("Name")),
                           qMakePair("browserNatural", QT_TR_NOOP("Natural Order")),
                           qMakePair("browserModified", QT_TR_NOOP("Modified Date")),
                           qMakePair("browserCreated", QT_TR_NOOP("Created Date"))}) {
    auto* action = add(pair.first, pair.second, true);
    sorting->addAction(action);
    connect(action, &QAction::triggered, this, [this, action] {
      auto options = m_options;
      options.sort = action->objectName() == QLatin1String("browserNatural")
                         ? platform::FileBrowserSort::Natural
                     : action->objectName() == QLatin1String("browserModified")
                         ? platform::FileBrowserSort::Modified
                     : action->objectName() == QLatin1String("browserCreated")
                         ? platform::FileBrowserSort::Created
                         : platform::FileBrowserSort::Name;
      setOptions(options);
    });
  }
  m_optionsMenu->addSeparator();
  auto* descending = add("browserDescending", QT_TR_NOOP("Descending"), true);
  connect(descending, &QAction::triggered, this, [this](bool checked) {
    auto options = m_options;
    options.descending = checked;
    setOptions(options);
  });
  auto* folders = add("browserFoldersFirst", QT_TR_NOOP("Folders First"), true);
  connect(folders, &QAction::triggered, this, [this](bool checked) {
    auto options = m_options;
    options.foldersFirst = checked;
    setOptions(options);
  });
  auto* grouping = add("browserGroupByFolder", QT_TR_NOOP("Group by Folder"), true);
  connect(grouping, &QAction::triggered, this, [this](bool checked) {
    auto options = m_options;
    options.groupByFolder = checked;
    setOptions(options);
  });
  m_optionsMenu->addSeparator();
  auto* hidden = add("browserShowHidden", QT_TR_NOOP("Show Hidden Files and Folders"), true);
  connect(hidden, &QAction::triggered, this, [this](bool checked) {
    auto options = m_options;
    options.showHidden = checked;
    setOptions(options);
  });
  auto* nonMarkdown = add("browserShowNonMarkdown", QT_TR_NOOP("Show Non-Markdown Files"), true);
  connect(nonMarkdown, &QAction::triggered, this, [this](bool checked) {
    auto options = m_options;
    options.showNonMarkdown = checked;
    setOptions(options);
  });
  connect(add("browserFileFilters", QT_TR_NOOP("File Filters…"), false), &QAction::triggered, this,
          &FileTree::editFileFilters);
  connect(add("browserRefresh", QT_TR_NOOP("Refresh"), false), &QAction::triggered, this,
          &FileTree::refresh);
  m_optionsMenu->addSeparator();
  m_optionsMenu->addAction(m_undoFileOperation);
  updateOptionsMenu();
  return m_optionsMenu;
}

void FileTree::editFileFilters() {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("fileFiltersDialog"));
  dialog.setWindowTitle(tr("File Filters"));
  dialog.resize(460, 390);
  auto* layout = new QVBoxLayout(&dialog);
  auto* hint = new QLabel(
      tr("Use one pattern per line. Match names with *.png or paths with notes/**/*.md."), &dialog);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  auto* precedence = new QLabel(
      tr("Include patterns replace the file-type filter. Exclude patterns take precedence."),
      &dialog);
  precedence->setWordWrap(true);
  layout->addWidget(precedence);
  const auto field = [&](const QString& label, const QString& id, const QStringList& patterns) {
    auto* caption = new QLabel(label, &dialog);
    auto* input = new QPlainTextEdit(patterns.join('\n'), &dialog);
    input->setObjectName(id);
    input->setAccessibleName(label);
    input->setTabChangesFocus(true);
    caption->setBuddy(input);
    layout->addWidget(caption);
    layout->addWidget(input);
    return input;
  };
  const auto* includes =
      field(tr("Include files (optional)"), QStringLiteral("includeGlobs"), m_options.includeGlobs);
  const auto* excludes = field(tr("Exclude files and folders"), QStringLiteral("excludeGlobs"),
                               m_options.excludeGlobs);
  auto* error = new QLabel(&dialog);
  error->setObjectName(QStringLiteral("fileFilterError"));
  error->setTextFormat(Qt::PlainText);
  error->setWordWrap(true);
  error->hide();
  layout->addWidget(error);
  auto* buttons = new QDialogButtonBox(&dialog);
  auto* apply = buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole);
  apply->setObjectName(QStringLiteral("fileFiltersAccept"));
  buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  auto options = m_options;
  connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
    options.includeGlobs = globLines(*includes);
    options.excludeGlobs = globLines(*excludes);
    for (const auto& pattern : options.includeGlobs + options.excludeGlobs) {
      if (!fileGlob(pattern).expression.isValid()) {
        error->setText(tr("Invalid file pattern: %1").arg(pattern));
        error->show();
        return;
      }
    }
    dialog.accept();
  });
  if (dialog.exec() == QDialog::Accepted) setOptions(options);
}

void FileTree::updateOptionsMenu() {
  if (!m_optionsMenu) return;
  for (auto* action : m_optionsMenu->actions()) {
    const auto source = action->property("fileBrowserSource").toByteArray();
    if (!source.isEmpty()) action->setText(tr(source.constData()));
    const auto id = action->objectName();
    if (id == QLatin1String("browserTree"))
      action->setChecked(m_options.view == platform::FileBrowserView::Tree);
    if (id == QLatin1String("browserList"))
      action->setChecked(m_options.view == platform::FileBrowserView::List);
    if (id == QLatin1String("browserName"))
      action->setChecked(m_options.sort == platform::FileBrowserSort::Name);
    if (id == QLatin1String("browserNatural"))
      action->setChecked(m_options.sort == platform::FileBrowserSort::Natural);
    if (id == QLatin1String("browserModified"))
      action->setChecked(m_options.sort == platform::FileBrowserSort::Modified);
    if (id == QLatin1String("browserCreated"))
      action->setChecked(m_options.sort == platform::FileBrowserSort::Created);
    if (id == QLatin1String("browserGroupByFolder")) {
      action->setChecked(m_options.groupByFolder);
      action->setEnabled(m_options.view == platform::FileBrowserView::List);
    }
    if (id == QLatin1String("browserDescending")) action->setChecked(m_options.descending);
    if (id == QLatin1String("browserShowHidden")) action->setChecked(m_options.showHidden);
    if (id == QLatin1String("browserShowNonMarkdown"))
      action->setChecked(m_options.showNonMarkdown);
    if (id == QLatin1String("browserFoldersFirst")) {
      action->setChecked(m_options.foldersFirst);
      action->setEnabled(m_options.view == platform::FileBrowserView::Tree);
    }
  }
}
void FileTree::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    m_undoFileOperation->setText(tr("Undo File Operation"));
    const auto selected = currentItem() ? currentItem()->data(0, kPathRole).toString() : QString();
    rememberExpansion();
    renderEntries();
    if (auto* item = m_items.value(selected)) setCurrentItem(item);
    updateOptionsMenu();
  }
  QTreeWidget::changeEvent(event);
}
void FileTree::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
    if (auto* item = currentItem()) {
      if (item->data(0, kDirRole).toBool())
        item->setExpanded(!item->isExpanded());
      else
        emit entryActivated(item->data(0, kPathRole).toString());
    }
    event->accept();
    return;
  }
  QTreeWidget::keyPressEvent(event);
}

void FileTree::contextMenuEvent(QContextMenuEvent* event) {
  if (event->reason() == QContextMenuEvent::Mouse) setCurrentItem(itemAt(event->pos()));
  QScopedPointer<QMenu> menu(createContextMenu());
  menu->exec(event->globalPos());
}

QMenu* FileTree::createContextMenu(QWidget* parent) {
  const auto* item = currentItem();
  auto* menu = new QMenu(parent ? parent : this);
  menu->setObjectName(QStringLiteral("fileContextMenu"));
  menu->setToolTipsVisible(true);
  const bool onDirectory = !item || item->data(0, kDirRole).toBool();
  const auto path = item ? item->data(0, kPathRole).toString() : m_root;
  const bool external = !onDirectory && !platform::isMarkdownPath(path);
  const auto dir = onDirectory ? path : QFileInfo(path).absolutePath();
  const auto root = m_root;
  const auto add = [menu](const char* id, const QString& label) {
    auto* action = menu->addAction(label);
    action->setObjectName(QString::fromLatin1(id));
    return action;
  };
  if (external) {
    auto* open = add("fileOpenExternally", tr("Open in Default App"));
    open->setEnabled(QFileInfo(path).isFile());
    connect(open, &QAction::triggered, this, [this, path] { emit entryActivated(path); });
  } else {
    auto* open = add("fileOpenInNewWindow", tr("Open in New Window"));
    open->setEnabled(QFileInfo::exists(path));
    connect(open, &QAction::triggered, this, [this, path] { emit entryOpenInNewWindow(path); });
  }
  menu->addSeparator();
  auto* newFile = add("fileNewMarkdown", tr("New Markdown File"));
  auto* newFolder = add("fileNewFolder", tr("New Folder"));
  newFile->setEnabled(!dir.isEmpty() && QFileInfo(dir).isDir());
  newFolder->setEnabled(newFile->isEnabled());
  connect(newFile, &QAction::triggered, this, [this, dir] { newFileIn(dir); });
  connect(newFolder, &QAction::triggered, this, [this, dir] { newFolderIn(dir); });
  if (!onDirectory && !external) {
    menu->addSeparator();
    auto* duplicate = add("fileDuplicate", tr("Duplicate"));
    duplicate->setToolTip(
        tr("Copy the saved file beside the original. Unsaved edits are not included."));
    const QFileInfo file(path);
    const bool available = file.isFile() && !file.isSymLink();
    duplicate->setEnabled(available);
    connect(duplicate, &QAction::triggered, this, [this, path] { duplicateFile(path); });
    connect(add("fileRename", tr("Rename")), &QAction::triggered, this,
            [this, path] { renameFile(path); });
    auto* trash = add("fileMoveToTrash", tr("Move to Trash"));
    trash->setEnabled(available);
    connect(trash, &QAction::triggered, this, [this, path] { moveFileToTrash(path); });
  }
  menu->addSeparator();
  auto* copy = add("fileCopyPath", tr("Copy File Path"));
  copy->setEnabled(!path.isEmpty());
  connect(copy, &QAction::triggered, this, [path] {
    QApplication::clipboard()->setText(
        QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
  });
  auto* relative = add("fileCopyRelativePath", tr("Copy Relative Path"));
  relative->setEnabled(!path.isEmpty() && !root.isEmpty());
  connect(relative, &QAction::triggered, this,
          [path, root] { QApplication::clipboard()->setText(QDir(root).relativeFilePath(path)); });
  auto* reveal = add("fileReveal", onDirectory ? tr("Open Folder") : tr("Reveal in File Manager"));
  reveal->setEnabled(!platform::containingDirectoryUrl(path).isEmpty());
  connect(reveal, &QAction::triggered, this, [this, path, onDirectory] {
    if (!onDirectory) {
      platform::revealFile(path, this, [this] {
        QMessageBox::warning(this, tr("Reveal in File Manager"),
                             tr("Cannot show the file in the file manager."));
      });
      return;
    }
    if (!QFileInfo(path).isDir() || !QDesktopServices::openUrl(QUrl::fromLocalFile(path)))
      QMessageBox::warning(this, tr("Open Folder"),
                           tr("Cannot open the folder in the file manager."));
  });
  menu->addSeparator();
  menu->addAction(m_undoFileOperation);
  return menu;
}

QString FileTree::askName(const QString& title, const QString& label, const QString& value) {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("fileNameDialog"));
  dialog.setWindowTitle(title);
  dialog.setMinimumWidth(320);
  auto* layout = new QVBoxLayout(&dialog);
  auto* caption = new QLabel(label, &dialog);
  caption->setTextFormat(Qt::PlainText);
  auto* input = new QLineEdit(value, &dialog);
  input->setObjectName(QStringLiteral("fileNameInput"));
  input->setAccessibleName(label);
  input->selectAll();
  caption->setBuddy(input);
  layout->addWidget(caption);
  layout->addWidget(input);
  // Custom buttons keep the application translation when Qt shows the dialog.
  auto* buttons = new QDialogButtonBox(&dialog);
  buttons->setObjectName(QStringLiteral("fileNameButtons"));
  auto* accept = buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole);
  accept->setObjectName(QStringLiteral("fileNameAccept"));
  accept->setDefault(true);
  buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole)
      ->setObjectName(QStringLiteral("fileNameCancel"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  return dialog.exec() == QDialog::Accepted ? input->text().trimmed() : QString();
}

QString FileTree::operationError(const QString& error) const {
  if (error == QLatin1String("Invalid name"))
    return tr("Use a single name without path separators or reserved characters.");
  if (error == QLatin1String("Folder unavailable")) return tr("The folder is no longer available.");
  if (error == QLatin1String("File unavailable")) return tr("The file is no longer available.");
  if (error == QLatin1String("Entry unavailable"))
    return tr("The file or folder is no longer available.");
  if (error == QLatin1String("Entry replaced"))
    return tr("The original file or folder has been replaced.");
  if (error == QLatin1String("A file with that name already exists"))
    return tr("A file or folder with that name already exists.");
  if (error == QLatin1String("Cannot create file"))
    return tr("Cannot create the file. Check folder permissions.");
  if (error == QLatin1String("Cannot create folder"))
    return tr("Cannot create the folder. Check folder permissions.");
  if (error == QLatin1String("Copy failed"))
    return tr("Cannot copy the file. Check the source and folder permissions.");
  if (error == QLatin1String("Rename failed"))
    return tr("Cannot rename the file. Check the name and permissions.");
  if (error == QLatin1String("Cannot move into itself"))
    return tr("A folder cannot be moved into itself.");
  if (error == QLatin1String("Cannot copy into itself"))
    return tr("A folder cannot be copied into itself.");
  if (error == QLatin1String("Import failed"))
    return tr("Cannot copy the entry. Check permissions and folder contents.");
  if (error == QLatin1String("Move failed"))
    return tr("Cannot move the entry. Check permissions and use a folder on the same drive.");
  if (error == QLatin1String("Trash failed"))
    return tr("Cannot move the file to the system trash.");
  return error;
}
void FileTree::newFileIn(const QString& dir) {
  const auto name =
      askName(tr("New Markdown File"), tr("File name"), QStringLiteral("untitled.md"));
  if (name.isEmpty()) return;
  QString error;
  if (!platform::createMarkdownFile(dir, name, &error)) {
    QMessageBox::warning(this, tr("New Markdown File"), operationError(error));
    return;
  }
  clearFileOperation();
  rebuild();
  emit entryActivated(QDir(dir).filePath(name.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)
                                             ? name
                                             : name + QStringLiteral(".md")));
}
void FileTree::newFolderIn(const QString& dir) {
  const auto name = askName(tr("New Folder"), tr("Folder name"), tr("New Folder"));
  if (name.isEmpty()) return;
  QString error;
  if (!platform::createDirectory(dir, name, &error)) {
    QMessageBox::warning(this, tr("New Folder"), operationError(error));
    return;
  }
  clearFileOperation();
  rebuild();
  if (auto* item = m_items.value(QDir(dir).filePath(name))) {
    setCurrentItem(item);
    for (auto* ancestor = item->parent(); ancestor; ancestor = ancestor->parent())
      ancestor->setExpanded(true);
    scrollToItem(item);
  }
}

void FileTree::duplicateFile(const QString& path) {
  QString error;
  const auto target = platform::duplicateMarkdownFile(path, &error);
  if (target.isEmpty()) {
    QMessageBox::warning(this, tr("Duplicate"), operationError(error));
    return;
  }
  clearFileOperation();
  rebuild();
  selectPath(target);
  if (currentItem()) scrollToItem(currentItem());
}

void FileTree::renameFile(const QString& path) {
  if (path.isEmpty()) return;
  const auto name = askName(tr("Rename File"), tr("New name"), QFileInfo(path).fileName());
  if (name.isEmpty()) return;
  QString error;
  if (!platform::renameFile(path, name, &error)) {
    QMessageBox::warning(this, tr("Rename File"), operationError(error));
    return;
  }
  const auto newPath = QDir(QFileInfo(path).absolutePath())
                           .filePath(name.endsWith(QLatin1String(".md"), Qt::CaseInsensitive)
                                         ? name
                                         : name + QStringLiteral(".md"));
  if (newPath == path) return;
  recordFileOperation(path, newPath);
  refreshAfterRelocation(path, newPath);
}
void FileTree::moveFileToTrash(const QString& path) {
  QMessageBox confirm(QMessageBox::Question, tr("Move to Trash"),
                      tr("Move %1 to the system trash?").arg(QFileInfo(path).fileName()),
                      QMessageBox::NoButton, this);
  confirm.setTextFormat(Qt::PlainText);
  auto* move = confirm.addButton(tr("Move to Trash"), QMessageBox::AcceptRole);
  auto* cancel = confirm.addButton(tr("Cancel"), QMessageBox::RejectRole);
  confirm.setDefaultButton(cancel);
  confirm.setEscapeButton(cancel);
  confirm.exec();
  if (confirm.clickedButton() != move) return;
  QString error;
  if (!platform::moveMarkdownToTrash(path, &error)) {
    QMessageBox::warning(this, tr("Move to Trash"), operationError(error));
    return;
  }
  clearFileOperation();
  rebuild();
  emit fileRemoved(path);
}

void FileTree::recordFileOperation(const QString& before, const QString& after) {
  m_lastFileOperation = {before, after, platform::workspaceEntryIdentity(after)};
  m_undoFileOperation->setEnabled(!m_lastFileOperation.identity.isEmpty());
}

void FileTree::clearFileOperation() {
  m_lastFileOperation = {};
  m_undoFileOperation->setEnabled(false);
}

void FileTree::undoFileOperation() {
  const auto operation = m_lastFileOperation;
  if (operation.identity.isEmpty()) return;
  QString error;
  if (!platform::restoreWorkspaceEntry(m_root, operation.after, operation.before,
                                       operation.identity, &error)) {
    emit fileOperationFailed(tr("Cannot undo the file operation. %1").arg(operationError(error)));
    return;
  }
  clearFileOperation();
  refreshAfterRelocation(operation.after, operation.before);
}

void FileTree::refreshAfterRelocation(const QString& oldPath, const QString& newPath) {
  rebuild();
  if (auto* item = m_items.value(newPath)) {
    setCurrentItem(item);
    for (auto* ancestor = item->parent(); ancestor; ancestor = ancestor->parent())
      ancestor->setExpanded(true);
    scrollToItem(item);
  }
  emit entryMoved(oldPath, newPath);
}

QByteArray FileTree::undoIdentityBeforeSave(const QString& path) const {
  if (m_lastFileOperation.identity.isEmpty() ||
      QFileInfo(path) != QFileInfo(m_lastFileOperation.after))
    return {};
  const auto identity = platform::workspaceEntryIdentity(path);
  return identity == m_lastFileOperation.identity ? identity : QByteArray();
}

void FileTree::updateUndoIdentityAfterSave(const QByteArray& previousIdentity) {
  if (previousIdentity.isEmpty() || previousIdentity != m_lastFileOperation.identity) return;
  // Saving this tracked file legitimately replaces its on-disk identity.
  m_lastFileOperation.identity = platform::workspaceEntryIdentity(m_lastFileOperation.after);
  m_undoFileOperation->setEnabled(!m_lastFileOperation.identity.isEmpty());
}
}  // namespace typist::ui
