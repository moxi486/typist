#include "FileTree.h"

#include "FileOps.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCollator>
#include <QContextMenuEvent>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLayout>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedPointer>
#include <QStyle>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <algorithm>

namespace typist::ui {
namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kDirRole = Qt::UserRole + 1;
constexpr int kOrderRole = Qt::UserRole + 2;

bool skippedDirectory(const QString& name) {
  return name.startsWith('.') || name == QLatin1String("build") ||
         name == QLatin1String("node_modules");
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
  connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
    if (item && !item->data(0, kDirRole).toBool())
      emit entryActivated(item->data(0, kPathRole).toString());
  });
}

QList<QFileInfo> FileTree::scan() const {
  QList<QFileInfo> entries;
  if (m_root.isEmpty() || !QFileInfo(m_root).isDir()) return entries;
  QStringList pending{m_root};
  while (!pending.isEmpty()) {
    QDirIterator iterator(pending.takeLast(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                                                  QDir::NoSymLinks | QDir::Readable);
    while (iterator.hasNext()) {
      iterator.next();
      const auto info = iterator.fileInfo();
      if (info.isHidden() || info.fileName().startsWith('.')) continue;
      if (info.isDir()) {
        if (skippedDirectory(info.fileName())) continue;
        entries.append(info);
        pending.append(info.absoluteFilePath());
      } else if (info.suffix().compare(QLatin1String("md"), Qt::CaseInsensitive) == 0) {
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
    m_expanded.clear();
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
  setRootIsDecorated(!list);
  setIndentation(list ? 0 : 16);
  const QDir root(m_root);
  QList<QFileInfo> sorted = m_entries;
  // A fixed collation locale makes numeric order work even on C-locale CI hosts.
  QCollator collator(QLocale::English);
  collator.setCaseSensitivity(Qt::CaseInsensitive);
  collator.setNumericMode(true);
  std::sort(sorted.begin(), sorted.end(), [&](const QFileInfo& a, const QFileInfo& b) {
    if (!list && a.isDir() != b.isDir()) return m_options.foldersFirst ? a.isDir() : !a.isDir();
    int comparison = 0;
    if (m_options.sort == platform::FileBrowserSort::Modified) {
      const auto left = a.lastModified().toMSecsSinceEpoch();
      const auto right = b.lastModified().toMSecsSinceEpoch();
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
  for (const auto& info : m_entries) {
    if (list && info.isDir()) continue;
    const auto path = info.absoluteFilePath();
    auto* parent = list ? nullptr : m_items.value(info.absolutePath());
    auto* item = parent ? new FileItem(parent) : new FileItem(this);
    QString text = info.fileName();
    if (list) {
      const auto folder = root.relativeFilePath(info.absolutePath());
      text +=
          '\n' + QDir::toNativeSeparators(folder == QLatin1String(".") ? root.dirName() : folder);
    }
    item->setText(0, text);
    item->setToolTip(0, QDir::toNativeSeparators(path));
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
  if (options.view == m_options.view && options.sort == m_options.sort &&
      options.descending == m_options.descending && options.foldersFirst == m_options.foldersFirst)
    return;
  const auto selected = currentItem() ? currentItem()->data(0, kPathRole).toString() : QString();
  rememberExpansion();
  m_options = options;
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
  return item && !item->data(0, kDirRole).toBool() ? item : nullptr;
}
void FileTree::selectPath(const QString& path) {
  if (auto* item = findLeaf(path)) setCurrentItem(item);
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
  if (m_options.sort == platform::FileBrowserSort::Modified)
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
                           qMakePair("browserModified", QT_TR_NOOP("Modified Date"))}) {
    auto* action = add(pair.first, pair.second, true);
    sorting->addAction(action);
    connect(action, &QAction::triggered, this, [this, action] {
      auto options = m_options;
      options.sort = action->objectName() == QLatin1String("browserNatural")
                         ? platform::FileBrowserSort::Natural
                     : action->objectName() == QLatin1String("browserModified")
                         ? platform::FileBrowserSort::Modified
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
  m_optionsMenu->addSeparator();
  connect(add("browserRefresh", QT_TR_NOOP("Refresh"), false), &QAction::triggered, this,
          &FileTree::refresh);
  updateOptionsMenu();
  return m_optionsMenu;
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
    if (id == QLatin1String("browserDescending")) action->setChecked(m_options.descending);
    if (id == QLatin1String("browserFoldersFirst")) {
      action->setChecked(m_options.foldersFirst);
      action->setEnabled(m_options.view == platform::FileBrowserView::Tree);
    }
  }
}
void FileTree::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) updateOptionsMenu();
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
  const auto dir = onDirectory ? path : QFileInfo(path).absolutePath();
  const auto root = m_root;
  const auto add = [menu](const char* id, const QString& label) {
    auto* action = menu->addAction(label);
    action->setObjectName(QString::fromLatin1(id));
    return action;
  };
  auto* newFile = add("fileNewMarkdown", tr("New Markdown File"));
  auto* newFolder = add("fileNewFolder", tr("New Folder"));
  newFile->setEnabled(!dir.isEmpty() && QFileInfo(dir).isDir());
  newFolder->setEnabled(newFile->isEnabled());
  connect(newFile, &QAction::triggered, this, [this, dir] { newFileIn(dir); });
  connect(newFolder, &QAction::triggered, this, [this, dir] { newFolderIn(dir); });
  if (!onDirectory) {
    menu->addSeparator();
    auto* duplicate = add("fileDuplicate", tr("Duplicate"));
    duplicate->setToolTip(
        tr("Copy the saved file beside the original. Unsaved edits are not included."));
    duplicate->setEnabled(QFileInfo(path).isFile() && !QFileInfo(path).isSymLink());
    connect(duplicate, &QAction::triggered, this, [this, path] { duplicateFile(path); });
    connect(add("fileRename", tr("Rename")), &QAction::triggered, this,
            [this, path] { renameFile(path); });
    connect(add("fileDelete", tr("Delete")), &QAction::triggered, this,
            [this, path] { deleteFile(path); });
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
  auto* reveal = add("fileReveal", tr("Open Containing Folder"));
  reveal->setEnabled(!platform::containingDirectoryUrl(path).isEmpty());
  connect(reveal, &QAction::triggered, this, [this, path] {
    const auto url = platform::containingDirectoryUrl(path);
    if (url.isEmpty() || !QDesktopServices::openUrl(url))
      QMessageBox::warning(this, tr("Open Containing Folder"),
                           tr("Cannot open the folder in the file manager."));
  });
  return menu;
}

QString FileTree::askName(const QString& title, const QString& label, const QString& value) {
  QInputDialog dialog(this);
  dialog.setObjectName(QStringLiteral("fileNameDialog"));
  dialog.setWindowTitle(title);
  dialog.setLabelText(label);
  dialog.setTextValue(value);
  dialog.setOption(QInputDialog::NoButtons);
  // Custom buttons keep the application translation when Qt shows the dialog.
  auto* buttons = new QDialogButtonBox(&dialog);
  buttons->setObjectName(QStringLiteral("fileNameButtons"));
  auto* accept = buttons->addButton(tr("OK"), QDialogButtonBox::AcceptRole);
  accept->setObjectName(QStringLiteral("fileNameAccept"));
  accept->setDefault(true);
  buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole)
      ->setObjectName(QStringLiteral("fileNameCancel"));
  dialog.layout()->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  return dialog.exec() == QDialog::Accepted ? dialog.textValue().trimmed() : QString();
}

QString FileTree::operationError(const QString& error) const {
  if (error == QLatin1String("Invalid name"))
    return tr("Use a single name without path separators or reserved characters.");
  if (error == QLatin1String("Folder unavailable")) return tr("The folder is no longer available.");
  if (error == QLatin1String("File unavailable")) return tr("The file is no longer available.");
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
  if (error == QLatin1String("Delete failed"))
    return tr("Cannot delete the file. Check file permissions.");
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
  rebuild();
  selectPath(newPath);
  emit fileRenamed(path, newPath);
}
void FileTree::deleteFile(const QString& path) {
  if (path.isEmpty()) return;
  if (QMessageBox::question(this, tr("Delete File"),
                            tr("Delete %1?").arg(QFileInfo(path).fileName())) != QMessageBox::Yes)
    return;
  QString error;
  if (!platform::deleteFile(path, &error)) {
    QMessageBox::warning(this, tr("Delete File"), operationError(error));
    return;
  }
  rebuild();
  emit fileRemoved(path);
}
}  // namespace typist::ui
