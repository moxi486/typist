#include "FileOps.h"
#include "FileTree.h"
#include "Settings.h"

#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QFile>
#include <QFileSystemWatcher>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScopeGuard>
#include <QScopedPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidgetItemIterator>
#include <QtTest>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using typist::platform::FileBrowserOptions;
using typist::platform::FileBrowserSort;
using typist::platform::FileBrowserView;
using typist::ui::FileTree;

namespace {
bool writeFile(const QString& path, const QByteArray& contents = "# Note\n", qint64 time = 0) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size()) return false;
  if (time) {
    file.flush();
    return file.setFileTime(QDateTime::fromSecsSinceEpoch(time), QFileDevice::FileModificationTime);
  }
  return true;
}
QTreeWidgetItem* itemForPath(FileTree& tree, const QString& path) {
  QTreeWidgetItemIterator iterator(&tree);
  while (*iterator) {
    if ((*iterator)->data(0, Qt::UserRole).toString() == path) return *iterator;
    ++iterator;
  }
  return nullptr;
}
QStringList filenames(const FileTree& tree) {
  QStringList result;
  for (int i = 0; i < tree.entryCount(); ++i)
    result.append(QFileInfo(tree.entryPath(i)).fileName());
  return result;
}
}  // namespace

class FolderReceiver : public QObject {
  Q_OBJECT
 public:
  QUrl last;
  ~FolderReceiver() override { QDesktopServices::unsetUrlHandler("file"); }
 public slots:
  void capture(const QUrl& url) { last = url; }
};

class FileBrowserTests : public QObject {
  Q_OBJECT
 private slots:
  void groupedListSortsWithinParentsAndNavigates() {
    QTemporaryDir directory;
    for (const auto& name : {"root.md", "folder2/10-note.md", "folder2/2-note.md",
                             "folder2/nested/same.md", "folder10/same.md"})
      QVERIFY(writeFile(directory.filePath(QString::fromLatin1(name))));
    QVERIFY(QDir().mkpath(directory.filePath("empty")));
    FileTree tree;
    FileBrowserOptions options;
    options.view = FileBrowserView::List;
    options.sort = FileBrowserSort::Natural;
    options.groupByFolder = true;
    tree.setOptions(options);
    tree.setRoot(directory.path());
    QCOMPARE(tree.entryCount(), 5);
    QCOMPARE(tree.directoryCount(), 4);
    QCOMPARE(tree.topLevelItemCount(), 4);
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("Workspace Root"));
    QCOMPARE(tree.topLevelItem(1)->text(0), QStringLiteral("folder2"));
    QCOMPARE(tree.topLevelItem(2)->text(0),
             QDir::toNativeSeparators(QStringLiteral("folder2/nested")));
    QCOMPARE(tree.topLevelItem(3)->text(0), QStringLiteral("folder10"));
    QCOMPARE(tree.topLevelItem(1)->child(0)->data(0, Qt::UserRole).toString(),
             directory.filePath("folder2/2-note.md"));
    QVERIFY(!itemForPath(tree, directory.filePath("empty")));
    tree.setCurrentItem(tree.topLevelItem(1));
    QSignalSpy open(&tree, &FileTree::entryActivated);
    tree.show();
    QTest::keyClick(&tree, Qt::Key_Return);
    QVERIFY(!tree.currentItem()->isExpanded());
    QCOMPARE(open.count(), 0);
    QScopedPointer<QMenu> context(tree.createContextMenu());
    QVERIFY(!context->findChild<QAction*>("fileDuplicate"));
    context->findChild<QAction*>("fileCopyRelativePath")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("folder2"));
    const auto target = directory.filePath("folder2/2-note.md");
    tree.selectPath(target);
    QVERIFY(tree.currentItem()->parent()->isExpanded());
    QTest::keyClick(&tree, Qt::Key_Return);
    QCOMPARE(open.count(), 1);
    QCOMPARE(open.first().first().toString(), target);
    options.descending = true;
    tree.setOptions(options);
    QCOMPARE(tree.selectedPath(), target);
    QCOMPARE(tree.topLevelItem(1)->child(0)->data(0, Qt::UserRole).toString(),
             directory.filePath("folder2/10-note.md"));
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("Workspace Root"));
    QCOMPARE(tree.topLevelItem(3)->text(0), QStringLiteral("folder10"));
    auto* menu = tree.createOptionsMenu(&tree);
    QVERIFY(menu->findChild<QAction*>("browserGroupByFolder")->isChecked());
    menu->findChild<QAction*>("browserTree")->trigger();
    QVERIFY(!menu->findChild<QAction*>("browserGroupByFolder")->isEnabled());
    QVERIFY(menu->findChild<QAction*>("browserGroupByFolder")->isChecked());
    menu->findChild<QAction*>("browserList")->trigger();
    QVERIFY(menu->findChild<QAction*>("browserGroupByFolder")->isEnabled());
    QCOMPARE(tree.selectedPath(), target);
  }

  void groupAndTreeExpansionAreIndependentAcrossRefresh() {
    QTemporaryDir directory, other;
    const auto path = directory.filePath("a/b/note.md");
    QVERIFY(writeFile(path));
    QVERIFY(writeFile(directory.filePath("root.md")));
    FileTree tree;
    tree.setRoot(directory.path());
    itemForPath(tree, directory.filePath("a"))->setExpanded(false);
    auto options = tree.options();
    options.view = FileBrowserView::List;
    options.groupByFolder = true;
    tree.setOptions(options);
    auto* group = itemForPath(tree, directory.filePath("a/b"));
    QVERIFY(group && group->isExpanded());
    group->setExpanded(false);
    options.descending = true;
    tree.setOptions(options);
    QVERIFY(!itemForPath(tree, directory.filePath("a/b"))->isExpanded());
    options.groupByFolder = false;
    tree.setOptions(options);
    QCOMPARE(tree.topLevelItemCount(), 2);
    options.groupByFolder = true;
    tree.setOptions(options);
    QVERIFY(!itemForPath(tree, directory.filePath("a/b"))->isExpanded());
    QVERIFY(writeFile(directory.filePath("a/b/added.md")));
    QVERIFY(writeFile(directory.filePath("new/note.md")));
    QVERIFY(tree.refreshIfChanged());
    QVERIFY(!itemForPath(tree, directory.filePath("a/b"))->isExpanded());
    QVERIFY(itemForPath(tree, directory.filePath("new"))->isExpanded());
    options.view = FileBrowserView::Tree;
    tree.setOptions(options);
    QVERIFY(!itemForPath(tree, directory.filePath("a"))->isExpanded());
    options.view = FileBrowserView::List;
    tree.setOptions(options);
    QVERIFY(!itemForPath(tree, directory.filePath("a/b"))->isExpanded());
    QVERIFY(QFile::remove(path));
    QVERIFY(QFile::remove(directory.filePath("a/b/added.md")));
    QVERIFY(tree.refreshIfChanged());
    QVERIFY(!itemForPath(tree, directory.filePath("a/b")));
    QVERIFY(writeFile(directory.filePath("a/b/recreated.md")));
    QVERIFY(tree.refreshIfChanged());
    QVERIFY(itemForPath(tree, directory.filePath("a/b"))->isExpanded());
    itemForPath(tree, directory.filePath("a/b"))->setExpanded(false);
    QVERIFY(writeFile(other.filePath("a/b/note.md")));
    tree.setRoot(other.path());
    QVERIFY(itemForPath(tree, other.filePath("a/b"))->isExpanded());
  }

  void fileBrowserPreferencesKeepLegacyDefaults() {
    QSettings settings(QStringLiteral("Typist"), QStringLiteral("Typist"));
    const QStringList keys = {
        QStringLiteral("files/view"),           QStringLiteral("files/sort"),
        QStringLiteral("files/descending"),     QStringLiteral("files/foldersFirst"),
        QStringLiteral("files/groupByFolder"),  QStringLiteral("files/showHidden"),
        QStringLiteral("files/showNonMarkdown")};
    QMap<QString, QVariant> previous;
    for (const auto& key : keys) previous.insert(key, settings.value(key));
    const auto restore = qScopeGuard([&] {
      for (const auto& key : keys) {
        if (previous.value(key).isValid())
          settings.setValue(key, previous.value(key));
        else
          settings.remove(key);
      }
      settings.sync();
    });
    for (const auto& key : keys) settings.remove(key);
    settings.sync();
    const auto defaults = typist::platform::fileBrowserOptions();
    QCOMPARE(defaults.sort, FileBrowserSort::Name);
    QCOMPARE(defaults.view, FileBrowserView::Tree);
    QVERIFY(!defaults.groupByFolder);
    QVERIFY(!defaults.showHidden);
    QVERIFY(!defaults.showNonMarkdown);
    settings.setValue(QStringLiteral("files/sort"), QStringLiteral("unknown"));
    settings.sync();
    QCOMPARE(typist::platform::fileBrowserOptions().sort, FileBrowserSort::Name);
    FileBrowserOptions options;
    options.view = FileBrowserView::List;
    options.sort = FileBrowserSort::Created;
    options.descending = options.groupByFolder = options.foldersFirst = true;
    options.showHidden = true;
    options.showNonMarkdown = true;
    typist::platform::setFileBrowserOptions(options);
    const auto restored = typist::platform::fileBrowserOptions();
    QCOMPARE(restored.view, options.view);
    QCOMPARE(restored.sort, options.sort);
    QVERIFY(restored.groupByFolder && restored.descending && restored.foldersFirst);
    QVERIFY(restored.showHidden);
    QVERIFY(restored.showNonMarkdown);
  }

  void createdSortUsesBirthTimeAndRefreshesMetadataOnEntry() {
    QTemporaryDir directory;
    const auto oldPath = directory.filePath("z-old.md"), newPath = directory.filePath("a-new.md");
    QVERIFY(writeFile(oldPath, "old", 1800000000));
    QTest::qWait(30);
    QVERIFY(writeFile(newPath, "new", 1600000000));
    // Where the OS permits it, use deterministic birth dates; other systems retain real dates.
    for (const auto& path : {oldPath, newPath}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::ReadWrite));
      file.setFileTime(QDateTime::fromSecsSinceEpoch(path == oldPath ? 1600000000 : 1700000000),
                       QFileDevice::FileBirthTime);
    }
    FileTree tree;
    tree.setRoot(directory.path());
    auto options = tree.options();
    options.sort = FileBrowserSort::Created;
    tree.setOptions(options);
    const QFileInfo oldInfo(oldPath), newInfo(newPath);
    const auto oldDate =
        oldInfo.birthTime().isValid() ? oldInfo.birthTime() : oldInfo.lastModified();
    const auto newDate =
        newInfo.birthTime().isValid() ? newInfo.birthTime() : newInfo.lastModified();
    const bool oldFirst = oldDate < newDate || (oldDate == newDate && oldPath < newPath);
    QCOMPARE(tree.entryPath(0), oldFirst ? oldPath : newPath);
    const auto tip = itemForPath(tree, oldPath)->toolTip(0);
    QVERIFY(tip.contains(oldInfo.birthTime().isValid()
                             ? QStringLiteral("Created:")
                             : QStringLiteral("Creation time unavailable.")));
    options.descending = true;
    tree.setOptions(options);
    QCOMPARE(tree.entryPath(1), oldFirst ? oldPath : newPath);
    options.sort = FileBrowserSort::Name;
    options.descending = false;
    tree.setOptions(options);
    QVERIFY(writeFile(newPath, "NEW", 1900000000));
    options.sort = FileBrowserSort::Modified;
    tree.setOptions(options);
    QCOMPARE(tree.entryPath(0), oldPath);  // Must see the edit made while sorting by name.
  }

#ifdef Q_OS_UNIX
  void selectedFileResolvesDirectoryAliases() {
    QTemporaryDir directory;
    const auto root = directory.filePath("notes");
    const auto path = QDir(root).filePath("note.md");
    const auto alias = directory.filePath("alias");
    QVERIFY(writeFile(path));
    QVERIFY(QFile::link(root, alias));
    FileTree tree;
    tree.setRoot(QFileInfo(root).canonicalFilePath());
    tree.selectPath(QDir(alias).filePath("note.md"));
    QCOMPARE(QFileInfo(tree.selectedPath()).canonicalFilePath(),
             QFileInfo(path).canonicalFilePath());
    QScopedPointer<QMenu> menu(tree.createContextMenu());
    QVERIFY(menu->findChild<QAction*>("fileDuplicate"));
    tree.setRoot(alias);
    tree.selectPath(QFileInfo(path).canonicalFilePath());
    QCOMPARE(tree.selectedPath(), QDir(alias).filePath("note.md"));
    QCOMPARE(tree.rootPath(), alias);
    menu.reset(tree.createContextMenu());
    QVERIFY(menu->findChild<QAction*>("fileDuplicate"));
  }
#endif

  void fileOperationsPreserveBytesAndNeverOverwrite() {
    using namespace typist::platform;
    QTemporaryDir directory;
    const auto source = directory.filePath(QStringLiteral("中文 note.MD"));
    const auto existing = directory.filePath(QStringLiteral("中文 note-copy.MD"));
    const auto bytes = QByteArray::fromHex("efbbbf") +
                       QStringLiteral("# 计划\r\n\r\n![图](assets/p.png)\r\n").toUtf8();
    QVERIFY(writeFile(source, bytes));
    QVERIFY(writeFile(existing, "keep"));
    QString error = QStringLiteral("old");
    const auto copy = duplicateMarkdownFile(source, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(copy, directory.filePath(QStringLiteral("中文 note-copy-2.MD")));
    for (const auto& path : {source, copy, existing}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), path == existing ? QByteArray("keep") : bytes);
    }
    QCOMPARE(duplicateMarkdownFile(source, &error),
             directory.filePath(QStringLiteral("中文 note-copy-3.MD")));
    QVERIFY(createDirectory(directory.path(), QStringLiteral("子目录"), &error));
    QVERIFY(!createDirectory(directory.path(), QStringLiteral("子目录"), &error));
    QVERIFY(!createDirectory(directory.path(), QStringLiteral("中文 note.MD"), &error));
    QVERIFY(!createMarkdownFile(directory.path(), QStringLiteral("中文 note.MD"), &error));
    QVERIFY(!renameFile(copy, QStringLiteral("中文 note.MD"), &error));
    QCOMPARE(containingDirectoryUrl(source), QUrl::fromLocalFile(directory.path()));
    QCOMPARE(containingDirectoryUrl(directory.filePath(QStringLiteral("子目录"))),
             QUrl::fromLocalFile(directory.filePath(QStringLiteral("子目录"))));
    QVERIFY(containingDirectoryUrl(directory.filePath("missing.md")).isEmpty());
    QVERIFY(duplicateMarkdownFile(directory.path(), &error).isEmpty());
    QVERIFY(duplicateMarkdownFile(directory.filePath("missing.md"), &error).isEmpty());
#ifdef Q_OS_UNIX
    const auto alias = directory.filePath("alias.md");
    QVERIFY(QFile::link(source, alias));
    QVERIFY(duplicateMarkdownFile(alias, &error).isEmpty());
#endif
  }

  void namesStayInsideTheirParent() {
    using namespace typist::platform;
    QTemporaryDir directory;
    const auto parent = directory.filePath("notes");
    QVERIFY(QDir().mkdir(parent));
    const auto source = QDir(parent).filePath("original.md");
    QVERIFY(writeFile(source));
    for (const auto& name :
         {QString(), QStringLiteral("."), QStringLiteral(".."), QStringLiteral("../escape"),
          QStringLiteral("sub/note"), QStringLiteral("sub\\note"), QStringLiteral("CON"),
          QStringLiteral("com1.md"), QStringLiteral("LPT9"), QStringLiteral("bad:name"),
          QStringLiteral("trailing."), QStringLiteral("bad\nname")}) {
      QString error;
      QVERIFY(!createDirectory(parent, name, &error));
      QVERIFY(!error.isEmpty());
      QVERIFY(!createMarkdownFile(parent, name, &error));
      QVERIFY(!renameFile(source, name, &error));
      QVERIFY(QFileInfo::exists(source));
    }
    QVERIFY(!QFileInfo::exists(directory.filePath("escape.md")));
    QString error;
    QVERIFY(!createDirectory({}, QStringLiteral("folder"), &error));
    QVERIFY(!createMarkdownFile(directory.filePath("missing"), QStringLiteral("note"), &error));
    QCOMPARE(QDir(parent).entryList(QDir::Files), QStringList({QStringLiteral("original.md")}));
  }

  void contextActionsUseSnapshotAndDoNotOpenCopies() {
    QTemporaryDir directory;
    const auto path = directory.filePath("sub/note.md");
    const auto other = directory.filePath("other.md");
    QVERIFY(writeFile(path));
    QVERIFY(writeFile(other));
    FileTree tree;
    tree.setRoot(directory.path());
    tree.selectPath(path);
    QSignalSpy opened(&tree, &FileTree::entryActivated);
    QSignalSpy newWindow(&tree, &FileTree::entryOpenInNewWindow);
    QScopedPointer<QMenu> menu(tree.createContextMenu());
    tree.selectPath(other);  // A live selection change must not redirect a menu action.
    auto* openWindow = menu->findChild<QAction*>("fileOpenInNewWindow");
    QVERIFY(openWindow && openWindow->isEnabled());
    openWindow->trigger();
    QCOMPARE(newWindow.count(), 1);
    QCOMPARE(newWindow.takeFirst().first().toString(), path);
    QCOMPARE(tree.selectedPath(), other);
    menu->findChild<QAction*>("fileCopyPath")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(path));
    menu->findChild<QAction*>("fileCopyRelativePath")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sub/note.md"));
    menu->findChild<QAction*>("fileDuplicate")->trigger();
    QCOMPARE(tree.selectedPath(), directory.filePath("sub/note-copy.md"));
    QCOMPARE(tree.entryCount(), 3);
    QCOMPARE(opened.count(), 0);
    auto options = tree.options();
    options.view = FileBrowserView::List;
    tree.setOptions(options);
    tree.selectPath(path);
    menu.reset(tree.createContextMenu());
    menu->findChild<QAction*>("fileDuplicate")->trigger();
    QCOMPARE(tree.selectedPath(), directory.filePath("sub/note-copy-2.md"));
    QCOMPARE(opened.count(), 0);
    tree.setCurrentItem(nullptr);
    menu.reset(tree.createContextMenu());
    QVERIFY(!menu->findChild<QAction*>("fileDuplicate"));
    QVERIFY(!menu->findChild<QAction*>("fileMoveToTrash"));
    openWindow = menu->findChild<QAction*>("fileOpenInNewWindow");
    QVERIFY(openWindow && openWindow->isEnabled());
    openWindow->trigger();
    QCOMPARE(newWindow.count(), 1);
    QCOMPARE(newWindow.takeFirst().first().toString(), directory.path());
    QCOMPARE(opened.count(), 0);
    menu->findChild<QAction*>("fileCopyRelativePath")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("."));
    tree.setRoot({});
    menu.reset(tree.createContextMenu());
    QVERIFY(!menu->findChild<QAction*>("fileNewFolder")->isEnabled());
    QVERIFY(!menu->findChild<QAction*>("fileCopyPath")->isEnabled());
    QVERIFY(!menu->findChild<QAction*>("fileReveal")->isEnabled());
  }

  void fileManagerMenuOpensTheChosenFolderAndHandlesMissingFiles() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto folder = directory.filePath(QStringLiteral("中文 folder"));
    const auto path = QDir(folder).filePath(QStringLiteral("note #1.md"));
    QVERIFY(writeFile(path));
    FileTree tree;
    tree.setRoot(directory.path());
    tree.selectPath(path);
    QScopedPointer<QMenu> fileMenu(tree.createContextMenu());
    auto* fileAction = fileMenu->findChild<QAction*>("fileReveal");
    QVERIFY(fileAction && fileAction->isEnabled());
    QCOMPARE(fileAction->text(), QStringLiteral("Reveal in File Manager"));

    auto* folderItem = itemForPath(tree, folder);
    QVERIFY(folderItem);
    tree.setCurrentItem(folderItem);
    QScopedPointer<QMenu> folderMenu(tree.createContextMenu());
    tree.setCurrentItem(nullptr);
    QScopedPointer<QMenu> rootMenu(tree.createContextMenu());
    FolderReceiver receiver;
    QDesktopServices::setUrlHandler("file", &receiver, "capture");
    auto* folderAction = folderMenu->findChild<QAction*>("fileReveal");
    QVERIFY(folderAction && folderAction->isEnabled());
    QCOMPARE(folderAction->text(), QStringLiteral("Open Folder"));
    folderAction->trigger();
    QCOMPARE(receiver.last, QUrl::fromLocalFile(folder));
    auto* rootAction = rootMenu->findChild<QAction*>("fileReveal");
    QVERIFY(rootAction && rootAction->isEnabled());
    rootAction->trigger();
    QCOMPARE(receiver.last, QUrl::fromLocalFile(directory.path()));

    QVERIFY(QFile::remove(path));
    bool showedError = false;
    QTimer::singleShot(0, &tree, [&] {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        showedError = box->text() == QStringLiteral("Cannot show the file in the file manager.");
        box->accept();
      }
    });
    receiver.last.clear();
    fileAction->trigger();
    QVERIFY(showedError);
    QVERIFY(receiver.last.isEmpty());
  }

  void trashRejectsUnavailablePathsAndDirectories() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto folder = directory.filePath("folder.md");
    const auto text = directory.filePath("text.txt");
    QVERIFY(QDir().mkpath(folder));
    QVERIFY(writeFile(text));
    QString error;
    for (const auto& path : {QString(), directory.filePath("missing.md"), folder, text}) {
      QVERIFY(!typist::platform::moveMarkdownToTrash(path, &error));
      QCOMPARE(error, QStringLiteral("File unavailable"));
    }
    QVERIFY(QFileInfo(folder).isDir());
    QVERIFY(QFileInfo(text).isFile());
  }

  void cancelTrashKeepsTheFileAndSelection() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("测试 & note.MD"));
    QVERIFY(writeFile(path));
    FileTree tree;
    tree.setRoot(directory.path());
    tree.selectPath(path);
    QSignalSpy removed(&tree, &FileTree::fileRemoved);
    QScopedPointer<QMenu> menu(tree.createContextMenu());
    auto* action = menu->findChild<QAction*>("fileMoveToTrash");
    QVERIFY(action && action->isEnabled());
    bool confirmedDefault = false;
    QTimer::singleShot(0, &tree, [&] {
      if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        confirmedDefault = box->textFormat() == Qt::PlainText &&
                           box->text().contains(QFileInfo(path).fileName()) &&
                           box->defaultButton() == box->escapeButton();
        QTest::keyClick(box, Qt::Key_Escape);
      }
    });
    action->trigger();
    QVERIFY(confirmedDefault);
    QVERIFY(QFileInfo(path).isFile());
    QCOMPARE(tree.selectedPath(), path);
    QCOMPARE(removed.count(), 0);
  }

  void newFolderInSelectedDirectoryAndCancel() {
    QTemporaryDir directory;
    QVERIFY(writeFile(directory.filePath("sub/note.md")));
    FileTree tree;
    tree.setRoot(directory.path());
    tree.show();
    auto* parent = itemForPath(tree, directory.filePath("sub"));
    parent->setExpanded(false);
    tree.setCurrentItem(parent);
    QScopedPointer<QMenu> menu(tree.createContextMenu());
    QVERIFY(!menu->findChild<QAction*>("fileDuplicate"));
    QSignalSpy opened(&tree, &FileTree::entryActivated);
    QTimer safety;
    connect(&safety, &QTimer::timeout, &tree, [] {
      if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
    });
    safety.start(3000);
    bool prompted = false;
    QTimer::singleShot(0, &tree, [&] {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
        prompted = true;
        auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameInput"));
        QVERIFY(input);
        input->setText(QStringLiteral("新文件夹"));
        QTest::keyClick(input, Qt::Key_Return);
      }
    });
    menu->findChild<QAction*>("fileNewFolder")->trigger();
    QVERIFY(prompted);
    const auto path = directory.filePath(QStringLiteral("sub/新文件夹"));
    QVERIFY(QFileInfo(path).isDir());
    QVERIFY(itemForPath(tree, path));
    QCOMPARE(tree.currentItem(), itemForPath(tree, path));
    QVERIFY(itemForPath(tree, directory.filePath("sub"))->isExpanded());
    QCOMPARE(opened.count(), 0);
    menu.reset(tree.createContextMenu());
    QTimer::singleShot(0, &tree, [] {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->reject();
    });
    menu->findChild<QAction*>("fileNewFolder")->trigger();
    QVERIFY(QDir(path).entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty());
  }

  void treeAndListIncludeAllDepthsAndSkipExcludedEntries() {
    QTemporaryDir directory;
    for (const auto& name : {QStringLiteral("root.md"), QStringLiteral("a/b/c/计划.MD"),
                             QStringLiteral("a/duplicate.md"), QStringLiteral("b/duplicate.md"),
                             QStringLiteral(".hidden.md"), QStringLiteral(".git/private.md"),
                             QStringLiteral("build/a.md"), QStringLiteral("node_modules/a.md"),
                             QStringLiteral("ignore.txt")})
      QVERIFY(writeFile(directory.filePath(name)));
    QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("empty"))));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::link(directory.path(), directory.filePath(QStringLiteral("loop"))));
    QVERIFY(QFile::link(directory.filePath("root.md"), directory.filePath("alias.md")));
#endif
    FileTree tree;
    tree.setRoot(directory.path());
    QCOMPARE(tree.entryCount(), 4);
    QCOMPARE(tree.directoryCount(), 5);
    const auto target = directory.filePath(QStringLiteral("a/b/c/计划.MD"));
    tree.selectPath(target);
    QCOMPARE(tree.selectedPath(), target);
    FileBrowserOptions options;
    options.view = FileBrowserView::List;
    tree.setOptions(options);
    QCOMPARE(tree.entryCount(), 4);
    QCOMPARE(tree.topLevelItemCount(), 4);
    QCOMPARE(tree.selectedPath(), target);
    QVERIFY(itemForPath(tree, target)
                ->text(0)
                .contains(QDir::toNativeSeparators(QStringLiteral("a/b/c"))));
    QVERIFY(itemForPath(tree, directory.filePath("a/duplicate.md"))->text(0) !=
            itemForPath(tree, directory.filePath("b/duplicate.md"))->text(0));
    for (int i = 0; i < tree.topLevelItemCount(); ++i)
      QCOMPARE(tree.topLevelItem(i)->childCount(), 0);
    options.view = FileBrowserView::Tree;
    tree.setOptions(options);
    QCOMPARE(tree.selectedPath(), target);
    QCOMPARE(itemForPath(tree, target)->parent()->text(0), QStringLiteral("c"));
  }

  void hiddenEntriesFollowSidebarFilterAndWatcher() {
    QTemporaryDir directory;
    const auto visible = directory.filePath("notes/current.md");
    const auto hiddenDir = directory.filePath(".drafts");
    const auto hiddenNote = directory.filePath(".drafts/nested/note.MD");
    for (const auto& path :
         {visible, hiddenNote, directory.filePath(".root.md"),
          directory.filePath(".drafts/readme.txt"), directory.filePath(".git/ignored.md"),
          directory.filePath(".drafts/build/ignored.md"),
          directory.filePath(".drafts/node_modules/ignored.md")})
      QVERIFY(writeFile(path));
    const auto empty = directory.filePath(".empty");
    QVERIFY(QDir().mkpath(empty));
    int shownCount = 3;
#ifdef Q_OS_WIN
    const auto nativeHidden = directory.filePath("native-hidden.md");
    QVERIFY(writeFile(nativeHidden));
    QVERIFY(
        SetFileAttributesW(reinterpret_cast<LPCWSTR>(nativeHidden.utf16()), FILE_ATTRIBUTE_HIDDEN));
    QVERIFY(QFileInfo(nativeHidden).isHidden());
    ++shownCount;
#endif
    FileTree tree;
    tree.setRoot(directory.path());
    tree.selectPath(visible);
    auto* notes = itemForPath(tree, directory.filePath("notes"));
    QVERIFY(notes);
    notes->setExpanded(false);
    tree.setWatchEnabled(true);
    auto* watcher = tree.findChild<QFileSystemWatcher*>();
    QVERIFY(watcher && !watcher->directories().contains(hiddenDir));
    QCOMPARE(tree.entryCount(), 1);
    QScopedPointer<QMenu> menu(tree.createOptionsMenu(nullptr));
    auto* showHidden = menu->findChild<QAction*>(QStringLiteral("browserShowHidden"));
    QVERIFY(showHidden && !showHidden->isChecked());
    showHidden->trigger();
    QVERIFY(showHidden->isChecked() && tree.options().showHidden);
    QCOMPARE(tree.entryCount(), shownCount);
    QCOMPARE(tree.selectedPath(), visible);
    QVERIFY(!itemForPath(tree, directory.filePath("notes"))->isExpanded());
    QVERIFY(itemForPath(tree, hiddenNote));
    QVERIFY(itemForPath(tree, empty));
#ifdef Q_OS_WIN
    QVERIFY(itemForPath(tree, nativeHidden));
#endif
    QVERIFY(watcher->directories().contains(hiddenDir));
    QVERIFY(!watcher->directories().contains(directory.filePath(".git")));

    auto options = tree.options();
    options.view = FileBrowserView::List;
    options.groupByFolder = true;
    tree.setOptions(options);
    const auto added = directory.filePath(".drafts/added.md");
    QVERIFY(writeFile(added));
    QTRY_COMPARE(tree.entryCount(), shownCount + 1);
    QVERIFY(itemForPath(tree, added));
    QVERIFY(!itemForPath(tree, empty));
    showHidden->trigger();
    QCOMPARE(tree.entryCount(), 1);
    QCOMPARE(tree.selectedPath(), visible);
    QVERIFY(!watcher->directories().contains(hiddenDir));
    QVERIFY(!itemForPath(tree, hiddenNote));
  }

  void nonMarkdownFilesCombineWithHiddenFilterAndKeepPathActions() {
    QTemporaryDir directory;
    const auto note = directory.filePath("notes/guide.MD");
    const auto asset = directory.filePath(QStringLiteral("assets/图 #1.png"));
    const auto hidden = directory.filePath(".drafts/preview.pdf");
    for (const auto& path : {note, asset, hidden, directory.filePath("README"),
                             directory.filePath("build/generated.pdf")})
      QVERIFY(writeFile(path));
    FileTree tree;
    tree.setRoot(directory.path());
    tree.selectPath(note);
    QCOMPARE(tree.entryCount(), 1);
    QScopedPointer<QMenu> options(tree.createOptionsMenu(nullptr));
    auto* showNonMarkdown = options->findChild<QAction*>("browserShowNonMarkdown");
    auto* showHidden = options->findChild<QAction*>("browserShowHidden");
    QVERIFY(showNonMarkdown && showHidden && !showNonMarkdown->isChecked());
    showNonMarkdown->trigger();
    QCOMPARE(tree.entryCount(), 3);
    QCOMPARE(tree.selectedPath(), note);
    QVERIFY(itemForPath(tree, asset));
    QVERIFY(!itemForPath(tree, hidden));
    showHidden->trigger();
    QCOMPARE(tree.entryCount(), 4);
    QVERIFY(itemForPath(tree, hidden));
    options->findChild<QAction*>("browserList")->trigger();
    options->findChild<QAction*>("browserGroupByFolder")->trigger();
    QCOMPARE(tree.entryCount(), 4);
    tree.selectPath(asset);
    QScopedPointer<QMenu> menu(tree.createContextMenu());
    auto* open = menu->findChild<QAction*>("fileOpenExternally");
    QVERIFY(open && open->isEnabled());
    QVERIFY(!menu->findChild<QAction*>("fileOpenInNewWindow"));
    QVERIFY(!menu->findChild<QAction*>("fileDuplicate"));
    QVERIFY(!menu->findChild<QAction*>("fileRename"));
    QVERIFY(!menu->findChild<QAction*>("fileMoveToTrash"));
    QVERIFY(menu->findChild<QAction*>("fileReveal")->isEnabled());
    menu->findChild<QAction*>("fileCopyRelativePath")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("assets/图 #1.png"));
    QSignalSpy activated(&tree, &FileTree::entryActivated);
    open->trigger();
    QTest::keyClick(&tree, Qt::Key_Return);
    QCOMPARE(activated.size(), 2);
    QCOMPARE(activated.at(0).at(0).toString(), asset);
    QCOMPARE(activated.at(1).at(0).toString(), asset);
    showNonMarkdown->trigger();
    QCOMPARE(tree.entryCount(), 1);
    QVERIFY(showHidden->isChecked());
    QVERIFY(tree.selectedPath().isEmpty());
    QVERIFY(!itemForPath(tree, asset));
  }

  void nameNaturalDescendingAndFoldersFirst() {
    QTemporaryDir directory;
    for (const auto& name : {QStringLiteral("10-note.md"), QStringLiteral("2-note.md"),
                             QStringLiteral("1-note.md"), QStringLiteral("folder/deep.md")})
      QVERIFY(writeFile(directory.filePath(name)));
    FileTree tree;
    tree.setRoot(directory.path());
    QCOMPARE(filenames(tree), QStringList({"1-note.md", "10-note.md", "2-note.md", "deep.md"}));
    FileBrowserOptions options;
    options.sort = FileBrowserSort::Natural;
    tree.setOptions(options);
    QCOMPARE(filenames(tree), QStringList({"1-note.md", "2-note.md", "10-note.md", "deep.md"}));
    options.descending = true;
    tree.setOptions(options);
    QCOMPARE(filenames(tree), QStringList({"10-note.md", "2-note.md", "1-note.md", "deep.md"}));
    options.foldersFirst = true;
    tree.setOptions(options);
    QCOMPARE(filenames(tree).first(), QStringLiteral("deep.md"));
    options.view = FileBrowserView::List;
    tree.setOptions(options);
    QCOMPARE(filenames(tree), QStringList({"deep.md", "10-note.md", "2-note.md", "1-note.md"}));
  }

  void modifiedSortAndRefreshReorderSameSizeEdits() {
    QTemporaryDir directory;
    const auto older = directory.filePath("old.md"), newer = directory.filePath("new.md");
    QVERIFY(writeFile(older, "old", 1600000000));
    QVERIFY(writeFile(newer, "new", 1700000000));
    FileTree tree;
    FileBrowserOptions options;
    options.view = FileBrowserView::List;
    options.sort = FileBrowserSort::Modified;
    options.descending = true;
    tree.setOptions(options);
    tree.setRoot(directory.path());
    tree.selectPath(older);
    QCOMPARE(tree.entryPath(0), newer);
    QVERIFY(writeFile(older, "NEW", 1800000000));
    QVERIFY(tree.refreshIfChanged());
    QCOMPARE(tree.entryPath(0), older);
    QCOMPARE(tree.selectedPath(), older);
    QVERIFY(!tree.refreshIfChanged());
  }

  void expansionSurvivesSortListAndRefresh() {
    QTemporaryDir directory, other;
    QVERIFY(writeFile(directory.filePath("a/b/c/deep.md")));
    QVERIFY(writeFile(directory.filePath("a/b/c/d/last.md")));
    QVERIFY(writeFile(other.filePath("a/b/c/other.md")));
    FileTree tree;
    tree.setRoot(directory.path());
    auto* collapsed = itemForPath(tree, directory.filePath("a/b/c"));
    QVERIFY(collapsed && collapsed->isExpanded());
    collapsed->setExpanded(false);
    auto options = tree.options();
    options.sort = FileBrowserSort::Natural;
    tree.setOptions(options);
    QVERIFY(!itemForPath(tree, directory.filePath("a/b/c"))->isExpanded());
    options.view = FileBrowserView::List;
    tree.setOptions(options);
    QVERIFY(writeFile(directory.filePath("a/b/c/added.md")));
    QVERIFY(tree.refreshIfChanged());
    options.view = FileBrowserView::Tree;
    tree.setOptions(options);
    QVERIFY(!itemForPath(tree, directory.filePath("a/b/c"))->isExpanded());
    tree.setRoot(other.path());
    QVERIFY(itemForPath(tree, other.filePath("a/b/c"))->isExpanded());
  }

  void emptyDirectoriesAndDeletedSelectionRefresh() {
    QTemporaryDir directory;
    const auto path = directory.filePath("note.md");
    QVERIFY(writeFile(path));
    FileTree tree;
    tree.setRoot(directory.path());
    tree.selectPath(path);
    QVERIFY(QDir().mkpath(directory.filePath("empty/nested")));
    QVERIFY(tree.refreshIfChanged());
    QCOMPARE(tree.directoryCount(), 2);
    QCOMPARE(tree.selectedPath(), path);
    QVERIFY(QFile::remove(path));
    QVERIFY(tree.refreshIfChanged());
    QCOMPARE(tree.entryCount(), 0);
    QVERIFY(tree.selectedPath().isEmpty());
    tree.setRoot({});
    QCOMPARE(tree.topLevelItemCount(), 0);
    QCOMPARE(tree.directoryCount(), 0);
  }

  void menuAndKeyboard() {
    QTemporaryDir directory;
    const auto target = directory.filePath("note.md");
    QVERIFY(writeFile(target));
    FileTree tree;
    tree.setRoot(directory.path());
    tree.selectPath(target);
    auto* menu = tree.createOptionsMenu(&tree);
    QSignalSpy changes(&tree, &FileTree::optionsChanged);
    menu->findChild<QAction*>("browserList")->trigger();
    QCOMPARE(tree.options().view, FileBrowserView::List);
    QVERIFY(!menu->findChild<QAction*>("browserFoldersFirst")->isEnabled());
    menu->findChild<QAction*>("browserNatural")->trigger();
    QCOMPARE(tree.options().sort, FileBrowserSort::Natural);
    menu->findChild<QAction*>("browserDescending")->trigger();
    QVERIFY(tree.options().descending);
    QCOMPARE(changes.count(), 3);
    QCOMPARE(tree.selectedPath(), target);
    QSignalSpy open(&tree, &FileTree::entryActivated);
    tree.show();
    QTest::keyClick(&tree, Qt::Key_Return);
    QCOMPARE(open.count(), 1);
    QCOMPARE(open.at(0).at(0).toString(), target);
    QVERIFY(writeFile(directory.filePath("new.md")));
    menu->findChild<QAction*>("browserRefresh")->trigger();
    QCOMPARE(tree.entryCount(), 2);
  }

  void watcherUsesActualFoldersInListAndDropsOldRoot_data() {
    QTest::addColumn<bool>("grouped");
    QTest::newRow("flat") << false;
    QTest::newRow("grouped") << true;
  }
  void watcherUsesActualFoldersInListAndDropsOldRoot() {
    QFETCH(bool, grouped);
    QTemporaryDir directory, other;
    QVERIFY(writeFile(directory.filePath("a/b/note.md"), "old", 1600000000));
    FileTree tree;
    FileBrowserOptions options;
    options.view = FileBrowserView::List;
    options.sort = FileBrowserSort::Modified;
    options.groupByFolder = grouped;
    tree.setOptions(options);
    tree.setRoot(directory.path());
    tree.setWatchEnabled(true);
    auto* watcher = tree.findChild<QFileSystemWatcher*>();
    QVERIFY(watcher);
    QVERIFY(watcher->directories().contains(directory.filePath("a/b")));
    QVERIFY(watcher->files().contains(directory.filePath("a/b/note.md")));
    QVERIFY(writeFile(directory.filePath("a/b/added.md"), "new", 1700000000));
    QTRY_COMPARE(tree.entryCount(), 2);
    QCOMPARE(tree.entryPath(0), directory.filePath("a/b/note.md"));
    QVERIFY(writeFile(directory.filePath("a/b/note.md"), "NEW", 1800000000));
    QTRY_COMPARE(tree.entryPath(0), directory.filePath("a/b/added.md"));
    tree.setRoot(other.path());
    QCOMPARE(watcher->directories(), QStringList({other.path()}));
    QVERIFY(watcher->files().isEmpty());
    tree.setWatchEnabled(false);
    QVERIFY(watcher->directories().isEmpty());
    QVERIFY(writeFile(other.filePath("while-hidden.md")));
    tree.setWatchEnabled(true);
    QCOMPARE(tree.entryCount(), 1);
    QCOMPARE(tree.entryPath(0), other.filePath("while-hidden.md"));
  }

  void atomicSaveRestoresWatchWhenMetadataUnchanged_data() {
    QTest::addColumn<bool>("created");
    QTest::newRow("modified") << false;
    QTest::newRow("created") << true;
  }
  void atomicSaveRestoresWatchWhenMetadataUnchanged() {
    QFETCH(bool, created);
    QTemporaryDir directory;
    const auto path = directory.filePath("note.md");
    QVERIFY(writeFile(path, "old", 1600000000));
    FileTree tree;
    FileBrowserOptions options;
    options.sort = created ? FileBrowserSort::Created : FileBrowserSort::Modified;
    options.view = FileBrowserView::List;
    options.groupByFolder = true;
    tree.setOptions(options);
    tree.setRoot(directory.path());
    tree.setWatchEnabled(true);
    auto* watcher = tree.findChild<QFileSystemWatcher*>();
    QVERIFY(watcher && watcher->files().contains(path));
    QSignalSpy refreshed(&tree, &FileTree::directoryChanged);
    QSaveFile save(path);
    QVERIFY(save.open(QIODevice::WriteOnly));
    QCOMPARE(save.write("NEW"), qint64(3));
    QVERIFY(save.flush());
    QVERIFY(save.setFileTime(QDateTime::fromSecsSinceEpoch(1600000000),
                             QFileDevice::FileModificationTime));
    QVERIFY(save.commit());
    QTRY_VERIFY(refreshed.count() > 0);
    QVERIFY(watcher->files().contains(path));
    QVERIFY(writeFile(path, "more", 1800000000));
    QTRY_VERIFY(refreshed.count() > 1);
  }
};

QTEST_MAIN(FileBrowserTests)
#include "file_browser_tests.moc"
