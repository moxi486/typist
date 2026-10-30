#include "FileOps.h"
#include "FileTree.h"
#include "Settings.h"

#include <QAction>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QFileSystemWatcher>
#include <QInputDialog>
#include <QMenu>
#include <QSaveFile>
#include <QScopedPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidgetItemIterator>
#include <QtTest>

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
    QScopedPointer<QMenu> menu(tree.createContextMenu());
    tree.selectPath(other);  // A live selection change must not redirect a menu action.
    menu->findChild<QAction*>("fileCopyPath")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(path));
    menu->findChild<QAction*>("fileCopyRelativePath")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("sub/note.md"));
    FolderReceiver receiver;
    QDesktopServices::setUrlHandler("file", &receiver, "capture");
    menu->findChild<QAction*>("fileReveal")->trigger();
    QCOMPARE(receiver.last, QUrl::fromLocalFile(directory.filePath("sub")));
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
    QVERIFY(!menu->findChild<QAction*>("fileDelete"));
    menu->findChild<QAction*>("fileCopyRelativePath")->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("."));
    tree.setRoot({});
    menu.reset(tree.createContextMenu());
    QVERIFY(!menu->findChild<QAction*>("fileNewFolder")->isEnabled());
    QVERIFY(!menu->findChild<QAction*>("fileCopyPath")->isEnabled());
    QVERIFY(!menu->findChild<QAction*>("fileReveal")->isEnabled());
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
      if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget())) {
        prompted = true;
        dialog->setTextValue(QStringLiteral("新文件夹"));
        dialog->accept();
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
      if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))
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

  void watcherUsesActualFoldersInListAndDropsOldRoot() {
    QTemporaryDir directory, other;
    QVERIFY(writeFile(directory.filePath("a/b/note.md"), "old", 1600000000));
    FileTree tree;
    FileBrowserOptions options;
    options.view = FileBrowserView::List;
    options.sort = FileBrowserSort::Modified;
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

  void atomicSaveRestoresWatchWhenMetadataUnchanged() {
    QTemporaryDir directory;
    const auto path = directory.filePath("note.md");
    QVERIFY(writeFile(path, "old", 1600000000));
    FileTree tree;
    FileBrowserOptions options;
    options.sort = FileBrowserSort::Modified;
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
