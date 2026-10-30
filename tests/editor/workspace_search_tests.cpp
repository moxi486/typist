#include "FileSearch.h"
#include "FolderSearchRunner.h"
#include "WorkspaceSearchPanel.h"

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
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
  void matchingOptions_data() {
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<bool>("regex");
    QTest::addColumn<bool>("words");
    QTest::addColumn<bool>("sensitive");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("length");
    QTest::newRow("literal-metacharacters")
        << QStringLiteral("a-b a.b") << QStringLiteral("a.b") << false << false << false << 4 << 3;
    QTest::newRow("literal-escaped-whole-word")
        << QStringLiteral("xa.b a.b!") << QStringLiteral("a.b") << false << true << false << 5 << 3;
    QTest::newRow("regex-variable-length")
        << QStringLiteral("TODO: note42") << QStringLiteral("note\\d+") << true << false << false
        << 6 << 6;
    QTest::newRow("regex-case-fold") << QStringLiteral("Plan12 plan3") << QStringLiteral("plan\\d+")
                                     << true << false << false << 0 << 6;
    QTest::newRow("regex-case-sensitive")
        << QStringLiteral("Plan12 plan3") << QStringLiteral("plan\\d+") << true << false << true
        << 7 << 5;
    QTest::newRow("regex-whole-alternation")
        << QStringLiteral("scatter dogs cat") << QStringLiteral("cat|dog") << true << true << false
        << 13 << 3;
    QTest::newRow("whole-word-retry-alternation")
        << QStringLiteral("cat") << QStringLiteral("ca|cat") << true << true << false << 0 << 3;
    QTest::newRow("whole-word-letter-number-connector")
        << QStringLiteral("cat2 _cat cat_ cat") << QStringLiteral("cat") << false << true << false
        << 15 << 3;
    QTest::newRow("whole-word-combining-mark")
        << QStringLiteral("cafe\u0301 cafe") << QStringLiteral("cafe") << false << true << false
        << 6 << 4;
    QTest::newRow("whole-word-chinese") << QStringLiteral("新计划 计划！") << QStringLiteral("计划")
                                        << false << true << false << 4 << 2;
    QTest::newRow("unicode-properties-and-utf16")
        << QStringLiteral("😀 计划42") << QStringLiteral("\\p{L}+\\d+") << true << false << false
        << 3 << 4;
    QTest::newRow("backreferences") << QStringLiteral("go go!") << QStringLiteral("(go)\\s+\\1")
                                    << true << true << false << 0 << 5;
    QTest::newRow("heading-anchor") << QStringLiteral("## Heading") << QStringLiteral("^#{1,6}\\s")
                                    << true << false << false << 0 << 3;
    QTest::newRow("zero-width") << QStringLiteral("TODO") << QStringLiteral("(?=TODO)") << true
                                << false << false << 0 << 0;
    QTest::newRow("end-anchor") << QStringLiteral("TODO") << QStringLiteral("$") << true << false
                                << false << 4 << 0;
    QTest::newRow("no-substring") << QStringLiteral("scatter cats") << QStringLiteral("cat")
                                  << false << true << false << -1 << 0;
  }

  void matchingOptions() {
    QFETCH(QString, text);
    QFETCH(QString, pattern);
    QFETCH(bool, regex);
    QFETCH(bool, words);
    QFETCH(bool, sensitive);
    QFETCH(int, column);
    QFETCH(int, length);
    QTemporaryDir directory;
    QVERIFY(write(directory.filePath("note.md"), text.toUtf8()));
    SearchRequest request;
    request.rootDir = directory.path();
    request.pattern = pattern;
    request.regularExpression = regex;
    request.wholeWords = words;
    request.caseSensitive = sensitive;
    QString error;
    const auto hits = searchFiles(request, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(hits.size(), column < 0 ? 0 : 1);
    if (column < 0) return;
    QCOMPARE(hits.first().column, column);
    QCOMPARE(hits.first().length, length);
    QCOMPARE(hits.first().sourceLine, text);
  }

  void regexEmptyLinesErrorsAndLimits() {
    QTemporaryDir directory;
    const auto path = directory.filePath("note.md");
    QVERIFY(write(path, "first\r\n\r\nlast\r\n"));
    SearchRequest request;
    request.rootDir = directory.path();
    request.pattern = QStringLiteral("^$");
    request.regularExpression = true;
    QString error;
    auto hits = searchFiles(request, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(hits.size(), 2);
    QCOMPARE(hits.first().line, 2);
    QCOMPARE(hits.last().line, 4);
    for (const auto& hit : hits) {
      QCOMPARE(hit.column, 0);
      QCOMPARE(hit.length, 0);
      QVERIFY(hit.sourceLine.isEmpty());
    }
    QVERIFY(write(path, ""));
    QCOMPARE(searchFiles(request, &error).size(), 1);
    SearchStats stats;
    for (bool words : {false, true}) {
      request.wholeWords = words;
      for (const auto& pattern : {QStringLiteral("["), QStringLiteral("("), QStringLiteral("\\"),
                                  QStringLiteral("a{2,1}")}) {
        request.pattern = pattern;
        QVERIFY(searchFiles(request, &error, &stats).isEmpty());
        QCOMPARE(error, QStringLiteral("invalid_regex"));
        QCOMPARE(stats.scannedFiles, 0);
      }
    }
    request.wholeWords = false;
    request.pattern = QStringLiteral("one\rtwo");
    QVERIFY(searchFiles(request, &error).isEmpty());
    QCOMPARE(error, QStringLiteral("invalid_pattern"));
    request.pattern = QStringLiteral("(a+)+$");
    QVERIFY(write(path, "a\n" + QByteArray(10000, 'a') + "!"));
    hits = searchFiles(request, &error);
    QCOMPARE(error, QStringLiteral("regex_match_failed"));
    QCOMPARE(hits.size(), 1);  // Keep the completed lines, but report the incomplete search.
    request.pattern = QStringLiteral("^a+$");
    hits = searchFiles(request, &error);
    QVERIFY(error.isEmpty());
    QCOMPARE(hits.size(), 1);
    request.pattern = QStringLiteral("a");
    request.maxHits = 1;
    QCOMPARE(searchFiles(request, &error, &stats).size(), 1);
    QVERIFY(stats.limited);
  }

  void panelOptionsRerunAndRecoverFromErrors() {
    QTemporaryDir directory;
    QVERIFY(write(directory.filePath("note.md"), "cat\ncat2\ncat+\nCAT\n"));
    typist::ui::WorkspaceSearchPanel panel;
    panel.setRoot(directory.path());
    panel.show();
    auto* query = panel.findChild<QLineEdit*>("workspaceSearchQuery");
    auto* words = panel.findChild<QToolButton*>("workspaceSearchWords");
    auto* regex = panel.findChild<QToolButton*>("workspaceSearchRegex");
    auto* sensitive = panel.findChild<QToolButton*>("workspaceSearchCase");
    auto* tree = panel.findChild<QTreeWidget*>("workspaceSearchResults");
    auto* status = panel.findChild<QLabel*>("workspaceSearchStatus");
    query->setText(QStringLiteral("cat"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 4")));
    words->click();
    QCOMPARE(tree->topLevelItemCount(), 0);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 3")));
    sensitive->click();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 2")));
    query->setText(QStringLiteral("cat+"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 1")));
    regex->click();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 2")));
    // Replace multiple queued requests, including an invalid regular expression.
    query->setText(QStringLiteral("["));
    QTest::keyClick(query, Qt::Key_Return);
    query->setText(QStringLiteral("^CAT$"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 1")));
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("4: CAT"));
    query->setText(QStringLiteral("["));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Invalid regular expression.")));
    QCOMPARE(tree->topLevelItemCount(), 0);
    regex->click();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 0")));
    regex->click();
    query->setText(QStringLiteral("(a+)+$"));
    QVERIFY(write(directory.filePath("note.md"), QByteArray(10000, 'a') + "!"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Pattern is too complex.")));
    panel.hide();
    panel.show();
    query->setText(QStringLiteral("^a+!$"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 1")));
  }

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
    auto* firstGroup = tree->topLevelItem(0);
    tree->setCurrentItem(firstGroup);
    QTest::keyClick(tree, Qt::Key_Enter);
    QVERIFY(!firstGroup->isExpanded());
    QTest::keyClick(tree, Qt::Key_Return);
    QVERIFY(firstGroup->isExpanded());
    QCOMPARE(open.count(), 1);
    const auto point = tree->visualItemRect(firstGroup->child(0)).center();
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(open.count(), 1);
    QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(open.count(), 2);
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
