#include "editor_document.h"
#include "markdown.h"

#include <QtTest>
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

 private:
  std::string m_corpus;
  std::string read(const std::string& name) const;
};

std::string EditorSpike::read(const std::string& name) const {
  std::ifstream stream(std::filesystem::path(m_corpus) / name);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void EditorSpike::initTestCase() {
  m_corpus = qEnvironmentVariable("TYPIST_CORPUS_DIR");
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
      "headings.md", "paragraphs.md", "lists.md", "thematic.md", "code.md", "quote.md", "fences.md",
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
  // Known gap: document->Markdown drops tables; fidelity is held by the AST path.
  const auto parsed = typist::core::parseMarkdown(read("gfm/table.md"));
  QVERIFY(parsed.ok);

  QTextDocument document;
  typist::editor::buildDocument(parsed.blocks, &document);
  QVERIFY(!typist::editor::serializeDocument(&document).contains("|"));
  QVERIFY(typist::core::serializeMarkdown(parsed.blocks).contains("| --- |"));
}

QTEST_MAIN(EditorSpike)

#include "roundtrip_spike.moc"
