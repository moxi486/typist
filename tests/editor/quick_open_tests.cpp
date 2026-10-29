#include "QuickOpen.h"
#include "QuickOpenDialog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool write(const QString& path) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write("# Note\n") == 7;
}
}  // namespace

class QuickOpenTests : public QObject {
  Q_OBJECT
 private slots:
  void rankingAndRecents() {
    const QString root = QDir::tempPath();
    const QDir dir(root);
    const auto exact = dir.filePath(QStringLiteral("notes.md"));
    const auto prefix = dir.filePath(QStringLiteral("notes-today.md"));
    const auto fuzzy = dir.filePath(QStringLiteral("new-other-topics-example-summary.md"));
    const auto path = dir.filePath(QStringLiteral("notes/plan.md"));
    const auto chinese = dir.filePath(QStringLiteral("工作/项目计划.MD"));
    const QStringList files{path, fuzzy, prefix, exact, chinese};
    auto results = typist::platform::quickOpenMatches(files, {path}, root, QStringLiteral("NOTES"));
    QCOMPARE(results, QStringList({exact, prefix, fuzzy, path}));
    QCOMPARE(
        typist::platform::quickOpenMatches(files, {}, root, QStringLiteral("notes.md")).first(),
        exact);
    QCOMPARE(typist::platform::quickOpenMatches(files, {}, root, QStringLiteral("工作 计划")),
             QStringList({chinese}));
    QCOMPARE(typist::platform::quickOpenMatches(files, {}, root, QStringLiteral("notes/pla")),
             QStringList({path}));
    results = typist::platform::quickOpenMatches(files, {path, path, exact}, root, {});
    QCOMPARE(results.size(), files.size());
    QCOMPARE(results.at(0), path);
    QCOMPARE(results.at(1), exact);
    QCOMPARE(typist::platform::quickOpenMatches(files, {}, root, {}, 2).size(), 2);
    QVERIFY(
        typist::platform::quickOpenMatches(files, {}, root, QStringLiteral("missing")).isEmpty());
  }

  void scanPrunesDirectoriesAndHandlesLimits() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    for (const auto& name :
         {QStringLiteral("root.md"), QStringLiteral("docs/计划.MD"), QStringLiteral(".hidden.md"),
          QStringLiteral(".git/a.md"), QStringLiteral("build/deep/a.md"),
          QStringLiteral("node_modules/a.md"), QStringLiteral("docs/other.txt")})
      QVERIFY(write(directory.filePath(name)));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::link(directory.filePath(QStringLiteral("root.md")),
                        directory.filePath(QStringLiteral("alias.md"))));
    QVERIFY(QFile::link(directory.path(), directory.filePath(QStringLiteral("loop"))));
#endif
    std::atomic<bool> cancelled{false};
    auto result = typist::platform::indexMarkdownFiles(directory.path(), cancelled);
    QCOMPARE(result.paths.size(), 2);
    QVERIFY(result.paths.contains(directory.filePath(QStringLiteral("docs/计划.MD"))));
    QVERIFY(!result.limited);
    QVERIFY(result.error.isEmpty());
    result = typist::platform::indexMarkdownFiles(directory.path(), cancelled, 1);
    QCOMPARE(result.paths.size(), 1);
    QVERIFY(result.limited);
    result = typist::platform::indexMarkdownFiles(directory.path(), cancelled, 100, 1);
    QVERIFY(result.limited);
    cancelled.store(true);
    QVERIFY(typist::platform::indexMarkdownFiles(directory.path(), cancelled).paths.isEmpty());
    QVERIFY(!typist::platform::indexMarkdownFiles(directory.filePath("missing"), cancelled)
                 .error.isEmpty());
  }

  void keyboardSearchAndCancel() {
    QTemporaryDir directory;
    const auto first = directory.filePath(QStringLiteral("alpha.md"));
    const auto second = directory.filePath(QStringLiteral("beta.md"));
    QVERIFY(write(first));
    QVERIFY(write(second));
    typist::ui::QuickOpenDialog dialog(directory.path(), {});
    auto* query = dialog.findChild<QLineEdit*>(QStringLiteral("quickOpenQuery"));
    auto* list = dialog.findChild<QListWidget*>(QStringLiteral("quickOpenResults"));
    auto* status = dialog.findChild<QLabel*>(QStringLiteral("quickOpenStatus"));
    QSignalSpy accepted(&dialog, &QDialog::accepted);
    dialog.show();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("2 files")));
    QCOMPARE(dialog.selectedPath(), first);
    QTest::keyClick(query, Qt::Key_Down);
    QCOMPARE(dialog.selectedPath(), second);
    QTest::keyClick(query, Qt::Key_Up);
    QCOMPARE(dialog.selectedPath(), first);
    QTest::keyClick(query, Qt::Key_PageDown);
    QCOMPARE(dialog.selectedPath(), second);
    // Enter and arrow keys must resolve the query without waiting for debounce.
    query->setText(QStringLiteral("no-file"));
    QTest::keyClick(query, Qt::Key_Down);
    QCOMPARE(list->count(), 0);
    QTest::keyClick(query, Qt::Key_Return);
    QCOMPARE(accepted.size(), 0);
    query->setText(QStringLiteral("alp"));
    QTest::keyClick(query, Qt::Key_Return);
    QCOMPARE(accepted.size(), 1);
    QCOMPARE(dialog.selectedPath(), first);

    typist::ui::QuickOpenDialog cancelled(directory.path(), {});
    QSignalSpy rejected(&cancelled, &QDialog::rejected);
    cancelled.show();
    QTest::keyClick(cancelled.findChild<QLineEdit*>(), Qt::Key_Escape);
    QCOMPARE(rejected.size(), 1);
  }

  void recentFilesAvailableWithoutFolder() {
    QTemporaryDir directory;
    const auto recent = directory.filePath(QStringLiteral("recent.md"));
    QVERIFY(write(recent));
    typist::ui::QuickOpenDialog dialog(directory.filePath("missing"),
                                       {directory.filePath("deleted.md"), recent});
    QCOMPARE(dialog.selectedPath(), recent);
    auto* status = dialog.findChild<QLabel*>(QStringLiteral("quickOpenStatus"));
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Folder unavailable")));
    QCOMPARE(dialog.findChild<QListWidget*>()->count(), 1);
  }

  void closesDuringScan() {
    QTemporaryDir directory;
    for (int i = 0; i < 100; ++i)
      QVERIFY(write(directory.filePath(QStringLiteral("folder-%1/note.md").arg(i))));
    // Destructor must cancel/join its worker even before the finished signal is delivered.
    for (int i = 0; i < 5; ++i) {
      typist::ui::QuickOpenDialog dialog(directory.path(), {});
      dialog.show();
      dialog.reject();
    }
  }
};

QTEST_MAIN(QuickOpenTests)
#include "quick_open_tests.moc"
