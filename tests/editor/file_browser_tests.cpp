#include "FileTree.h"
#include "Settings.h"

#include <QAction>
#include <QDateTime>
#include <QFile>
#include <QFileSystemWatcher>
#include <QMenu>
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

class FileBrowserTests : public QObject {
  Q_OBJECT
 private slots:
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
  }
};

QTEST_MAIN(FileBrowserTests)
#include "file_browser_tests.moc"
