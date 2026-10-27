#include "editor_document.h"
#include "FileSearch.h"
#include "FileWriter.h"
#include "PdfWriter.h"
#include "AutoSave.h"
#include "DocxWriter.h"
#include "FileOps.h"
#include "FileTree.h"
#include "Search.h"
#include "Conflict.h"
#include "FolderSearchRunner.h"
#include "Localization.h"
#include "Toc.h"
#include "RecoveryStore.h"
#include "Settings.h"
#include "SystemTheme.h"
#include "ViewSwitch.h"
#include "Theme.h"
#include "markdown.h"

#include <QtTest>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QTextCursor>
#include <QUrl>
#include <QTextBlock>
#include <QTextDocument>

#include <filesystem>
#include <fstream>
#include <string>

class EditorSpike : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void headingsAndParagraphsSurviveDocument();
  void documentRoundTripMatchesSource();
  void taskListRoundTripsThroughDocument();
  void tableCapabilityProbe();
  void tableSerializationStaysOnTheAst();
  void writeFileAtomicallyOverwritesTarget();
  void savedMarkdownMatchesParsedSource();
  void undoRestoresSavedMarkdown();
  void sourceToggleRoundTripsWithoutUndoLeak();
  void undoHistoryStaysBounded();
  void pdfExportWritesPdfFile();
  void pdfExportSelectsPaper();
  void pdfExportPaginatesLongDocument();
  void pdfExportWiderMarginsProduceMorePages();
  void viewSwitchKeepsRenderedHistoryAcrossRoundTrip();
  void viewSwitchInjectionResetsTargetHistory();
  void viewSwitchRebuildsRenderedFromEditedSource();
  void autoSaveDebouncesTypingBursts();
  void autoSaveFlushRunsImmediately();
  void autoSaveStaysIdleWithoutEdits();
  void themeResolvesPreferenceAndStyles();
  void themePreferenceRoundTripsThroughSettings();
  void htmlExportEmbedsThemeStylesheet();
  void searchCountsMatchesAcrossViews();
  void searchWrapsAroundAtBothEnds();
  void searchRespectsCaseSensitivity();
  void searchReachesTableCells();
  void fileSearchReportsPathsAndLineNumbers();
  void fileSearchSkipsHiddenDirectories();
  void fileSearchRespectsCaseSensitivity();
  void fileSearchHonorsMaxHits();
  void fileSearchStaysWithinBudgetOnLargeFile();
  void fileTreeListsMarkdownFilesUnderRoot();
  void fileTreeSkipsHiddenDirectories();
  void fileTreeRootsSwitchReplacesEntries();
  void fileTreeSelectionTracksPath();
  void fileTreeGroupsFilesByDirectory();
  void fileTreeDirectoryCountMatchesSubdirectories();
  void fileTreeRebuildSeesNewFile();
  void platformFileOpsCreateRenameDelete();
  void platformCreateRejectsDuplicateName();
  void platformRenameAppendsMarkdownSuffix();
  void fileTreeRefreshDetectsAddedFile();
  void fileTreeRefreshDetectsContentChange();
  void fileTreeRefreshPreservesSelection();
  void dropTargetPicksMarkdownFile();
  void searchHighlightsEveryMatch();
  void searchHighlightRespectsCaseSensitivity();
  void docxExportWritesEachPackageMemberOnce();
  void docxExportReportsHeadingTableAndTaskItem();
  void docxExportFailsWhenPathUnwritable();
  void outlineListsHeadingLevelsAndPositions();
  void outlineCapsEntryCount();
  void regexSearchCountsAnchoredHeadingLines();
  void regexAnchorsMatchSourceViewLines();
  void regexSearchIgnoresInvalidPatterns();
  void regexSearchStepsAndWrapsAround();
  void regexSearchHonoursCaseFlag();
  void recentFilesKeepNewestFirstAndCap();
  void recoveryStoreRoundTripsThroughEnvOverride();
  void recoveryStoreKeysSeparateUntitledAndPaths();
  void conflictDecisionProtectsUnsavedEdits();
  void folderSearchRunnerReportsHitsOffUiThread();
  void cancellableScanStopsWhenFlagIsSet();
  void translationCandidatesFollowLocaleLanguage();
  void translationFileCoversMainWindowStrings();
  void translatorFallsBackWhenCatalogMissing();
  void systemThemeParsesPlatformHints();
  void systemThemeReadsWindowsRegistryHint();

 private:
  std::string m_corpus;
  std::string read(const std::string& name) const;
};

std::string EditorSpike::read(const std::string& name) const {
  std::ifstream stream(std::filesystem::path(m_corpus) / name);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void EditorSpike::initTestCase() {
  m_corpus = qEnvironmentVariable("TYPIST_CORPUS_DIR").toStdString();
  QVERIFY(!m_corpus.empty());
}

void EditorSpike::headingsAndParagraphsSurviveDocument() {
  const auto parsed = typist::core::parseMarkdown(read("headings.md"));
  QVERIFY(parsed.ok);

  QTextDocument document;
  typist::editor::buildDocument(parsed.blocks, &document);
  QVERIFY(document.toPlainText().contains("Title"));
  QVERIFY(document.toPlainText().contains("Section"));
  QVERIFY(document.blockCount() >= static_cast<int>(parsed.blocks.size()));
}

void EditorSpike::documentRoundTripMatchesSource() {
  const std::vector<std::string> files = {
      "headings.md", "paragraphs.md", "lists.md", "nested_ordered.md", "nested_wide.md",
      "thematic.md", "code.md", "code_info.md", "fences.md", "gfm/table.md",
      "gfm/table_then_para.md",
      "gfm/table_escapes.md", "gfm/tasklist.md",
  };
  for (const std::string& name : files) {
    const auto parsed = typist::core::parseMarkdown(read(name));
    QVERIFY2(parsed.ok, name.c_str());

    QTextDocument document;
    typist::editor::buildDocument(parsed.blocks, &document);
    const auto reparsed = typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
    QVERIFY2(reparsed.ok, name.c_str());
    QVERIFY2(typist::core::astEqual(parsed.blocks, reparsed.blocks), name.c_str());
  }
}

void EditorSpike::taskListRoundTripsThroughDocument() {
  const auto parsed = typist::core::parseMarkdown(read("gfm/tasklist.md"));
  QVERIFY(parsed.ok);

  QTextDocument document;
  typist::editor::buildDocument(parsed.blocks, &document);
  const auto reparsed = typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
  QVERIFY(reparsed.ok);
  QVERIFY(typist::core::astEqual(parsed.blocks, reparsed.blocks));
  QVERIFY(document.toPlainText().contains("done task"));
}

void EditorSpike::tableCapabilityProbe() {
  QTextDocument document;
  QVERIFY(typist::editor::insertTableProbe(&document, 2, 2));

  const auto parsed = typist::core::parseMarkdown(read("gfm/table.md"));
  QVERIFY(parsed.ok);
  QTextDocument rendered;
  typist::editor::buildDocument(parsed.blocks, &rendered);
  QVERIFY(rendered.toPlainText().contains("a"));
  QVERIFY(rendered.toPlainText().contains("2"));
}

void EditorSpike::tableSerializationStaysOnTheAst() {
  // The document side now emits the table itself, so both paths must agree.
  const auto parsed = typist::core::parseMarkdown(read("gfm/table.md"));
  QVERIFY(parsed.ok);

  QTextDocument document;
  typist::editor::buildDocument(parsed.blocks, &document);
  QVERIFY(typist::editor::serializeDocument(&document).contains("| --- |"));
  QVERIFY(typist::core::serializeMarkdown(parsed.blocks).find("| --- |") != std::string::npos);

  const auto reparsed =
      typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
  QVERIFY(reparsed.ok);
  QVERIFY(typist::core::astEqual(parsed.blocks, reparsed.blocks));
}

void EditorSpike::undoRestoresSavedMarkdown() {
  QTextDocument document;
  document.setUndoRedoEnabled(true);
  const auto parsed = typist::core::parseMarkdown(read("lists.md"));
  QVERIFY(parsed.ok);
  typist::editor::buildDocument(parsed.blocks, &document);
  document.clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);

  const QString saved = typist::editor::serializeDocument(&document);
  QVERIFY(!document.isUndoAvailable());

  QTextCursor cursor(&document);
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral(" extra words"));
  const QString edited = typist::editor::serializeDocument(&document);
  QVERIFY(edited != saved);
  QVERIFY(document.isUndoAvailable());

  document.undo();
  QVERIFY(typist::editor::serializeDocument(&document) == saved);
  QVERIFY(document.isRedoAvailable());

  document.redo();
  QVERIFY(typist::editor::serializeDocument(&document) == edited);
}

void EditorSpike::sourceToggleRoundTripsWithoutUndoLeak() {
  QTextDocument document;
  document.setUndoRedoEnabled(true);
  const auto parsed = typist::core::parseMarkdown(read("gfm/table_escapes.md"));
  QVERIFY(parsed.ok);
  typist::editor::buildDocument(parsed.blocks, &document);

  const QString source = typist::editor::serializeDocument(&document);
  QTextCursor cursor(&document);
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral(" typed edit"));
  QVERIFY(document.isUndoAvailable());

  QString error;
  QVERIFY2(typist::editor::loadMarkdown(source, &document, &error), error.isEmpty() ? "parse failed" : qPrintable(error));
  QVERIFY(!document.isUndoAvailable());

  const auto reparsed = typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
  QVERIFY(reparsed.ok);
  QVERIFY(typist::core::astEqual(parsed.blocks, reparsed.blocks));
  QVERIFY(!document.toPlainText().contains("typed edit"));
}

void EditorSpike::undoHistoryStaysBounded() {
  QTextDocument document;
  document.setUndoRedoEnabled(true);
  const auto parsed = typist::core::parseMarkdown(read("paragraphs.md"));
  QVERIFY(parsed.ok);
  typist::editor::buildDocument(parsed.blocks, &document);
  document.clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);

  QTextCursor cursor(&document);
  cursor.movePosition(QTextCursor::End);
  for (int i = 0; i <= typist::editor::kUndoStepLimit; ++i) {
    cursor.beginEditBlock();
    cursor.insertText(QStringLiteral("x"));
    cursor.endEditBlock();
  }
  QVERIFY(document.availableUndoSteps() > typist::editor::kUndoStepLimit);

  typist::editor::boundUndoHistory(&document);
  QVERIFY(document.availableUndoSteps() == 0);
  QVERIFY(!document.isUndoAvailable());
}

int pdfPageCount(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return 0;
  const QByteArray data = file.readAll();
  file.close();
  int pages = 0;
  int offset = 0;
  while ((offset = data.indexOf("/Type /Page", offset)) != -1) {
    if (data.mid(offset + 11, 1) != QByteArrayLiteral("s")) ++pages;
    offset += 11;
  }
  return pages;
}

QSizeF pdfMediaBox(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return QSizeF();
  const QByteArray data = file.readAll();
  file.close();
  const QByteArray marker = QByteArrayLiteral("/MediaBox [0 0 ");
  const int offset = data.indexOf(marker);
  if (offset == -1) return QSizeF();
  const QByteArray rest = data.mid(offset + marker.size(), 32);
  const QList<QByteArray> parts = rest.left(rest.indexOf(']')).split(' ');
  return parts.size() >= 2 ? QSizeF(parts.at(0).toDouble(), parts.at(1).toDouble()) : QSizeF();
}

QString tempPdfPath(const QString& name) { return QDir::temp().filePath(name); }

void EditorSpike::viewSwitchKeepsRenderedHistoryAcrossRoundTrip() {
  typist::ui::ViewSwitch view;
  QString error;
  QVERIFY(view.loadMarkdownIntoBoth(QStringLiteral("# Title\n\nBody text\n"), &error));
  const QString baseline = typist::editor::serializeDocument(view.rendered());

  QTextCursor cursor(view.rendered());
  cursor.movePosition(QTextCursor::End);
  cursor.beginEditBlock();
  cursor.insertText(QStringLiteral(" added"));
  cursor.endEditBlock();

  QVERIFY(view.switchView(&error));
  QVERIFY(view.isSourceMode());
  QVERIFY(view.switchView(&error));
  QVERIFY(!view.isSourceMode());
  QCOMPARE(view.active(), view.rendered());
  QVERIFY(view.rendered()->isUndoAvailable());

  view.rendered()->undo();
  QCOMPARE(typist::editor::serializeDocument(view.rendered()), baseline);
}

void EditorSpike::pdfExportSelectsPaper() {
  if (!typist::platform::pdfExportAvailable()) QSKIP("PDF export unavailable in this build");
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("# Title\n\nBody text\n"), &document, &error));

  const QString a4Path = tempPdfPath(QStringLiteral("typist_a4.pdf"));
  typist::platform::PdfPageOptions options;
  options.paper = QStringLiteral("a4");
  QVERIFY2(typist::platform::exportPdf(a4Path, &document, options, &error), qPrintable(error));
  const QSizeF a4 = pdfMediaBox(a4Path);

  options.paper = QStringLiteral("letter");
  const QString letterPath = tempPdfPath(QStringLiteral("typist_letter.pdf"));
  QVERIFY2(typist::platform::exportPdf(letterPath, &document, options, &error), qPrintable(error));
  const QSizeF letter = pdfMediaBox(letterPath);

  QVERIFY(a4.width() > 0.0);
  QVERIFY(letter.width() > 0.0);
  // MediaBox is written in points: A4 is narrower but taller than Letter.
  QVERIFY(a4.width() < letter.width());
  QVERIFY(a4.height() > letter.height());
  QFile::remove(a4Path);
  QFile::remove(letterPath);
}
void EditorSpike::pdfExportWritesPdfFile() {
  if (!typist::platform::pdfExportAvailable()) QSKIP("Qt PrintSupport is not available in this build");

  QTextDocument document;
  const auto parsed = typist::core::parseMarkdown(read("gfm/table.md"));
  QVERIFY(parsed.ok);
  typist::editor::buildDocument(parsed.blocks, &document);

  const QString path = QDir::tempPath() + QStringLiteral("/typist_pdf_probe.pdf");
  QFile::remove(path);
  QString error;
  QVERIFY2(typist::platform::exportPdf(path, &document, &error), qPrintable(error));

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QVERIFY(file.size() > 200);
  QVERIFY(file.read(5) == QByteArray("%PDF-"));
  file.close();
  QFile::remove(path);
}

void EditorSpike::viewSwitchInjectionResetsTargetHistory() {
  typist::ui::ViewSwitch view;
  QString error;
  QVERIFY(view.loadMarkdownIntoBoth(QStringLiteral("# Title\n\nBody text\n"), &error));

  QTextCursor cursor(view.rendered());
  cursor.beginEditBlock();
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral(" extra"));
  cursor.endEditBlock();

  QVERIFY(view.switchView(&error));
  QCOMPARE(view.source()->toPlainText(), typist::editor::serializeDocument(view.rendered()));
  // Injected content is a new baseline, so the plain view starts with an empty stack.
  QVERIFY(!view.source()->isUndoAvailable());
  // The rendered view keeps the edit that was made while it was shown.
  QVERIFY(view.rendered()->isUndoAvailable());
}

void EditorSpike::viewSwitchRebuildsRenderedFromEditedSource() {
  typist::ui::ViewSwitch view;
  QString error;
  QVERIFY(view.loadMarkdownIntoBoth(QStringLiteral("# Title\n\nBody text\n"), &error));
  QVERIFY(view.switchView(&error));

  QTextCursor cursor(view.source());
  cursor.beginEditBlock();
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("\n## Added section\n"));
  cursor.endEditBlock();

  QVERIFY(view.switchView(&error));
  // Blank-line differences are normalized by the serializer, so compare ASTs.
  const auto fromRendered = typist::core::parseMarkdown(typist::editor::serializeDocument(view.rendered()).toStdString());
  const auto fromSource = typist::core::parseMarkdown(view.source()->toPlainText().toStdString());
  QVERIFY(fromRendered.ok);
  QVERIFY(fromSource.ok);
  QVERIFY(typist::core::astEqual(fromRendered.blocks, fromSource.blocks));
  QVERIFY(!view.rendered()->isUndoAvailable());
}

void EditorSpike::autoSaveDebouncesTypingBursts() {
  typist::ui::AutoSave autoSave;
  int flushes = 0;
  autoSave.setInterval(60);
  autoSave.setCallback([&flushes] { ++flushes; });

  autoSave.notifyChanged();
  autoSave.notifyChanged();
  autoSave.notifyChanged();
  QVERIFY(autoSave.isPending());
  QTRY_COMPARE_WITH_TIMEOUT(flushes, 1, 2000);
  QVERIFY(!autoSave.isPending());
}

void EditorSpike::autoSaveFlushRunsImmediately() {
  typist::ui::AutoSave autoSave;
  int flushes = 0;
  autoSave.setInterval(200);
  autoSave.setCallback([&flushes] { ++flushes; });

  autoSave.notifyChanged();
  autoSave.flushNow();
  QCOMPARE(flushes, 1);
  QVERIFY(!autoSave.isPending());
  // The expired window must not fire a second write after an explicit flush.
  QTest::qWait(250);
  QCOMPARE(flushes, 1);
}

void EditorSpike::autoSaveStaysIdleWithoutEdits() {
  typist::ui::AutoSave autoSave;
  int flushes = 0;
  autoSave.setInterval(60);
  autoSave.setCallback([&flushes] { ++flushes; });

  QTest::qWait(200);
  QCOMPARE(flushes, 0);
  QVERIFY(!autoSave.isPending());
}

void EditorSpike::themeResolvesPreferenceAndStyles() {
  QVERIFY(typist::ui::resolveTheme(QStringLiteral("light"), true) == typist::ui::Theme::Light);
  QVERIFY(typist::ui::resolveTheme(QStringLiteral("dark"), false) == typist::ui::Theme::Dark);
  QVERIFY(typist::ui::resolveTheme(QStringLiteral("system"), true) == typist::ui::Theme::Dark);
  QVERIFY(typist::ui::resolveTheme(QStringLiteral("system"), false) == typist::ui::Theme::Light);
  QVERIFY(typist::ui::resolveTheme(QStringLiteral("nonsense"), false) == typist::ui::Theme::Light);

  QVERIFY(typist::ui::editorStyleSheet(typist::ui::Theme::Light) != typist::ui::editorStyleSheet(typist::ui::Theme::Dark));
  QVERIFY(typist::ui::htmlStyle(typist::ui::Theme::Dark).contains("#1f2328"));
  QVERIFY(typist::ui::htmlStyle(typist::ui::Theme::Light).contains("#ffffff"));
  QVERIFY(typist::ui::themeName(typist::ui::Theme::Dark) == QStringLiteral("dark"));
}

void EditorSpike::themePreferenceRoundTripsThroughSettings() {
  const QString storeDir = QDir::tempPath() + QStringLiteral("/typist_theme_settings_probe");
  QDir().mkpath(storeDir);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storeDir);

  typist::platform::setThemePreference(QStringLiteral("dark"));
  QVERIFY(typist::platform::themePreference() == QStringLiteral("dark"));

  typist::platform::setThemePreference(QStringLiteral("system"));
  QVERIFY(typist::platform::themePreference() == QStringLiteral("system"));

  QFile::remove(storeDir + QStringLiteral("/Typist.ini"));
}

void EditorSpike::htmlExportEmbedsThemeStylesheet() {
  const std::string body = typist::core::toHtml(read("gfm/table.md"));
  const QString css = typist::ui::htmlStyle(typist::ui::Theme::Dark);
  const std::string page = typist::core::wrapHtmlDocument(body, "Doc", css.toStdString());
  QVERIFY(QString::fromStdString(page).contains(css));
  QVERIFY(QString::fromStdString(page).contains("#1f2328"));
  QVERIFY(!QString::fromStdString(typist::core::wrapHtmlDocument(body, "Doc")).contains("#1f2328"));
}

void EditorSpike::writeFileAtomicallyOverwritesTarget() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_writer_probe.md"));
  QString error;
  QVERIFY2(typist::platform::writeAtomic(path, QStringLiteral("# One\n"), &error), error.toLatin1().constData());
  QVERIFY2(typist::platform::writeAtomic(path, QStringLiteral("# Two\n"), &error), error.toLatin1().constData());

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("# Two\n"));
  file.close();
  QVERIFY(QFile::remove(path));
}

void EditorSpike::savedMarkdownMatchesParsedSource() {
  const auto parsed = typist::core::parseMarkdown(read("quote.md"));
  QVERIFY(parsed.ok);

  QTextDocument document;
  typist::editor::buildDocument(parsed.blocks, &document);

  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_save_probe.md"));
  QString error;
  QVERIFY2(typist::platform::writeAtomic(path, typist::editor::serializeDocument(&document), &error),
           error.toLatin1().constData());

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto reparsed = typist::core::parseMarkdown(QString::fromUtf8(file.readAll()).toStdString());
  QVERIFY(reparsed.ok);
  QVERIFY(typist::core::astEqual(parsed.blocks, reparsed.blocks));
  file.close();
  QVERIFY(QFile::remove(path));
}

void EditorSpike::pdfExportPaginatesLongDocument() {
  if (!typist::platform::pdfExportAvailable()) QSKIP("PDF export unavailable in this build");
  QString source;
  for (int i = 0; i < 200; ++i) {
    source += QStringLiteral("Paragraph %1 with enough prose to occupy a couple of lines.\n\n").arg(i);
  }
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(source, &document, &error));

  const QString path = tempPdfPath(QStringLiteral("typist_multipage.pdf"));
  typist::platform::PdfPageOptions options;
  options.header = QStringLiteral("Typist export test");
  options.footer = QStringLiteral("Page #");
  QVERIFY2(typist::platform::exportPdf(path, &document, options, &error), qPrintable(error));
  QVERIFY(pdfPageCount(path) >= 3);
  QFile::remove(path);
}

void EditorSpike::pdfExportWiderMarginsProduceMorePages() {
  if (!typist::platform::pdfExportAvailable()) QSKIP("PDF export unavailable in this build");
  QString source;
  for (int i = 0; i < 200; ++i) {
    source += QStringLiteral("Paragraph %1 with enough prose to occupy a couple of lines.\n\n").arg(i);
  }
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(source, &document, &error));

  const QString tightPath = tempPdfPath(QStringLiteral("typist_tight.pdf"));
  typist::platform::PdfPageOptions tight;
  tight.marginMm = 10.0;
  QVERIFY2(typist::platform::exportPdf(tightPath, &document, tight, &error), qPrintable(error));
  const QString widePath = tempPdfPath(QStringLiteral("typist_wide.pdf"));
  typist::platform::PdfPageOptions wide;
  wide.marginMm = 60.0;
  QVERIFY2(typist::platform::exportPdf(widePath, &document, wide, &error), qPrintable(error));
  QVERIFY(pdfPageCount(widePath) > pdfPageCount(tightPath));
  QFile::remove(tightPath);
  QFile::remove(widePath);
}

void EditorSpike::searchCountsMatchesAcrossViews() {
  typist::ui::ViewSwitch view;
  QString error;
  QVERIFY(view.loadMarkdownIntoBoth(QStringLiteral("- first item\n- second item\n  - nested item\n- third item\n"), &error));
  typist::ui::SearchController search;
  QVERIFY(search.countMatches(view.rendered(), QStringLiteral("item")) == 4);
  QVERIFY(search.countMatches(view.source(), QStringLiteral("item")) == 4);
}

void EditorSpike::searchWrapsAroundAtBothEnds() {
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("alpha item beta item gamma"), &document, &error));
  typist::ui::SearchController search;
  const int first = document.find(QStringLiteral("item"), 0).selectionStart();
  const int last = document.find(QStringLiteral("item"), first + 1).selectionStart();

  QTextCursor cursor(&document);
  cursor.movePosition(QTextCursor::End);
  QVERIFY(search.selectNext(cursor, QStringLiteral("item")));
  QCOMPARE(cursor.selectionStart(), first);
  QVERIFY(search.selectNext(cursor, QStringLiteral("item")));
  QCOMPARE(cursor.selectionStart(), last);
  // Past the last match the forward step wraps to the first one.
  QVERIFY(search.selectNext(cursor, QStringLiteral("item")));
  QCOMPARE(cursor.selectionStart(), first);
  // Before the first match the backward step wraps to the last one.
  QVERIFY(search.selectPrevious(cursor, QStringLiteral("item")));
  QCOMPARE(cursor.selectionStart(), last);
  QVERIFY(search.selectPrevious(cursor, QStringLiteral("item")));
  QCOMPARE(cursor.selectionStart(), first);
}

void EditorSpike::searchRespectsCaseSensitivity() {
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("Item item ITEM"), &document, &error));
  typist::ui::SearchController search;
  QVERIFY(search.countMatches(&document, QStringLiteral("item")) == 3);
  search.setCaseSensitive(true);
  QVERIFY(search.countMatches(&document, QStringLiteral("item")) == 1);
}

// Table cells live in their own QTextFrame; the probe shows QTextDocument::find reaches them.
void EditorSpike::searchReachesTableCells() {
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("| cell | value |\n| --- | --- |\n| aaa | bbb |\n"), &document, &error));
  typist::ui::SearchController search;
  QVERIFY(search.countMatches(&document, QStringLiteral("aaa")) == 1);
  QVERIFY(search.countMatches(&document, QStringLiteral("value")) == 1);
}

namespace {

QString writeSearchFixture(const QString& dir, const QString& name, const QString& content) {
  const QString path = QDir(dir).filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) return QString();
  file.write(content.toUtf8());
  file.close();
  return path;
}

}  // namespace

void EditorSpike::fileSearchReportsPathsAndLineNumbers() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_search_a"));
  QDir().mkpath(dir);
  writeSearchFixture(dir, QStringLiteral("alpha.md"), QStringLiteral("# Alpha\n\nneedle here\n"));
  writeSearchFixture(dir, QStringLiteral("beta.md"), QStringLiteral("needle first line\n"));

  typist::platform::SearchRequest request;
  request.rootDir = dir;
  request.pattern = QStringLiteral("needle");
  QString error;
  const QVector<typist::platform::SearchHit> hits = typist::platform::searchFiles(request, &error);
  QVERIFY2(hits.size() == 2, error.toLatin1().constData());

  int alphaLine = -1;
  int betaLine = -1;
  for (const typist::platform::SearchHit& hit : hits) {
    if (hit.path.endsWith(QStringLiteral("alpha.md"))) alphaLine = hit.line;
    if (hit.path.endsWith(QStringLiteral("beta.md"))) betaLine = hit.line;
  }
  QCOMPARE(alphaLine, 3);
  QCOMPARE(betaLine, 1);
  QDir(dir).removeRecursively();
}

void EditorSpike::fileSearchSkipsHiddenDirectories() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_search_b"));
  QDir().mkpath(dir);
  writeSearchFixture(dir, QStringLiteral("visible.md"), QStringLiteral("needle visible\n"));
  QDir().mkpath(QDir(dir).filePath(QStringLiteral(".git")));
  writeSearchFixture(QDir(dir).filePath(QStringLiteral(".git")), QStringLiteral("hook.md"), QStringLiteral("needle hidden\n"));

  typist::platform::SearchRequest request;
  request.rootDir = dir;
  request.pattern = QStringLiteral("needle");
  QString error;
  const QVector<typist::platform::SearchHit> hits = typist::platform::searchFiles(request, &error);
  QVERIFY(hits.size() == 1);
  QVERIFY(hits.at(0).path.endsWith(QStringLiteral("visible.md")));
  QDir(dir).removeRecursively();
}

void EditorSpike::fileSearchRespectsCaseSensitivity() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_search_c"));
  QDir().mkpath(dir);
  writeSearchFixture(dir, QStringLiteral("case.md"), QStringLiteral("Needle once\n"));

  typist::platform::SearchRequest request;
  request.rootDir = dir;
  request.pattern = QStringLiteral("needle");
  QString error;
  QVERIFY(typist::platform::searchFiles(request, &error).size() == 1);
  request.caseSensitive = true;
  QVERIFY(typist::platform::searchFiles(request, &error).isEmpty());
  QDir(dir).removeRecursively();
}

void EditorSpike::fileSearchHonorsMaxHits() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_search_d"));
  QDir().mkpath(dir);
  QString body;
  for (int i = 0; i < 60; ++i) body += QStringLiteral("needle line %1\n").arg(i);
  writeSearchFixture(dir, QStringLiteral("many.md"), body);

  typist::platform::SearchRequest request;
  request.rootDir = dir;
  request.pattern = QStringLiteral("needle");
  request.maxHits = 10;
  QString error;
  QVERIFY(typist::platform::searchFiles(request, &error).size() == 10);
  QDir(dir).removeRecursively();
}

void EditorSpike::fileSearchStaysWithinBudgetOnLargeFile() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_search_e"));
  QDir().mkpath(dir);
  QString body;
  for (int i = 0; i < 12000; ++i) body += QStringLiteral("filler paragraph %1 with prose\n").arg(i);
  writeSearchFixture(dir, QStringLiteral("big.md"), body);

  typist::platform::SearchRequest request;
  request.rootDir = dir;
  request.pattern = QStringLiteral("filler paragraph 11999");
  QString error;
  QElapsedTimer timer;
  timer.start();
  const QVector<typist::platform::SearchHit> hits = typist::platform::searchFiles(request, &error);
  const qint64 elapsed = timer.elapsed();
  QVERIFY(hits.size() == 1);
  QVERIFY(elapsed < 400);
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeListsMarkdownFilesUnderRoot() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_tree_a"));
  QDir().mkpath(QDir(dir).filePath(QStringLiteral("b")));
  writeSearchFixture(dir, QStringLiteral("a.md"), QStringLiteral("# A\n"));
  writeSearchFixture(QDir(dir).filePath(QStringLiteral("b")), QStringLiteral("c.md"), QStringLiteral("# C\n"));
  writeSearchFixture(dir, QStringLiteral("notes.txt"), QStringLiteral("ignored\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  QVERIFY(tree.entryCount() == 2);
  QVERIFY(tree.entryPath(0).endsWith(QStringLiteral("a.md")));
  QVERIFY(tree.entryPath(1).endsWith(QStringLiteral("b/c.md")));
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeSkipsHiddenDirectories() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_tree_b"));
  QDir().mkpath(QDir(dir).filePath(QStringLiteral(".git")));
  writeSearchFixture(dir, QStringLiteral("visible.md"), QStringLiteral("# V\n"));
  writeSearchFixture(QDir(dir).filePath(QStringLiteral(".git")), QStringLiteral("hook.md"), QStringLiteral("# H\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  QVERIFY(tree.entryCount() == 1);
  QVERIFY(tree.entryPath(0).endsWith(QStringLiteral("visible.md")));
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeRootsSwitchReplacesEntries() {
  const QString first = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_tree_c1"));
  const QString second = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_tree_c2"));
  QDir().mkpath(first);
  QDir().mkpath(second);
  writeSearchFixture(first, QStringLiteral("one.md"), QStringLiteral("# 1\n"));
  writeSearchFixture(second, QStringLiteral("two.md"), QStringLiteral("# 2\n"));
  writeSearchFixture(second, QStringLiteral("three.md"), QStringLiteral("# 3\n"));

  typist::ui::FileTree tree;
  tree.setRoot(first);
  QVERIFY(tree.entryCount() == 1);
  tree.setRoot(second);
  QVERIFY(tree.entryCount() == 2);
  QVERIFY(tree.entryPath(0).endsWith(QStringLiteral("three.md")));
  QVERIFY(tree.entryPath(1).endsWith(QStringLiteral("two.md")));
  QDir(first).removeRecursively();
  QDir(second).removeRecursively();
}

void EditorSpike::fileTreeSelectionTracksPath() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_tree_d"));
  QDir().mkpath(dir);
  const QString target = writeSearchFixture(dir, QStringLiteral("target.md"), QStringLiteral("# T\n"));
  writeSearchFixture(dir, QStringLiteral("other.md"), QStringLiteral("# O\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  tree.selectPath(target);
  QCOMPARE(tree.selectedPath(), target);
  tree.selectPath(QDir(dir).filePath(QStringLiteral("missing.md")));
  QCOMPARE(tree.selectedPath(), target);
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeGroupsFilesByDirectory() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_tree_h"));
  QDir().mkpath(QDir(dir).filePath(QStringLiteral("sub")));
  writeSearchFixture(dir, QStringLiteral("top.md"), QStringLiteral("# T\n"));
  writeSearchFixture(QDir(dir).filePath(QStringLiteral("sub")), QStringLiteral("deep.md"), QStringLiteral("# D\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  QVERIFY(tree.entryCount() == 2);
  QVERIFY(tree.directoryCount() == 1);
  QVERIFY(tree.entryPath(0).endsWith(QStringLiteral("top.md")));
  QVERIFY(tree.entryPath(1).endsWith(QStringLiteral("deep.md")));
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeDirectoryCountMatchesSubdirectories() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_tree_i"));
  QDir().mkpath(QDir(dir).filePath(QStringLiteral("a")));
  QDir().mkpath(QDir(dir).filePath(QStringLiteral("a/b")));
  writeSearchFixture(QDir(dir).filePath(QStringLiteral("a/b")), QStringLiteral("deep.md"), QStringLiteral("# D\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  QVERIFY(tree.directoryCount() == 2);
  QVERIFY(tree.entryCount() == 1);
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeRebuildSeesNewFile() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_tree_j"));
  QDir().mkpath(dir);
  writeSearchFixture(dir, QStringLiteral("first.md"), QStringLiteral("# F\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  QVERIFY(tree.entryCount() == 1);

  QString error;
  QVERIFY2(typist::platform::createMarkdownFile(dir, QStringLiteral("second"), &error), error.toLatin1().constData());
  tree.setRoot(dir);
  QVERIFY(tree.entryCount() == 2);
  QVERIFY(tree.entryPath(1).endsWith(QStringLiteral("second.md")));
  QDir(dir).removeRecursively();
}

void EditorSpike::platformFileOpsCreateRenameDelete() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_ops_a"));
  QDir().mkpath(dir);
  QString error;
  QVERIFY2(typist::platform::createMarkdownFile(dir, QStringLiteral("note"), &error), error.toLatin1().constData());
  const QString created = QDir(dir).filePath(QStringLiteral("note.md"));
  QVERIFY(QFileInfo::exists(created));

  QFile file(created);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("body\n");
  file.close();

  QVERIFY2(typist::platform::renameFile(created, QStringLiteral("renamed"), &error), error.toLatin1().constData());
  const QString renamed = QDir(dir).filePath(QStringLiteral("renamed.md"));
  QVERIFY(!QFileInfo::exists(created));
  QVERIFY(QFileInfo::exists(renamed));
  QFile reopened(renamed);
  QVERIFY(reopened.open(QIODevice::ReadOnly));
  QCOMPARE(QString::fromUtf8(reopened.readAll()), QStringLiteral("body\n"));
  reopened.close();

  QVERIFY2(typist::platform::deleteFile(renamed, &error), error.toLatin1().constData());
  QVERIFY(!QFileInfo::exists(renamed));
  QDir(dir).removeRecursively();
}

void EditorSpike::platformCreateRejectsDuplicateName() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_ops_b"));
  QDir().mkpath(dir);
  QString error;
  QVERIFY(typist::platform::createMarkdownFile(dir, QStringLiteral("dup.md"), &error));
  QVERIFY(!typist::platform::createMarkdownFile(dir, QStringLiteral("dup"), &error));
  QVERIFY(!error.isEmpty());
  QDir(dir).removeRecursively();
}

void EditorSpike::platformRenameAppendsMarkdownSuffix() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_ops_c"));
  QDir().mkpath(dir);
  const QString source = writeSearchFixture(dir, QStringLiteral("old.md"), QStringLiteral("# O\n"));
  QString error;
  QVERIFY2(typist::platform::renameFile(source, QStringLiteral("newname"), &error), error.toLatin1().constData());
  QVERIFY(QFileInfo::exists(QDir(dir).filePath(QStringLiteral("newname.md"))));
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeRefreshDetectsAddedFile() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_watch_a"));
  QDir().mkpath(dir);
  writeSearchFixture(dir, QStringLiteral("one.md"), QStringLiteral("# 1\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  QVERIFY(!tree.refreshIfChanged());

  writeSearchFixture(dir, QStringLiteral("two.md"), QStringLiteral("# 2\n"));
  QVERIFY(tree.refreshIfChanged());
  QVERIFY(tree.entryCount() == 2);
  QVERIFY(!tree.refreshIfChanged());
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeRefreshDetectsContentChange() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_watch_b"));
  QDir().mkpath(dir);
  writeSearchFixture(dir, QStringLiteral("note.md"), QStringLiteral("# short\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  QVERIFY(!tree.refreshIfChanged());

  writeSearchFixture(dir, QStringLiteral("note.md"), QStringLiteral("# much longer body\n\nmore text\n"));
  QVERIFY(tree.refreshIfChanged());
  QVERIFY(tree.entryCount() == 1);
  QDir(dir).removeRecursively();
}

void EditorSpike::fileTreeRefreshPreservesSelection() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_watch_c"));
  QDir().mkpath(dir);
  const QString keep = writeSearchFixture(dir, QStringLiteral("keep.md"), QStringLiteral("# K\n"));
  writeSearchFixture(dir, QStringLiteral("other.md"), QStringLiteral("# O\n"));

  typist::ui::FileTree tree;
  tree.setRoot(dir);
  tree.selectPath(keep);
  QCOMPARE(tree.selectedPath(), keep);

  writeSearchFixture(dir, QStringLiteral("added.md"), QStringLiteral("# A\n"));
  QVERIFY(tree.refreshIfChanged());
  QCOMPARE(tree.selectedPath(), keep);
  QDir(dir).removeRecursively();
}

void EditorSpike::dropTargetPicksMarkdownFile() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_drop"));
  QDir().mkpath(dir);
  const QString markdown = writeSearchFixture(dir, QStringLiteral("doc.md"), QStringLiteral("# D\n"));
  const QString text = QDir(dir).filePath(QStringLiteral("notes.txt"));
  QFile file(text);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("plain\n");
  file.close();

  const QList<QUrl> urls = {QUrl::fromLocalFile(text), QUrl::fromLocalFile(markdown)};
  QCOMPARE(typist::platform::firstMarkdownPath(urls), markdown);
  QVERIFY(typist::platform::firstMarkdownPath({QUrl::fromLocalFile(text)}).isEmpty());
  QVERIFY(typist::platform::firstMarkdownPath({QUrl(QStringLiteral("https://example.com/a.md"))}).isEmpty());
  QDir(dir).removeRecursively();
}

void EditorSpike::searchHighlightsEveryMatch() {
  typist::ui::SearchController search;
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("item one\n\nitem two\n\nitem three\n\n| item |\n| --- |\n| item |\n"), &document, &error));

  const QVector<QTextCursor> hits = search.allMatches(&document, QStringLiteral("item"));
  QVERIFY(hits.size() == 5);
  QVERIFY(hits.size() == search.countMatches(&document, QStringLiteral("item")));
  QVERIFY(hits.last().selectedText().contains(QStringLiteral("item")));
}

void EditorSpike::searchHighlightRespectsCaseSensitivity() {
  typist::ui::SearchController search;
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("Item item ITEM\n"), &document, &error));

  QVERIFY(search.allMatches(&document, QStringLiteral("item")).size() == 3);
  search.setCaseSensitive(true);
  QVERIFY(search.allMatches(&document, QStringLiteral("item")).size() == 1);
}

namespace {

QByteArray readWholeFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return QByteArray();
  const QByteArray data = file.readAll();
  file.close();
  return data;
}

int centralEntryCount(const QByteArray& data) {
  const int eocd = data.size() - 22;
  if (eocd < 0 || data.mid(eocd, 4) != QByteArrayLiteral("PK\x05\x06")) return -1;
  // EOCD: this-disk entry count is a 2-byte field at +8; total count at +10.
  return static_cast<int>(static_cast<unsigned char>(data.at(eocd + 8)) |
                          (static_cast<unsigned char>(data.at(eocd + 9)) << 8));
}
QStringList localEntryNames(const QByteArray& data) {
  QStringList names;
  int from = 0;
  while (true) {
    const int at = data.indexOf(QByteArrayLiteral("PK\x03\x04"), from);
    if (at < 0 || at + 30 > data.size()) break;
    const int nameLength = static_cast<unsigned char>(data.at(at + 26)) |
                           (static_cast<unsigned char>(data.at(at + 27)) << 8);
    const int extraLength = static_cast<unsigned char>(data.at(at + 28)) |
                            (static_cast<unsigned char>(data.at(at + 29)) << 8);
    // Local header layout: sizes sit at +18 (compressed) and +22 (uncompressed); names at +26/+28 lengths.
    const quint32 compressedSize = static_cast<quint32>(
        static_cast<unsigned char>(data.at(at + 18)) | (static_cast<unsigned char>(data.at(at + 19)) << 8) |
        (static_cast<unsigned char>(data.at(at + 20)) << 16) | (static_cast<unsigned char>(data.at(at + 21)) << 24));
    names.append(QString::fromUtf8(data.mid(at + 30, nameLength)));
    from = at + 30 + nameLength + extraLength + compressedSize;
  }
  return names;
}

}  // namespace

void EditorSpike::docxExportWritesEachPackageMemberOnce() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_once.docx"));
  QString error;
  QVERIFY(typist::platform::exportDocx(path, QStringLiteral("# Title\n\n- alpha\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n"), &error));

  const QByteArray data = readWholeFile(path);
  QVERIFY(data.size() > 400);
  QVERIFY(data.startsWith("PK\x03\x04"));
  // A duplicated member name corrupts the package: walk local headers and require unique names.
  const QStringList names = localEntryNames(data);
  QCOMPARE(names.size(), 6);
  QStringList expected;
  expected << QStringLiteral("[Content_Types].xml") << QStringLiteral("_rels/.rels")
           << QStringLiteral("word/_rels/document.xml.rels") << QStringLiteral("word/styles.xml")
           << QStringLiteral("word/numbering.xml") << QStringLiteral("word/document.xml");
  QCOMPARE(names, expected);
  QCOMPARE(centralEntryCount(data), 6);
  QFile::remove(path);
}

void EditorSpike::docxExportReportsHeadingTableAndTaskItem() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_parts.docx"));
  QString error;
  QVERIFY(typist::platform::exportDocx(path, QStringLiteral("# Title\n\n- [x] done\n\n| col |\n| --- |\n| v |\n"), &error));

  const QByteArray document = readWholeFile(path);
  QVERIFY(document.contains("Heading1"));
  QVERIFY(document.contains("<w:tbl>"));
  QVERIFY(document.contains("[x] done"));
  QVERIFY(typist::platform::docxSizeForBench(path, QStringLiteral("# Title\n")) > 400);
  QFile::remove(path);
}

void EditorSpike::docxExportFailsWhenPathUnwritable() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_missing_dir/out.docx"));
  QString error;
  QVERIFY(!typist::platform::exportDocx(path, QStringLiteral("# Title\n"), &error));
  QVERIFY(!error.isEmpty());
}

void EditorSpike::outlineListsHeadingLevelsAndPositions() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("# One\n\n## Two\n\n### Three\n\nbody text\n"), &doc, nullptr));
  const QVector<typist::ui::TocEntry> entries = typist::ui::outlineFromDocument(&doc);
  QCOMPARE(entries.size(), 3);
  QCOMPARE(entries.at(0).level, 1);
  QCOMPARE(entries.at(1).level, 2);
  QCOMPARE(entries.at(2).level, 3);
  QCOMPARE(entries.at(1).text, QStringLiteral("Two"));
  for (const typist::ui::TocEntry& entry : entries) {
    QCOMPARE(entry.position, doc.findBlockByNumber(entry.blockNumber).position());
  }
}

void EditorSpike::outlineCapsEntryCount() {
  QString source;
  for (int i = 0; i < 30; ++i) source += QStringLiteral("## Heading %1\n\n").arg(i);
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(source, &doc, nullptr));
  QCOMPARE(typist::ui::outlineFromDocument(&doc, 5).size(), 5);
  QCOMPARE(typist::ui::outlineFromDocument(&doc).size(), 30);
}

void EditorSpike::regexSearchCountsAnchoredHeadingLines() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("# A\n\n## B\n\nplain 12 words\n"), &doc, nullptr));
  typist::ui::SearchController search;
  search.setRegex(true);
  // Anchors apply to each block of the rendered document, where heading markers are styling, not text.
  QCOMPARE(search.countMatches(&doc, QStringLiteral("^A")), 1);
  QCOMPARE(search.countMatches(&doc, QStringLiteral("^B")), 1);
  QCOMPARE(search.countMatches(&doc, QStringLiteral("[0-9]+")), 1);
  QCOMPARE(search.allMatches(&doc, QStringLiteral("^A")).size(), 1);
}

void EditorSpike::regexAnchorsMatchSourceViewLines() {
  QTextDocument doc;
  doc.setPlainText(QStringLiteral("# A\n\n## B\n\nplain 12 words\n"));
  typist::ui::SearchController search;
  search.setRegex(true);
  QCOMPARE(search.countMatches(&doc, QStringLiteral("^#")), 2);
  QCOMPARE(search.allMatches(&doc, QStringLiteral("^#")).size(), 2);
}
void EditorSpike::regexSearchIgnoresInvalidPatterns() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("# A\n\nbody\n"), &doc, nullptr));
  typist::ui::SearchController search;
  search.setRegex(true);
  QCOMPARE(search.countMatches(&doc, QStringLiteral("(")), 0);
  QTextCursor cursor(&doc);
  QVERIFY(!search.selectNext(cursor, QStringLiteral("(")));
  QVERIFY(search.allMatches(&doc, QStringLiteral("(")).isEmpty());
}

void EditorSpike::regexSearchStepsAndWrapsAround() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("## first\n\n## second\n\nbody\n"), &doc, nullptr));
  typist::ui::SearchController search;
  search.setRegex(true);
  const QString pattern = QStringLiteral("first|second");
  const QVector<typist::ui::TocEntry> headings = typist::ui::outlineFromDocument(&doc);
  QCOMPARE(headings.size(), 2);
  const int firstAt = headings.at(0).position;
  const int secondAt = headings.at(1).position;
  QVERIFY(firstAt < secondAt);
  QTextCursor cursor(&doc);
  QVERIFY(search.selectNext(cursor, pattern));
  QCOMPARE(cursor.selectionStart(), firstAt);
  QVERIFY(search.selectNext(cursor, pattern));
  QCOMPARE(cursor.selectionStart(), secondAt);
  QVERIFY(search.selectNext(cursor, pattern));
  QCOMPARE(cursor.selectionStart(), firstAt);
  QVERIFY(search.selectPrevious(cursor, pattern));
  QCOMPARE(cursor.selectionStart(), secondAt);
}
void EditorSpike::regexSearchHonoursCaseFlag() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("Hello\n\nhello again\n"), &doc, nullptr));
  typist::ui::SearchController search;
  search.setRegex(true);
  QCOMPARE(search.countMatches(&doc, QStringLiteral("hello")), 2);
  search.setCaseSensitive(true);
  QCOMPARE(search.countMatches(&doc, QStringLiteral("hello")), 1);
}

void EditorSpike::recentFilesKeepNewestFirstAndCap() {
  // The store is the native one (registry on Windows), so clear it for a deterministic run.
  QSettings store(QStringLiteral("Typist"), QStringLiteral("Typist"));
  store.remove(QStringLiteral("recentFiles"));
  store.sync();
  typist::platform::addRecentFile(QStringLiteral("/a.md"));
  typist::platform::addRecentFile(QStringLiteral("/b.md"));
  typist::platform::addRecentFile(QStringLiteral("/a.md"));
  const QStringList stored = typist::platform::recentFiles();
  QVERIFY2(stored.size() == 2, qPrintable(stored.join(QStringLiteral(";"))));
  QCOMPARE(stored.first(), QStringLiteral("/a.md"));
  for (int i = 0; i < 12; ++i) typist::platform::addRecentFile(QStringLiteral("/f%1.md").arg(i));
  QCOMPARE(typist::platform::recentFiles().size(), 8);
  typist::platform::removeRecentFile(QStringLiteral("/f11.md"));
  QVERIFY(!typist::platform::recentFiles().contains(QStringLiteral("/f11.md")));
  store.remove(QStringLiteral("recentFiles"));
  store.sync();
  QVERIFY(typist::platform::recentFiles().isEmpty());
}

void EditorSpike::recoveryStoreRoundTripsThroughEnvOverride() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  qputenv("TYPIST_RECOVERY_DIR", QDir::tempPath() == dir.path() ? QByteArray() : dir.path().toLocal8Bit());
  QVERIFY(typist::ui::recoveryDir() == dir.path());
  QVERIFY(typist::ui::writeDraft(QStringLiteral("/doc.md"), QStringLiteral("# Draft\n\nbody")));
  QString text;
  QVERIFY(typist::ui::readDraft(QStringLiteral("/doc.md"), &text));
  QCOMPARE(text, QStringLiteral("# Draft\n\nbody"));
  QCOMPARE(typist::ui::draftPaths().size(), 1);
  QVERIFY(typist::ui::removeDraft(QStringLiteral("/doc.md")));
  QVERIFY(!typist::ui::removeDraft(QStringLiteral("/doc.md")));
  QVERIFY(!typist::ui::readDraft(QStringLiteral("/missing.md"), &text));
}

void EditorSpike::recoveryStoreKeysSeparateUntitledAndPaths() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  qputenv("TYPIST_RECOVERY_DIR", dir.path().toLocal8Bit());
  QCOMPARE(typist::ui::recoveryKeyFor(QString()), QStringLiteral("untitled"));
  QVERIFY(typist::ui::recoveryKeyFor(QStringLiteral("/a.md")) != typist::ui::recoveryKeyFor(QStringLiteral("/b.md")));
  QVERIFY(typist::ui::writeDraft(QString(), QStringLiteral("untitled draft")));
  QString text;
  QVERIFY(typist::ui::readDraft(QString(), &text));
  QCOMPARE(text, QStringLiteral("untitled draft"));
  QVERIFY(!typist::ui::readDraft(QStringLiteral("/a.md"), &text));
}

void EditorSpike::conflictDecisionProtectsUnsavedEdits() {
  QCOMPARE(typist::ui::decideConflict(true, false, true), typist::ui::ConflictChoice::KeepEditor);
  QCOMPARE(typist::ui::decideConflict(false, true, true), typist::ui::ConflictChoice::ReloadDisk);
  QCOMPARE(typist::ui::decideConflict(true, true, false), typist::ui::ConflictChoice::KeepEditor);
  QCOMPARE(typist::ui::decideConflict(true, true, true), typist::ui::ConflictChoice::ReloadDisk);
}

void EditorSpike::folderSearchRunnerReportsHitsOffUiThread() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  for (int i = 0; i < 5; ++i) {
    QFile file(dir.path() + QStringLiteral("/doc%1.md").arg(i));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("needle line for scan\n");
    file.close();
  }
  typist::platform::FolderSearchRunner runner;
  QSignalSpy spy(&runner, &typist::platform::FolderSearchRunner::resultsReady);
  typist::platform::SearchRequest request;
  request.rootDir = dir.path();
  request.pattern = QStringLiteral("needle");
  runner.start(request);
  QVERIFY(spy.wait(5000));
  QCOMPARE(spy.count(), 1);
  const QVector<typist::platform::SearchHit> hits = spy.at(0).at(0).value<QVector<typist::platform::SearchHit>>();
  QCOMPARE(hits.size(), 5);
  QVERIFY(spy.at(0).at(1).toBool() == false);
  QVERIFY(runner.isRunning() == false);
}

void EditorSpike::cancellableScanStopsWhenFlagIsSet() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QFile file(dir.path() + QStringLiteral("/doc.md"));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("needle\n");
  file.close();
  typist::platform::SearchRequest request;
  request.rootDir = dir.path();
  request.pattern = QStringLiteral("needle");
  std::atomic<bool> cancelled{true};
  bool wasCancelled = false;
  QString error;
  const QVector<typist::platform::SearchHit> hits =
      typist::platform::searchFilesCancellable(request, &error, cancelled, &wasCancelled);
  QVERIFY(hits.isEmpty());
  QVERIFY(wasCancelled);
}

void EditorSpike::translationCandidatesFollowLocaleLanguage() {
  const QStringList candidates = typist::ui::translationCandidates(QStringLiteral("zh_CN"));
  QCOMPARE(candidates.size(), 4);
  QVERIFY(candidates.first().endsWith(QStringLiteral("typist_zh.qm")));
  QVERIFY(typist::ui::translationCandidates(QStringLiteral("en_US")).isEmpty());
  QVERIFY(typist::ui::translationCandidates(QStringLiteral("de_DE")).first().endsWith(QStringLiteral("typist_de.qm")));
}

void EditorSpike::translationFileCoversMainWindowStrings() {
  const QString tsPath = qEnvironmentVariable("TYPIST_TS_FILE");
  const QString sourcePath = qEnvironmentVariable("TYPIST_UI_SOURCE");
  if (tsPath.isEmpty() || sourcePath.isEmpty()) QSKIP("Translation consistency check needs TYPIST_TS_FILE and TYPIST_UI_SOURCE.");
  QFile tsFile(tsPath);
  QVERIFY(tsFile.open(QIODevice::ReadOnly));
  QFile sourceFile(sourcePath);
  QVERIFY(sourceFile.open(QIODevice::ReadOnly));
  const QString sourceCode = QString::fromUtf8(sourceFile.readAll());
  sourceFile.close();
  QXmlStreamReader xml(&tsFile);
  int messages = 0;
  bool inMainWindow = false;
  QString source;
  QString translation;
  QString currentElement;
  while (!xml.atEnd()) {
    const QXmlStreamReader::TokenType token = xml.readNext();
    if (xml.hasError()) {
      QFAIL(qPrintable(QStringLiteral("Translation file is not well-formed XML: ") + xml.errorString()));
    }
    if (token == QXmlStreamReader::StartElement) {
      currentElement = xml.name().toString();
      if (currentElement == QStringLiteral("message")) {
        source.clear();
        translation.clear();
      }
    } else if (token == QXmlStreamReader::EndElement) {
      if (xml.name() == QStringLiteral("message")) {
        QVERIFY(inMainWindow);
        QVERIFY(!source.isEmpty());
        QVERIFY(!translation.isEmpty());
        QVERIFY(sourceCode.contains(QStringLiteral("tr(\"") + source + QStringLiteral("\")")));
        ++messages;
      }
      currentElement.clear();
    } else if (token == QXmlStreamReader::Characters && !xml.isWhitespace()) {
      if (currentElement == QStringLiteral("name")) inMainWindow = xml.text().toString() == QStringLiteral("MainWindow");
      else if (currentElement == QStringLiteral("source")) source = xml.text().toString();
      else if (currentElement == QStringLiteral("translation")) translation = xml.text().toString();
    }
  }
  QSet<QString> uniqueTr;
  int cursor = sourceCode.indexOf(QStringLiteral("tr(\""));
  while (cursor >= 0) {
    const QString rest = sourceCode.mid(cursor + 5);
    const int closing = rest.indexOf(QChar(34));
    if (closing >= 0) uniqueTr.insert(rest.left(closing));
    cursor = sourceCode.indexOf(QStringLiteral("tr(\""), cursor + 5 + closing);
  }
  QCOMPARE(messages, uniqueTr.size());
  QVERIFY(messages >= 30);
  tsFile.close();
  QVERIFY(messages >= 30);
}

void EditorSpike::translatorFallsBackWhenCatalogMissing() {
  QTranslator translator;
  QVERIFY(!typist::ui::installTranslatorForLocale(&translator, QStringLiteral("zz_ZZ")));
}

void EditorSpike::systemThemeParsesPlatformHints() {
  QCOMPARE(typist::platform::darkHintFromAppsUseLightTheme(0), typist::platform::DarkHint::Dark);
  QCOMPARE(typist::platform::darkHintFromAppsUseLightTheme(1), typist::platform::DarkHint::Light);
  QCOMPARE(typist::platform::darkHintFromGtkTheme(QStringLiteral("Adwaita:dark")), typist::platform::DarkHint::Dark);
  QCOMPARE(typist::platform::darkHintFromGtkTheme(QStringLiteral("Yaru-dark")), typist::platform::DarkHint::Dark);
  QCOMPARE(typist::platform::darkHintFromGtkTheme(QStringLiteral("Adwaita")), typist::platform::DarkHint::Light);
  QCOMPARE(typist::platform::darkHintFromGtkTheme(QString()), typist::platform::DarkHint::Unknown);
}

void EditorSpike::systemThemeReadsWindowsRegistryHint() {
#if defined(Q_OS_WIN)
  QVERIFY(typist::platform::nativeDarkHint() != typist::platform::DarkHint::Unknown);
#else
  QSKIP("Native dark hint is only readable on Windows in this suite.");
#endif
}

QTEST_MAIN(EditorSpike)

#include "roundtrip_spike.moc"