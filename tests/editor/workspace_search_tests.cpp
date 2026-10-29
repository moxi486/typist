#include "FileSearch.h"
#include "FolderSearchRunner.h"
#include "WorkspaceSearchPanel.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest>

using namespace typist::platform;
namespace {
bool write(const QString& path, const QByteArray& text) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}
}  // namespace

class WorkspaceSearchTests : public QObject {
  Q_OBJECT
 private slots:
  void sourceLinesUnicodeCaseAndPreviewContext() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("中文.MD"));
    const QString longLine = QString(250, 'a') + QStringLiteral("计划 needle tail");
    QVERIFY(
        write(path, (QStringLiteral("# Heading\r\nNeedle needle\r\n") + longLine + '\n').toUtf8()));
    SearchRequest request;
    request.rootDir = directory.path();
    request.pattern = QStringLiteral("needle");
    SearchStats stats;
    QString error = QStringLiteral("old");
    auto hits = searchFiles(request, &error, &stats);
    QVERIFY(error.isEmpty());
    QCOMPARE(hits.size(), 2);  // One result per matching source line.
    QCOMPARE(hits.at(0).line, 2);
    QCOMPARE(hits.at(0).column, 0);
    QCOMPARE(hits.at(0).sourceLine, QStringLiteral("Needle needle"));
    QCOMPARE(hits.at(1).line, 3);
    QCOMPARE(hits.at(1).column, 253);
    QVERIFY(hits.at(1).preview.contains(QStringLiteral("needle")));
    QVERIFY(hits.at(1).preview.size() < 150);
    request.caseSensitive = true;
    hits = searchFiles(request, &error);
    QCOMPARE(hits.at(0).column, 7);
    request.pattern = QStringLiteral("计划");
    hits = searchFiles(request, &error);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().column, 250);
    QCOMPARE(hits.first().length, 2);
  }

  void filtersLimitsAndErrors() {
    QTemporaryDir directory;
    for (const auto& name :
         {QStringLiteral("note.md"), QStringLiteral("a/b/中文.MD"), QStringLiteral(".hidden.md"),
          QStringLiteral(".git/a.md"), QStringLiteral("build/a.md"),
          QStringLiteral("node_modules/a.md"), QStringLiteral("other.txt")})
      QVERIFY(write(directory.filePath(name), "needle\nneedle\n"));
#ifdef Q_OS_UNIX
    QVERIFY(QFile::link(directory.path(), directory.filePath("loop")));
    QVERIFY(QFile::link(directory.filePath("note.md"), directory.filePath("alias.md")));
#endif
    QVERIFY(write(directory.filePath("large.md"), QByteArray(500, 'x')));
    QVERIFY(write(directory.filePath("binary.md"), QByteArray("needle\0bytes", 12)));
    SearchRequest request;
    request.rootDir = directory.path();
    request.pattern = QStringLiteral("needle");
    request.maxFileBytes = 100;
    SearchStats stats;
    QString error;
    QCOMPARE(searchFiles(request, &error, &stats).size(), 4);
    QCOMPARE(stats.scannedFiles, 2);
    QCOMPARE(stats.skippedFiles, 2);
    QVERIFY(!stats.limited);
    request.maxHits = 1;
    QCOMPARE(searchFiles(request, &error, &stats).size(), 1);
    QVERIFY(stats.limited);
    request.maxHits = 0;
    request.maxFiles = 1;
    searchFiles(request, &error, &stats);
    QVERIFY(stats.limited);
    request.maxFiles = 100;
    request.maxEntries = 1;
    searchFiles(request, &error, &stats);
    QVERIFY(stats.limited);
    request.maxEntries = 100;
    request.maxTotalBytes = 1;
    QVERIFY(searchFiles(request, &error, &stats).isEmpty());
    QVERIFY(stats.limited);
    request.rootDir = directory.filePath("missing");
    QVERIFY(searchFiles(request, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("folder_unavailable"));
    request.pattern.clear();
    QVERIFY(searchFiles(request, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("invalid_pattern"));
    std::atomic<bool> cancelled{true};
    bool wasCancelled = false;
    searchFilesCancellable(request, &error, cancelled, &wasCancelled);
    QVERIFY(wasCancelled);
    QVERIFY(error.isEmpty());
  }

  void latestRequestWinsAndDiscardSuppressesQueuedResults() {
    QTemporaryDir directory;
    QVERIFY(write(directory.filePath("note.md"), "alpha\nbeta\ngamma\n"));
    FolderSearchRunner runner;
    QSignalSpy results(&runner, &FolderSearchRunner::resultsReady);
    SearchRequest request;
    request.rootDir = directory.path();
    request.pattern = QStringLiteral("alpha");
    runner.start(request);
    request.pattern = QStringLiteral("beta");
    runner.start(request);
    request.pattern = QStringLiteral("gamma");
    runner.start(request);
    QTRY_COMPARE(results.count(), 1);
    const auto hits = results.first().at(0).value<QVector<SearchHit>>();
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.first().sourceLine, QStringLiteral("gamma"));
    QVERIFY(!runner.isRunning());
    results.clear();
    runner.start(request);
    runner.discard();
    QTRY_VERIFY(!runner.isRunning());
    QCOMPARE(results.count(), 0);
    runner.start(request);
    runner.start(request);
    runner.cancel();
    QTRY_COMPARE(results.count(), 1);
    QVERIFY(results.first().at(0).value<QVector<SearchHit>>().isEmpty());
    QVERIFY(results.first().at(1).toBool());
    // Shutdown joins only the active worker, including when it has a pending replacement.
    for (int i = 0; i < 3; ++i) {
      FolderSearchRunner closing;
      closing.start(request);
      closing.start(request);
    }
  }

  void panelGroupsNavigatesAndInvalidatesResults() {
    QTemporaryDir directory;
    QVERIFY(write(directory.filePath("a/same.md"), "needle\nneedle\n"));
    QVERIFY(write(directory.filePath("b/same.md"), "needle\n"));
    typist::ui::WorkspaceSearchPanel panel;
    panel.setRoot(directory.path());
    panel.show();
    auto* query = panel.findChild<QLineEdit*>("workspaceSearchQuery");
    auto* tree = panel.findChild<QTreeWidget*>("workspaceSearchResults");
    auto* status = panel.findChild<QLabel*>("workspaceSearchStatus");
    query->setText(QStringLiteral("needle"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 3")));
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->text(0), QDir::toNativeSeparators(QStringLiteral("a/same.md")));
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
    QSignalSpy open(&panel, &typist::ui::WorkspaceSearchPanel::hitActivated);
    QTest::keyClick(query, Qt::Key_Down);
    QTest::keyClick(tree, Qt::Key_Return);
    QCOMPARE(open.count(), 1);
    QCOMPARE(open.first().at(0).value<SearchHit>().line, 1);
    query->setText(QStringLiteral("no matches"));
    QCOMPARE(tree->topLevelItemCount(), 0);
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 0")));
    query->setText(QStringLiteral("needle"));
    QTest::keyClick(query, Qt::Key_Return);
    panel.findChild<QPushButton*>("workspaceSearchCancel")->click();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Search cancelled.")));
    panel.setRoot(directory.filePath("missing"));
    panel.openFocused();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Folder unavailable.")));
    panel.stop();
  }
};
QTEST_MAIN(WorkspaceSearchTests)
#include "workspace_search_tests.moc"
