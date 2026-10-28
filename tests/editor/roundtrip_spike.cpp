#include "AutoSave.h"
#include "Conflict.h"
#include "DocxWriter.h"
#include "FileOps.h"
#include "FileSearch.h"
#include "FileTree.h"
#include "FileWriter.h"
#include "FolderSearchRunner.h"
#include "Localization.h"
#include "PdfWriter.h"
#include "RecoveryStore.h"
#include "Search.h"
#include "Settings.h"
#include "SystemTheme.h"
#include "Theme.h"
#include "Toc.h"
#include "UpdateChecker.h"
#include "ViewSwitch.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAbstractTextDocumentLayout>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QUrl>
#include <QtTest>
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
  void docxTableGridWidthsFollowContent();
  void pdfTallBlockSplitsAtLineBoundaries();
  void toggleTaskCheckboxFlipsRenderedState();
  void toggleTaskCheckboxIgnoresPlainBlocks();
  void toggleTaskCheckboxIsOneUndoStep();
  void docxExportWritesHyperlinkRelationships();
  void inlineImageRendersAndRoundTrips();
  void nestedListsRoundTripAtDepthFour();
  void pdfLongTablePaginatesRowByRow();
  void headingInlineStylesRoundTrip();
  void pdfParagraphChunkOrphanRules();
  void pdfPageStartBlockProbe();
  void pdfHeaderRowForContinuationPages();
  void pdfContinuationPagesRepeatTableHeader();
  void taskListRendersCheckboxGlyphs();
  void sidebarExpandStateSurvivesRebuild();
  void docxExportEmbedsImageDrawing();
  void docxExportFallsBackWhenImageMissing();
  void updateFeedParsesVersionAndAssets();
  void updateFeedRejectsMalformedOrIncompleteFeed();
  void versionComparisonAcrossMinorAndPatch();
  void inlineRunsRoundTripThroughMarkdownSerialize();
  void inlineRunsRoundTripThroughDocument();
  void renderedDocumentCarriesInlineFormats();
  void pasteMarkdownInsertsBlocksAsOneUndoStep();
  void pasteMarkdownKeepsTableAndTaskItem();
  void imageLinkSurvivesDocumentRoundTrip();
  void docxInlineEmphasisWritesRunProperties();
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
      "headings.md",       "paragraphs.md",          "lists.md",
      "nested_ordered.md", "nested_wide.md",         "thematic.md",
      "code.md",           "code_info.md",           "fences.md",
      "gfm/table.md",      "gfm/table_then_para.md", "gfm/table_escapes.md",
      "gfm/tasklist.md",
  };
  for (const std::string& name : files) {
    const auto parsed = typist::core::parseMarkdown(read(name));
    QVERIFY2(parsed.ok, name.c_str());

    QTextDocument document;
    typist::editor::buildDocument(parsed.blocks, &document);
    const auto reparsed =
        typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
    QVERIFY2(reparsed.ok, name.c_str());
    QVERIFY2(typist::core::astEqual(parsed.blocks, reparsed.blocks), name.c_str());
  }
}

void EditorSpike::taskListRoundTripsThroughDocument() {
  const auto parsed = typist::core::parseMarkdown(read("gfm/tasklist.md"));
  QVERIFY(parsed.ok);

  QTextDocument document;
  typist::editor::buildDocument(parsed.blocks, &document);
  const auto reparsed =
      typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
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
  QVERIFY2(typist::editor::loadMarkdown(source, &document, &error),
           error.isEmpty() ? "parse failed" : qPrintable(error));
  QVERIFY(!document.isUndoAvailable());

  const auto reparsed =
      typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
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
  QVERIFY(
      typist::editor::loadMarkdown(QStringLiteral("# Title\n\nBody text\n"), &document, &error));

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
  if (!typist::platform::pdfExportAvailable())
    QSKIP("Qt PrintSupport is not available in this build");

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
  const auto fromRendered =
      typist::core::parseMarkdown(typist::editor::serializeDocument(view.rendered()).toStdString());
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

  QVERIFY(typist::ui::editorStyleSheet(typist::ui::Theme::Light) !=
          typist::ui::editorStyleSheet(typist::ui::Theme::Dark));
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
  QVERIFY2(typist::platform::writeAtomic(path, QStringLiteral("# One\n"), &error),
           error.toLatin1().constData());
  QVERIFY2(typist::platform::writeAtomic(path, QStringLiteral("# Two\n"), &error),
           error.toLatin1().constData());

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
  QVERIFY2(
      typist::platform::writeAtomic(path, typist::editor::serializeDocument(&document), &error),
      error.toLatin1().constData());

  QFile file(path);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const auto reparsed =
      typist::core::parseMarkdown(QString::fromUtf8(file.readAll()).toStdString());
  QVERIFY(reparsed.ok);
  QVERIFY(typist::core::astEqual(parsed.blocks, reparsed.blocks));
  file.close();
  QVERIFY(QFile::remove(path));
}

void EditorSpike::pdfExportPaginatesLongDocument() {
  if (!typist::platform::pdfExportAvailable()) QSKIP("PDF export unavailable in this build");
  QString source;
  for (int i = 0; i < 200; ++i) {
    source +=
        QStringLiteral("Paragraph %1 with enough prose to occupy a couple of lines.\n\n").arg(i);
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
    source +=
        QStringLiteral("Paragraph %1 with enough prose to occupy a couple of lines.\n\n").arg(i);
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
  QVERIFY(view.loadMarkdownIntoBoth(
      QStringLiteral("- first item\n- second item\n  - nested item\n- third item\n"), &error));
  typist::ui::SearchController search;
  QVERIFY(search.countMatches(view.rendered(), QStringLiteral("item")) == 4);
  QVERIFY(search.countMatches(view.source(), QStringLiteral("item")) == 4);
}

void EditorSpike::searchWrapsAroundAtBothEnds() {
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("alpha item beta item gamma"), &document,
                                       &error));
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
  QVERIFY(typist::editor::loadMarkdown(
      QStringLiteral("| cell | value |\n| --- | --- |\n| aaa | bbb |\n"), &document, &error));
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
  writeSearchFixture(QDir(dir).filePath(QStringLiteral(".git")), QStringLiteral("hook.md"),
                     QStringLiteral("needle hidden\n"));

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
  writeSearchFixture(QDir(dir).filePath(QStringLiteral("b")), QStringLiteral("c.md"),
                     QStringLiteral("# C\n"));
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
  writeSearchFixture(QDir(dir).filePath(QStringLiteral(".git")), QStringLiteral("hook.md"),
                     QStringLiteral("# H\n"));

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
  const QString target =
      writeSearchFixture(dir, QStringLiteral("target.md"), QStringLiteral("# T\n"));
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
  writeSearchFixture(QDir(dir).filePath(QStringLiteral("sub")), QStringLiteral("deep.md"),
                     QStringLiteral("# D\n"));

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
  writeSearchFixture(QDir(dir).filePath(QStringLiteral("a/b")), QStringLiteral("deep.md"),
                     QStringLiteral("# D\n"));

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
  QVERIFY2(typist::platform::createMarkdownFile(dir, QStringLiteral("second"), &error),
           error.toLatin1().constData());
  tree.setRoot(dir);
  QVERIFY(tree.entryCount() == 2);
  QVERIFY(tree.entryPath(1).endsWith(QStringLiteral("second.md")));
  QDir(dir).removeRecursively();
}

void EditorSpike::platformFileOpsCreateRenameDelete() {
  const QString dir = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_ops_a"));
  QDir().mkpath(dir);
  QString error;
  QVERIFY2(typist::platform::createMarkdownFile(dir, QStringLiteral("note"), &error),
           error.toLatin1().constData());
  const QString created = QDir(dir).filePath(QStringLiteral("note.md"));
  QVERIFY(QFileInfo::exists(created));

  QFile file(created);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("body\n");
  file.close();

  QVERIFY2(typist::platform::renameFile(created, QStringLiteral("renamed"), &error),
           error.toLatin1().constData());
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
  QVERIFY2(typist::platform::renameFile(source, QStringLiteral("newname"), &error),
           error.toLatin1().constData());
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

  writeSearchFixture(dir, QStringLiteral("note.md"),
                     QStringLiteral("# much longer body\n\nmore text\n"));
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
  const QString markdown =
      writeSearchFixture(dir, QStringLiteral("doc.md"), QStringLiteral("# D\n"));
  const QString text = QDir(dir).filePath(QStringLiteral("notes.txt"));
  QFile file(text);
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("plain\n");
  file.close();

  const QList<QUrl> urls = {QUrl::fromLocalFile(text), QUrl::fromLocalFile(markdown)};
  QCOMPARE(typist::platform::firstMarkdownPath(urls), markdown);
  QVERIFY(typist::platform::firstMarkdownPath({QUrl::fromLocalFile(text)}).isEmpty());
  QVERIFY(typist::platform::firstMarkdownPath({QUrl(QStringLiteral("https://example.com/a.md"))})
              .isEmpty());
  QDir(dir).removeRecursively();
}

void EditorSpike::searchHighlightsEveryMatch() {
  typist::ui::SearchController search;
  QTextDocument document;
  QString error;
  QVERIFY(typist::editor::loadMarkdown(
      QStringLiteral("item one\n\nitem two\n\nitem three\n\n| item |\n| --- |\n| item |\n"),
      &document, &error));

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
    // Local header layout: sizes sit at +18 (compressed) and +22 (uncompressed); names at +26/+28
    // lengths.
    const quint32 compressedSize =
        static_cast<quint32>(static_cast<unsigned char>(data.at(at + 18)) |
                             (static_cast<unsigned char>(data.at(at + 19)) << 8) |
                             (static_cast<unsigned char>(data.at(at + 20)) << 16) |
                             (static_cast<unsigned char>(data.at(at + 21)) << 24));
    names.append(QString::fromUtf8(data.mid(at + 30, nameLength)));
    from = at + 30 + nameLength + extraLength + compressedSize;
  }
  return names;
}

}  // namespace

void EditorSpike::docxExportWritesEachPackageMemberOnce() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_once.docx"));
  QString error;
  QVERIFY(typist::platform::exportDocx(
      path, QStringLiteral("# Title\n\n- alpha\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n"), &error));

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
  QVERIFY(typist::platform::exportDocx(
      path, QStringLiteral("# Title\n\n- [x] done\n\n| col |\n| --- |\n| v |\n"), &error));

  const QByteArray document = readWholeFile(path);
  QVERIFY(document.contains("Heading1"));
  QVERIFY(document.contains("<w:tbl>"));
  const QByteArray glyph = QString(QChar(0x2611)).toUtf8();
  const int glyphAt = document.indexOf(glyph);
  const int escAt = document.indexOf("&#");
  QVERIFY2(glyphAt >= 0, QStringLiteral("glyph=%1 escaped=%2 head=%3")
                             .arg(glyphAt)
                             .arg(escAt)
                             .arg(QString::fromLatin1(document.left(60)))
                             .toUtf8()
                             .constData());
  QVERIFY(typist::platform::docxSizeForBench(path, QStringLiteral("# Title\n")) > 400);
  QFile::remove(path);
}

void EditorSpike::docxTableGridWidthsFollowContent() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_widths.docx"));
  QString error;
  QVERIFY(typist::platform::exportDocx(
      path,
      QStringLiteral(
          "| short | much longer cell text here | mid |\n| --- | --- | --- |\n| a | b | c |\n"),
      &error));
  const QByteArray data = readWholeFile(path);
  QFile::remove(path);

  QVector<int> widths;
  int offset = 0;
  const QByteArray marker = QByteArrayLiteral("<w:gridCol w:w=\"");
  while ((offset = data.indexOf(marker, offset)) != -1) {
    const int start = offset + marker.size();
    int end = start;
    while (end < data.size() && data[end] >= '0' && data[end] <= '9') ++end;
    widths.append(QByteArray(data.constData() + start, end - start).toInt());
    offset = end;
  }
  QCOMPARE(widths.size(), 3);
  QVERIFY(widths.at(1) > widths.at(0));
  QVERIFY(widths.at(1) > widths.at(2));
  int total = 0;
  for (int width : widths) total += width;
  QVERIFY(total >= 9400 && total <= 9700);
}

void EditorSpike::pdfTallBlockSplitsAtLineBoundaries() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_pdf_tall.pdf"));
  QStringList lines;
  for (int i = 0; i < 600; ++i) lines << QStringLiteral("line %1 of a long paragraph").arg(i);
  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(lines.join(QStringLiteral("\n")), &document, nullptr));
  // Keep the page-count bounds independent of the platform's default font and DPI.
  QFont font(QStringLiteral("Arial"));
  font.setPixelSize(12);
  document.setDefaultFont(font);
  QTextCursor fontCursor(&document);
  fontCursor.select(QTextCursor::Document);
  QTextCharFormat fixedFont;
  fixedFont.setFont(font);
  fontCursor.mergeCharFormat(fixedFont);

  const std::unique_ptr<QTextDocument> copy(document.clone());
  copy->setPageSize(QSizeF(719.0, 935.0));
  copy->documentLayout()->documentSize();
  const QTextBlock paragraph = copy->firstBlock().next();
  const double height = copy->documentLayout()->blockBoundingRect(paragraph).height();
  QVERIFY2(height > 935.0, QStringLiteral("paragraph height=%1 expected taller than the page")
                               .arg(height)
                               .toUtf8()
                               .constData());
  QVERIFY(paragraph.layout() != nullptr && paragraph.layout()->lineCount() > 1);

  QString error;
  QVERIFY2(typist::platform::exportPdf(path, &document, &error), qPrintable(error));
  const int pages = pdfPageCount(path);
  QFile::remove(path);
  QVERIFY2(pages >= 3, QStringLiteral("expected the tall block to span pages, got %1")
                           .arg(pages)
                           .toUtf8()
                           .constData());
  QVERIFY2(pages <= 6, qPrintable(QStringLiteral("pages=%1 lines=%2 height=%3 font=%4")
                                      .arg(pages)
                                      .arg(paragraph.layout()->lineCount())
                                      .arg(height)
                                      .arg(paragraph.layout()->font().toString())));
}

void EditorSpike::toggleTaskCheckboxFlipsRenderedState() {
  QTextDocument document;
  QVERIFY(
      typist::editor::loadMarkdown(QStringLiteral("- [x] done\n- [ ] todo\n"), &document, nullptr));

  int checkedBlock = -1;
  int uncheckedBlock = -1;
  for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
    bool checked = false;
    if (!typist::editor::taskCheckboxState(&document, block.blockNumber(), &checked)) continue;
    if (checked)
      checkedBlock = block.blockNumber();
    else
      uncheckedBlock = block.blockNumber();
  }
  QVERIFY(checkedBlock >= 0);
  QVERIFY(uncheckedBlock >= 0);
  QVERIFY(typist::editor::toggleTaskCheckbox(&document, checkedBlock));
  QVERIFY(typist::editor::toggleTaskCheckbox(&document, uncheckedBlock));
  const QString serialized = typist::editor::serializeDocument(&document);
  QVERIFY(serialized.contains(QStringLiteral("- [ ] done")));
  QVERIFY(serialized.contains(QStringLiteral("- [x] todo")));
  QVERIFY(typist::editor::toggleTaskCheckbox(&document, checkedBlock));
  QVERIFY(typist::editor::serializeDocument(&document).contains(QStringLiteral("- [x] done")));
}

void EditorSpike::toggleTaskCheckboxIgnoresPlainBlocks() {
  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("para\n"), &document, nullptr));
  const QString before = typist::editor::serializeDocument(&document);
  QVERIFY(!typist::editor::toggleTaskCheckbox(&document, 0));
  QVERIFY(!typist::editor::toggleTaskCheckbox(&document, 99));
  QVERIFY(!typist::editor::toggleTaskCheckbox(nullptr, 0));
  QCOMPARE(typist::editor::serializeDocument(&document), before);
}

void EditorSpike::toggleTaskCheckboxIsOneUndoStep() {
  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("- [x] done\n"), &document, nullptr));
  const QString before = typist::editor::serializeDocument(&document);
  int taskBlock = -1;
  for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
    bool checked = false;
    if (typist::editor::taskCheckboxState(&document, block.blockNumber(), &checked) && checked)
      taskBlock = block.blockNumber();
  }
  QVERIFY(typist::editor::toggleTaskCheckbox(&document, taskBlock));
  QVERIFY(typist::editor::serializeDocument(&document).contains(QStringLiteral("- [ ] done")));
  document.undo();
  QCOMPARE(typist::editor::serializeDocument(&document), before);
}

void EditorSpike::updateFeedParsesVersionAndAssets() {
  const QByteArray json = QByteArrayLiteral(
      "{\"version\":\"0.2.0\",\"assets\":[{\"name\":\"Typist-win64.zip\",\"url\":\"https://"
      "example.org/Typist-win64.zip\"},{\"name\":\"Typist-0.2.0.deb\",\"url\":\"https://"
      "example.org/Typist-0.2.0.deb\"}]}");
  typist::ui::UpdateFeed feed;
  QString error;
  QVERIFY2(typist::ui::parseUpdateFeed(json, &feed, &error), error.toUtf8().constData());
  QCOMPARE(feed.version, QStringLiteral("0.2.0"));
  QCOMPARE(feed.assets.size(), 2);
  QCOMPARE(feed.assets.at(0).toString(), QStringLiteral("https://example.org/Typist-win64.zip"));
}

void EditorSpike::updateFeedRejectsMalformedOrIncompleteFeed() {
  typist::ui::UpdateFeed feed;
  QString error;
  QVERIFY(!typist::ui::parseUpdateFeed(QByteArrayLiteral("not json"), &feed, &error));
  QVERIFY(error.contains(QStringLiteral("JSON")));
  QVERIFY(!typist::ui::parseUpdateFeed(QByteArrayLiteral("{\"assets\":[]}"), &feed, &error));
  QVERIFY(error.contains(QStringLiteral("version")));
  QVERIFY(!typist::ui::parseUpdateFeed(QByteArrayLiteral("{\"version\":\"0.2.0\",\"assets\":[]}"),
                                       &feed, &error));
  QVERIFY(error.contains(QStringLiteral("assets")));
}

void EditorSpike::versionComparisonAcrossMinorAndPatch() {
  QVERIFY(typist::ui::versionIsNewer(QStringLiteral("0.2.0"), QStringLiteral("0.1.9")));
  QVERIFY(typist::ui::versionIsNewer(QStringLiteral("0.0.10"), QStringLiteral("0.0.9")));
  QVERIFY(typist::ui::versionIsNewer(QStringLiteral("1.10.0"), QStringLiteral("1.9.0")));
  QVERIFY(!typist::ui::versionIsNewer(QStringLiteral("0.1.0"), QStringLiteral("0.1.0")));
  QVERIFY(!typist::ui::versionIsNewer(QStringLiteral("0.1.0"), QStringLiteral("0.2.0")));
  QVERIFY(!typist::ui::versionIsNewer(QStringLiteral("0.2.0-beta1"), QStringLiteral("0.2.0")));
}

void EditorSpike::pdfContinuationPagesRepeatTableHeader() {
  if (!typist::platform::pdfExportAvailable()) {
    QSKIP("PDF export is not available in this build");
  }
  QString source;
  source += QStringLiteral("| col a | col b |\n|---|---|\n");
  for (int i = 0; i < 100; ++i) {
    source += QStringLiteral("| row %1 | value %1 |\n").arg(i);
  }
  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));

  const QString offPath = QDir::tempPath() + QStringLiteral("/typist_header_off.pdf");
  const QString onPath = QDir::tempPath() + QStringLiteral("/typist_header_on.pdf");
  typist::platform::PdfPageOptions off;
  off.repeatTableHeader = false;
  QString error;
  QVERIFY(typist::platform::exportPdf(offPath, &document, off, &error));
  QVERIFY(
      typist::platform::exportPdf(onPath, &document, typist::platform::PdfPageOptions(), &error));
  const int offPages = pdfPageCount(offPath);
  const int onPages = pdfPageCount(onPath);
  QVERIFY2(offPages >= 2, qPrintable(QStringLiteral("off pages=%1").arg(offPages)));
  QVERIFY2(onPages >= offPages,
           qPrintable(QStringLiteral("off=%1 on=%2").arg(offPages).arg(onPages)));
  const qint64 offBytes = QFile(offPath).size();
  const qint64 onBytes = QFile(onPath).size();
  QVERIFY2(onBytes > offBytes,
           qPrintable(QStringLiteral("off bytes=%1 on bytes=%2").arg(offBytes).arg(onBytes)));
  QFile::remove(offPath);
  QFile::remove(onPath);
}

void EditorSpike::pdfParagraphChunkOrphanRules() {
  const auto covers = [](const QVector<QPair<int, int>>& chunks, int lineCount) {
    int start = 0;
    for (const QPair<int, int>& chunk : chunks) {
      if (chunk.first != start) return false;
      if (chunk.second <= chunk.first) return false;
      start = chunk.second;
    }
    return start == lineCount;
  };
  const QVector<QPair<int, int>> even = typist::platform::splitParagraphChunks(60, 30);
  QCOMPARE(even.size(), 2);
  QVERIFY(covers(even, 60));
  const QVector<QPair<int, int>> tail = typist::platform::splitParagraphChunks(31, 30);
  QCOMPARE(tail.size(), 2);
  QCOMPARE(tail.at(0).second - tail.at(0).first, 29);
  QCOMPARE(tail.at(1).second - tail.at(1).first, 2);
  QVERIFY(covers(tail, 31));
  const QVector<QPair<int, int>> twice = typist::platform::splitParagraphChunks(61, 30);
  QVERIFY(covers(twice, 61));
  for (const QPair<int, int>& chunk : twice) {
    QVERIFY(chunk.second - chunk.first > 1);
  }
  const QVector<QPair<int, int>> single = typist::platform::splitParagraphChunks(1, 30);
  QCOMPARE(single.size(), 1);
  QCOMPARE(typist::platform::splitParagraphChunks(0, 30).size(), 0);
  QVERIFY(typist::platform::splitParagraphChunks(40, 1).size() >= 2);
}

void EditorSpike::pdfPageStartBlockProbe() {
  QString source;
  source += QStringLiteral("| col a | col b |\n|---|---|\n");
  for (int i = 0; i < 100; ++i) {
    source += QStringLiteral("| row %1 | value %1 |\n").arg(i);
  }
  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
  const double contentHeight = 974.0;
  document.setPageSize(QSizeF(595.0, contentHeight));
  document.documentLayout()->documentSize();
  int blockNumber = 0;
  int page = 1;
  QString report;
  while (blockNumber < document.blockCount()) {
    QTextBlock block = document.findBlockByNumber(blockNumber);
    const QRectF blockRect = document.documentLayout()->blockBoundingRect(block);
    const double base = blockRect.y();
    const bool inTable = QTextCursor(block).currentTable() != nullptr;
    const bool hasHeader = !typist::platform::tableHeaderRectFor(&document, block).isNull();
    report += QStringLiteral("p%1 inTable=%2 header=%3\n")
                  .arg(page)
                  .arg(inTable ? 1 : 0)
                  .arg(hasHeader ? 1 : 0);
    if (page > 1 && inTable) {
      QVERIFY2(hasHeader, qPrintable(report));
    }
    int nextBlock = blockNumber;
    QTextBlock scan = block;
    while (scan.isValid()) {
      const QRectF rect = document.documentLayout()->blockBoundingRect(scan);
      if (nextBlock > blockNumber && (rect.y() - base) + rect.height() > contentHeight) break;
      ++nextBlock;
      scan = scan.next();
    }
    blockNumber = nextBlock;
    ++page;
  }
  QVERIFY(page > 2);
}

void EditorSpike::pdfHeaderRowForContinuationPages() {
  QString source;
  source += QStringLiteral("| col a | col b |\n|---|---|\n");
  for (int i = 0; i < 60; ++i) {
    source += QStringLiteral("| row %1 | value %1 |\n").arg(i);
  }
  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
  document.setPageSize(QSizeF(595.0, 300.0));
  document.documentLayout()->documentSize();

  int headerCell = -1;
  int deepCell = -1;
  for (int i = 0; i < document.blockCount(); ++i) {
    QTextBlock block = document.findBlockByNumber(i);
    QTextTable* table = QTextCursor(block).currentTable();
    if (table == nullptr) continue;
    if (headerCell < 0) headerCell = i;
    if (block.blockNumber() > table->cellAt(0, 0).firstCursorPosition().block().blockNumber() + 4)
      deepCell = i;
  }
  QVERIFY(headerCell >= 0);
  QVERIFY(deepCell >= 0);

  const QRectF headerRect =
      typist::platform::tableHeaderRectFor(&document, document.findBlockByNumber(deepCell));
  QVERIFY2(!headerRect.isNull(), "continuation blocks report no header row");
  QVERIFY2(headerRect.height() > 0.0 && headerRect.height() < 300.0,
           qPrintable(QStringLiteral("header height=%1").arg(headerRect.height())));
  const QRectF headerCellRect =
      document.documentLayout()->blockBoundingRect(document.findBlockByNumber(headerCell));
  QVERIFY2(qAbs(headerRect.y() - headerCellRect.y()) < 1.0,
           qPrintable(QStringLiteral("header rect y=%1 cell y=%2")
                          .arg(headerRect.y())
                          .arg(headerCellRect.y())));
  QVERIFY2(typist::platform::tableHeaderRectFor(&document, document.findBlockByNumber(headerCell))
               .isNull(),
           "first row reported a header to repeat");
}

void EditorSpike::taskListRendersCheckboxGlyphs() {
  const QString source = QStringLiteral("- [x] done\n- [ ] todo\n");
  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));

  int checkedBlock = -1;
  int uncheckedBlock = -1;
  bool checked = false;
  for (int i = 0; i < document.blockCount(); ++i) {
    if (!typist::editor::taskCheckboxState(&document, i, &checked)) continue;
    if (checked)
      checkedBlock = i;
    else
      uncheckedBlock = i;
  }
  QVERIFY(checkedBlock >= 0);
  QVERIFY(uncheckedBlock >= 0);
  const QString checkedText = document.findBlockByNumber(checkedBlock).text();
  const QString uncheckedText = document.findBlockByNumber(uncheckedBlock).text();
  QVERIFY2(checkedText.startsWith(QChar(0x2611)),
           qPrintable(QStringLiteral("checked glyph=%1").arg(checkedText.at(0).unicode())));
  QVERIFY2(uncheckedText.startsWith(QChar(0x2610)),
           qPrintable(QStringLiteral("unchecked glyph=%1").arg(uncheckedText.at(0).unicode())));
  QVERIFY2(document.findBlockByNumber(checkedBlock).textList()->format().style() ==
               QTextListFormat::ListStyleUndefined,
           "task list still carries a bullet style");

  const QString serialized = typist::editor::serializeDocument(&document);
  QVERIFY2(serialized.contains(QStringLiteral("- [x] done")) &&
               serialized.contains(QStringLiteral("- [ ] todo")),
           qPrintable(serialized));
  QVERIFY(!serialized.contains(QChar(0x2611)) && !serialized.contains(QChar(0x2610)));
  QVERIFY(typist::core::astEqual(typist::core::parseMarkdown(source.toStdString()).blocks,
                                 typist::core::parseMarkdown(serialized.toStdString()).blocks));

  QVERIFY(typist::editor::toggleTaskCheckbox(&document, checkedBlock));
  const QString flipped = document.findBlockByNumber(checkedBlock).text();
  QVERIFY2(flipped.startsWith(QChar(0x2610)),
           qPrintable(QStringLiteral("glyph after toggle=%1").arg(flipped.at(0).unicode())));
  const QString afterToggle = typist::editor::serializeDocument(&document);
  QVERIFY2(afterToggle.contains(QStringLiteral("- [ ] done")), qPrintable(afterToggle));
}

void EditorSpike::sidebarExpandStateSurvivesRebuild() {
  QDir root(QDir::tempPath());
  const QString dirName = QStringLiteral("typist_tree_state");
  QVERIFY(root.mkpath(QStringLiteral("%1/nested").arg(dirName)));
  QFile leaf(root.filePath(QStringLiteral("%1/nested/a.md").arg(dirName)));
  QVERIFY(leaf.open(QIODevice::WriteOnly));
  leaf.write("# leaf\n");
  leaf.close();

  typist::ui::FileTree tree;
  tree.setRoot(root.filePath(dirName));
  QCOMPARE(tree.entryCount(), 1);
  QCOMPARE(tree.directoryCount(), 1);
  QVERIFY(tree.topLevelItem(0)->isExpanded());

  tree.topLevelItem(0)->setExpanded(false);
  tree.setRoot(root.filePath(dirName));
  QVERIFY2(!tree.topLevelItem(0)->isExpanded(), "collapsed directory was reopened");

  tree.topLevelItem(0)->setExpanded(true);
  tree.setRoot(root.filePath(dirName));
  QVERIFY2(tree.topLevelItem(0)->isExpanded(), "expanded directory was collapsed");
  QCOMPARE(tree.entryCount(), 1);

  QFile::remove(root.filePath(QStringLiteral("%1/nested/a.md").arg(dirName)));
  QVERIFY(root.rmdir(QStringLiteral("%1/nested").arg(dirName)));
  QVERIFY(root.rmdir(dirName));
}

void EditorSpike::docxExportEmbedsImageDrawing() {
  const QString imagePath =
      QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_image.png"));
  QImage image(24, 18, QImage::Format_ARGB32);
  image.fill(QColor(180, 60, 40));
  QVERIFY(image.save(imagePath, "PNG"));
  const QString markdown = QStringLiteral("![pic](%1)\n").arg(imagePath);
  const QString docxPath =
      QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_image.docx"));
  QString error;
  QVERIFY2(typist::platform::exportDocx(docxPath, markdown, &error), error.toUtf8().constData());
  QFile file(docxPath);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QByteArray data = file.readAll();
  file.close();
  QVERIFY2(data.contains("<w:drawing>"), "no drawing element");
  QVERIFY2(data.contains("r:embed=\"rId1001\""), "no embedded relationship id");
  QVERIFY2(data.contains("word/media/image1.png"), "no media part entry");
  QVERIFY2(data.contains("\x89PNG"), "media entry does not carry PNG bytes");
  QVERIFY2(data.contains("<Default Extension=\"png\""), "content types miss png default");
  QVERIFY2(data.contains("cx=\"228600\"") && data.contains("cy=\"171450\""),
           "extents do not match 24x18 px in EMU");
  QVERIFY2(
      data.contains("<Relationship Id=\"rId1001\"") && data.contains("Target=\"media/image1.png\""),
      "image relationship missing");
  const QByteArray probePath = qgetenv("TYPIST_DOCX_IMAGE_PROBE");
  if (!probePath.isEmpty()) QFile::copy(docxPath, QString::fromUtf8(probePath));
  QFile::remove(docxPath);
  QFile::remove(imagePath);
}

void EditorSpike::docxExportFallsBackWhenImageMissing() {
  const QString docxPath =
      QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_missing_image.docx"));
  QString error;
  QVERIFY2(typist::platform::exportDocx(docxPath, QStringLiteral("![gone](missing.png)\n"), &error),
           error.toUtf8().constData());
  QFile file(docxPath);
  QVERIFY(file.open(QIODevice::ReadOnly));
  const QByteArray data = file.readAll();
  file.close();
  QVERIFY(!data.contains("<w:drawing>"));
  QVERIFY(!data.contains("word/media"));
  QVERIFY(data.contains("<w:hyperlink"));
  QFile::remove(docxPath);
}

void EditorSpike::headingInlineStylesRoundTrip() {
  const QString source = QStringLiteral("# **bold** and *em* with ") + QChar(0x60) +
                         QStringLiteral("code") + QChar(0x60) +
                         QStringLiteral(" and [link](https://example.org/a)") +
                         QStringLiteral("\n\n### ## not a heading\n");
  const auto parsed = typist::core::parseMarkdown(source.toStdString());
  const QString serialized = QString::fromStdString(typist::core::serializeMarkdown(parsed.blocks));
  QVERIFY2(serialized.startsWith(QStringLiteral("# **bold** and *em* with ") + QChar(0x60) +
                                 QStringLiteral("code") + QChar(0x60) +
                                 QStringLiteral(" and [link](https://example.org/a)")),
           serialized.toUtf8().constData());
  QVERIFY(typist::core::astEqual(parsed.blocks,
                                 typist::core::parseMarkdown(serialized.toStdString()).blocks));

  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
  const QString documentText = typist::editor::serializeDocument(&document);
  QVERIFY2(documentText.contains(QStringLiteral("**bold**")) &&
               documentText.contains(QStringLiteral("*em*")) &&
               documentText.contains(QChar(0x60)) &&
               documentText.contains(QStringLiteral("[link](https://example.org/a)")),
           documentText.toUtf8().constData());
  QVERIFY(typist::core::astEqual(typist::core::parseMarkdown(documentText.toStdString()).blocks,
                                 typist::core::parseMarkdown(serialized.toStdString()).blocks));
}

void EditorSpike::pdfLongTablePaginatesRowByRow() {
  QString source;
  source += QStringLiteral("| col a | col b | col c |\n| --- | --- | --- |\n");
  for (int row = 0; row < 60; ++row) {
    source += QStringLiteral("| row %1 | value %1 | text %1 |\n").arg(row);
  }
  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
  const QString path =
      QDir(QDir::tempPath()).filePath(QStringLiteral("typist_pdf_table_pages.pdf"));
  QString error;
  QVERIFY2(typist::platform::exportPdf(path, &document, &error), error.toUtf8().constData());
  const int pages = pdfPageCount(path);
  QFile::remove(path);
  QVERIFY2(pages >= 2, QStringLiteral("pages=%1").arg(pages).toUtf8().constData());
}

void EditorSpike::nestedListsRoundTripAtDepthFour() {
  const QString source =
      QStringLiteral("- one\n  - two\n    - three\n      - four\n        - five\n- next\n");
  const typist::core::ParseResult first = typist::core::parseMarkdown(source.toStdString());
  QVERIFY(first.ok);
  const QString serialized = QString::fromStdString(typist::core::serializeMarkdown(first.blocks));
  const typist::core::ParseResult second = typist::core::parseMarkdown(serialized.toStdString());
  QVERIFY(second.ok);
  QVERIFY2(typist::core::astEqual(first.blocks, second.blocks), serialized.toUtf8().constData());

  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
  const QString fromDocument = typist::editor::serializeDocument(&document);
  const typist::core::ParseResult third = typist::core::parseMarkdown(fromDocument.toStdString());
  QVERIFY(third.ok);
  QVERIFY2(typist::core::astEqual(first.blocks, third.blocks), fromDocument.toUtf8().constData());
}

void EditorSpike::inlineImageRendersAndRoundTrips() {
  const QString imagePath =
      QDir(QDir::tempPath()).filePath(QStringLiteral("typist_inline_pic.png"));
  QImage image(24, 18, QImage::Format_RGB32);
  image.fill(Qt::blue);
  QVERIFY(image.save(imagePath));

  QTextDocument document;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("body\n\n![pic](%1)\n").arg(imagePath),
                                       &document, nullptr));
  const QString serialized = typist::editor::serializeDocument(&document);
  QVERIFY(serialized.contains(QStringLiteral("![pic](") + imagePath + QStringLiteral(")")));
  QVERIFY(!serialized.contains(QChar(0xFFFC)));

  // Layout is lazy: heights only become real once the document is laid out.
  const QSizeF laidOut = document.documentLayout()->documentSize();
  QVERIFY(laidOut.height() > 18.0);

  bool tagged = false;
  bool imageShown = false;
  for (QTextBlock block = document.firstBlock(); block.isValid(); block = block.next()) {
    for (const QTextLayout::FormatRange& range : block.textFormats()) {
      if (range.format.hasProperty(typist::editor::kImageProperty)) tagged = true;
    }
    if (document.documentLayout()->blockBoundingRect(block).height() >= 18.0) imageShown = true;
  }
  QVERIFY(tagged);
  QVERIFY(imageShown);

  const typist::core::ParseResult parsed = typist::core::parseMarkdown(serialized.toStdString());
  QVERIFY(parsed.ok);
  bool imageRun = false;
  for (const typist::core::Block& block : parsed.blocks) {
    for (const typist::core::InlineRun& run : block.runs)
      if (run.image) imageRun = true;
  }
  QVERIFY(imageRun);
  QFile::remove(imagePath);
}

void EditorSpike::docxExportWritesHyperlinkRelationships() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_links.docx"));
  QString error;
  QVERIFY(typist::platform::exportDocx(
      path,
      QStringLiteral("see [docs](https://example.com/docs) and again "
                     "[docs](https://example.com/docs) plus ![pic](E:/imgs/pic.png)\n"),
      &error));
  const QByteArray data = readWholeFile(path);
  QFile::remove(path);

  int hyperlinks = 0;
  int offset = 0;
  while ((offset = data.indexOf("<w:hyperlink", offset)) != -1) {
    ++hyperlinks;
    offset += 12;
  }
  QCOMPARE(hyperlinks, 3);

  int external = 0;
  offset = 0;
  while ((offset = data.indexOf("TargetMode=\"External\"", offset)) != -1) {
    ++external;
    offset += 20;
  }
  QCOMPARE(external, 2);  // the repeated URL shares one relationship
  QVERIFY(data.contains("https://example.com/docs"));
  QVERIFY(data.contains("E:/imgs/pic.png"));
  QCOMPARE(localEntryNames(data).size(), 6);
}

void EditorSpike::docxExportFailsWhenPathUnwritable() {
  const QString path =
      QDir(QDir::tempPath()).filePath(QStringLiteral("typist_missing_dir/out.docx"));
  QString error;
  QVERIFY(!typist::platform::exportDocx(path, QStringLiteral("# Title\n"), &error));
  QVERIFY(!error.isEmpty());
}

static QString runDump(const std::vector<typist::core::InlineRun>& runs) {
  QString out;
  for (const typist::core::InlineRun& run : runs) {
    QString flags;
    if (run.bold) flags += QLatin1Char('B');
    if (run.italic) flags += QLatin1Char('I');
    if (run.strike) flags += QLatin1Char('S');
    if (run.code) flags += QLatin1Char('C');
    if (run.link) flags += QLatin1Char('L');
    if (run.image) flags += QLatin1Char('G');
    out += QStringLiteral("[") + flags + QStringLiteral("]") + QString::fromStdString(run.text) +
           QStringLiteral(" ");
  }
  return out;
}

void EditorSpike::inlineRunsRoundTripThroughMarkdownSerialize() {
  const std::string source =
      "**bold** and *em* ~~gone~~ `code` [link](http://example.com) ![pic](p.png)\n";
  const typist::core::ParseResult first = typist::core::parseMarkdown(source);
  QVERIFY(first.ok);
  int styled = 0;
  bool bold = false;
  bool italic = false;
  bool strike = false;
  bool code = false;
  bool link = false;
  bool image = false;
  QString linkUrl;
  QString imageUrl;
  for (const typist::core::InlineRun& run : first.blocks.front().runs) {
    if (run.bold || run.italic || run.strike || run.code || run.link || run.image) ++styled;
    if (run.bold) bold = true;
    if (run.italic) italic = true;
    if (run.strike) strike = true;
    if (run.code) code = true;
    if (run.link) {
      link = true;
      linkUrl = QString::fromStdString(run.url);
    }
    if (run.image) {
      image = true;
      imageUrl = QString::fromStdString(run.url);
    }
  }
  QCOMPARE(styled, 6);
  QVERIFY2(bold && italic && strike && code && link && image,
           qPrintable(runDump(first.blocks.front().runs)));
  QCOMPARE(linkUrl, QStringLiteral("http://example.com"));
  QCOMPARE(imageUrl, QStringLiteral("p.png"));

  const std::string serialized = typist::core::serializeMarkdown(first.blocks);
  QVERIFY(serialized.find("**bold**") != std::string::npos);
  QVERIFY(serialized.find("*em*") != std::string::npos);
  QVERIFY(serialized.find("~~gone~~") != std::string::npos);
  QVERIFY(serialized.find("`code`") != std::string::npos);
  QVERIFY(serialized.find("[link](http://example.com)") != std::string::npos);
  QVERIFY(serialized.find("![pic](p.png)") != std::string::npos);

  const typist::core::ParseResult again = typist::core::parseMarkdown(serialized);
  QVERIFY(typist::core::astEqual(first.blocks, again.blocks));
}

void EditorSpike::inlineRunsRoundTripThroughDocument() {
  const QString source =
      QStringLiteral("**bold** and *em* ~~gone~~ `code` [link](http://example.com)\n");
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(source, &doc, nullptr));
  const QString serialized = typist::editor::serializeDocument(&doc);
  QVERIFY(serialized.contains(QStringLiteral("**bold**")));
  QVERIFY(serialized.contains(QStringLiteral("*em*")));
  QVERIFY(serialized.contains(QStringLiteral("~~gone~~")));
  QVERIFY(serialized.contains(QStringLiteral("`code`")));
  QVERIFY(serialized.contains(QStringLiteral("[link](http://example.com)")));
  QVERIFY(typist::core::astEqual(typist::core::parseMarkdown(source.toStdString()).blocks,
                                 typist::core::parseMarkdown(serialized.toStdString()).blocks));
}

void EditorSpike::renderedDocumentCarriesInlineFormats() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(
      QStringLiteral(
          "plain **bold** *em* ~~gone~~ `code` [link](http://example.com) ![pic](p.png)\n"),
      &doc, nullptr));
  bool bold = false;
  bool italic = false;
  bool strike = false;
  bool mono = false;
  bool link = false;
  bool image = false;
  for (QTextBlock block = doc.firstBlock(); block.isValid(); block = block.next()) {
    for (const QTextLayout::FormatRange& range : block.textFormats()) {
      if (range.format.fontWeight() >= QFont::Bold) bold = true;
      if (range.format.fontItalic()) italic = true;
      if (range.format.fontStrikeOut()) strike = true;
      if (range.format.fontFamilies().toStringList().contains(QStringLiteral("Consolas")))
        mono = true;
      if (range.format.hasProperty(typist::editor::kImageProperty)) image = true;
      if (range.format.isAnchor() &&
          range.format.anchorHref() == QStringLiteral("http://example.com"))
        link = true;
    }
  }
  QVERIFY(bold);
  QVERIFY(italic);
  QVERIFY(strike);
  QVERIFY(mono);
  QVERIFY(link);
  QVERIFY(image);
}

void EditorSpike::pasteMarkdownInsertsBlocksAsOneUndoStep() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("para\n"), &doc, nullptr));
  const QString before = typist::editor::serializeDocument(&doc);
  QTextCursor cursor(&doc);
  cursor.movePosition(QTextCursor::End);
  QString error;
  QVERIFY(typist::editor::insertMarkdownAtCursor(QStringLiteral("## Title\n\n**em** body\n"),
                                                 cursor, &error));
  const QString after = typist::editor::serializeDocument(&doc);
  QVERIFY(after.contains(QStringLiteral("para")));
  QVERIFY(after.contains(QStringLiteral("## Title")));
  QVERIFY(after.contains(QStringLiteral("**em** body")));
  doc.undo();
  QCOMPARE(typist::editor::serializeDocument(&doc), before);
}

void EditorSpike::pasteMarkdownKeepsTableAndTaskItem() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("para\n"), &doc, nullptr));
  QTextCursor cursor(&doc);
  cursor.movePosition(QTextCursor::End);
  QString error;
  QVERIFY(typist::editor::insertMarkdownAtCursor(
      QStringLiteral("| a | b |\n| --- | --- |\n| 1 | 2 |\n\n- [x] done\n"), cursor, &error));
  const QString serialized = typist::editor::serializeDocument(&doc);
  const QString expected =
      QStringLiteral("para\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n\n- [x] done\n");
  QVERIFY(!serialized.contains(QStringLiteral("<w:tbl>")));
  QVERIFY(typist::core::astEqual(typist::core::parseMarkdown(expected.toStdString()).blocks,
                                 typist::core::parseMarkdown(serialized.toStdString()).blocks));
}

void EditorSpike::imageLinkSurvivesDocumentRoundTrip() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("body\n"), &doc, nullptr));
  QTextCursor cursor(&doc);
  cursor.movePosition(QTextCursor::End);
  QString error;
  QVERIFY(typist::editor::insertMarkdownAtCursor(QStringLiteral("![dog](E:/imgs/dog.png)\n"),
                                                 cursor, &error));
  const QString serialized = typist::editor::serializeDocument(&doc);
  QVERIFY(serialized.contains(QStringLiteral("![dog](E:/imgs/dog.png)")));
  const typist::core::ParseResult parsed = typist::core::parseMarkdown(serialized.toStdString());
  QVERIFY(parsed.ok);
  bool image = false;
  for (const typist::core::Block& block : parsed.blocks) {
    for (const typist::core::InlineRun& run : block.runs)
      if (run.image) image = true;
  }
  QVERIFY(image);
}

void EditorSpike::docxInlineEmphasisWritesRunProperties() {
  const QString path = QDir(QDir::tempPath()).filePath(QStringLiteral("typist_docx_inline.docx"));
  QString error;
  QVERIFY(typist::platform::exportDocx(path, QStringLiteral("**bold** *em* ~~gone~~ `code`\n"),
                                       &error));
  const QByteArray data = readWholeFile(path);
  QVERIFY(data.contains("<w:b/>"));
  QVERIFY(data.contains("<w:i/>"));
  QVERIFY(data.contains("<w:strike/>"));
  QVERIFY(data.contains("Consolas"));
  QFile::remove(path);
}

void EditorSpike::outlineListsHeadingLevelsAndPositions() {
  QTextDocument doc;
  QVERIFY(typist::editor::loadMarkdown(
      QStringLiteral("# One\n\n## Two\n\n### Three\n\nbody text\n"), &doc, nullptr));
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
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("# A\n\n## B\n\nplain 12 words\n"), &doc,
                                       nullptr));
  typist::ui::SearchController search;
  search.setRegex(true);
  // Anchors apply to each block of the rendered document, where heading markers are styling, not
  // text.
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
  QVERIFY(typist::editor::loadMarkdown(QStringLiteral("## first\n\n## second\n\nbody\n"), &doc,
                                       nullptr));
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
  qputenv("TYPIST_RECOVERY_DIR",
          QDir::tempPath() == dir.path() ? QByteArray() : dir.path().toLocal8Bit());
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
  QVERIFY(typist::ui::recoveryKeyFor(QStringLiteral("/a.md")) !=
          typist::ui::recoveryKeyFor(QStringLiteral("/b.md")));
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
  const QVector<typist::platform::SearchHit> hits =
      spy.at(0).at(0).value<QVector<typist::platform::SearchHit>>();
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
  QVERIFY(typist::ui::translationCandidates(QStringLiteral("de_DE"))
              .first()
              .endsWith(QStringLiteral("typist_de.qm")));
}

void EditorSpike::translationFileCoversMainWindowStrings() {
  const QString tsPath = qEnvironmentVariable("TYPIST_TS_FILE");
  const QString sourcePath = qEnvironmentVariable("TYPIST_UI_SOURCE");
  if (tsPath.isEmpty() || sourcePath.isEmpty())
    QSKIP("Translation consistency check needs TYPIST_TS_FILE and TYPIST_UI_SOURCE.");
  QFile tsFile(tsPath);
  QVERIFY(tsFile.open(QIODevice::ReadOnly));
  QFile sourceFile(sourcePath);
  QVERIFY(sourceFile.open(QIODevice::ReadOnly));
  const QString sourceCode = QString::fromUtf8(sourceFile.readAll());
  sourceFile.close();
  // Formatting can wrap tr() calls and split a message into adjacent C++ literals.
  const QRegularExpression trCall(QStringLiteral(R"tr(\btr\s*\(\s*((?:"(?:\\.|[^"\\])*"\s*)+))tr"));
  const QRegularExpression stringLiteral(QStringLiteral(R"tr("((?:\\.|[^"\\])*)")tr"));
  QSet<QString> uniqueTr;
  auto calls = trCall.globalMatch(sourceCode);
  while (calls.hasNext()) {
    const QString arguments = calls.next().captured(1);
    auto literals = stringLiteral.globalMatch(arguments);
    QString text;
    while (literals.hasNext()) text += literals.next().captured(1);
    uniqueTr.insert(text);
  }
  QXmlStreamReader xml(&tsFile);
  int messages = 0;
  bool inMainWindow = false;
  QString source;
  QString translation;
  QString currentElement;
  while (!xml.atEnd()) {
    const QXmlStreamReader::TokenType token = xml.readNext();
    if (xml.hasError()) {
      QFAIL(qPrintable(QStringLiteral("Translation file is not well-formed XML: ") +
                       xml.errorString()));
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
        QVERIFY2(uniqueTr.contains(source),
                 qPrintable(QStringLiteral("missing tr for: ") + source));
        ++messages;
      }
      currentElement.clear();
    } else if (token == QXmlStreamReader::Characters && !xml.isWhitespace()) {
      if (currentElement == QStringLiteral("name"))
        inMainWindow = xml.text().toString() == QStringLiteral("MainWindow");
      else if (currentElement == QStringLiteral("source"))
        source = xml.text().toString();
      else if (currentElement == QStringLiteral("translation"))
        translation = xml.text().toString();
    }
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
  QCOMPARE(typist::platform::darkHintFromGtkTheme(QStringLiteral("Adwaita:dark")),
           typist::platform::DarkHint::Dark);
  QCOMPARE(typist::platform::darkHintFromGtkTheme(QStringLiteral("Yaru-dark")),
           typist::platform::DarkHint::Dark);
  QCOMPARE(typist::platform::darkHintFromGtkTheme(QStringLiteral("Adwaita")),
           typist::platform::DarkHint::Light);
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
