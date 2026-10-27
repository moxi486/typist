#include "editor_document.h"
#include "FileSearch.h"
#include "FileWriter.h"
#include "PdfWriter.h"
#include "AutoSave.h"
#include "FileTree.h"
#include "Search.h"
#include "Settings.h"
#include "ViewSwitch.h"
#include "Theme.h"
#include "markdown.h"

#include <QtTest>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QTextCursor>
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

QTEST_MAIN(EditorSpike)

#include "roundtrip_spike.moc"