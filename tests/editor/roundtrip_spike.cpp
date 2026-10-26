#include "editor_document.h"
#include "FileWriter.h"
#include "PdfWriter.h"
#include "AutoSave.h"
#include "Settings.h"
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
  void autoSaveDebouncesTypingBursts();
  void autoSaveFlushRunsImmediately();
  void autoSaveStaysIdleWithoutEdits();
  void themeResolvesPreferenceAndStyles();
  void themePreferenceRoundTripsThroughSettings();
  void htmlExportEmbedsThemeStylesheet();

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

QTEST_MAIN(EditorSpike)

#include "roundtrip_spike.moc"
