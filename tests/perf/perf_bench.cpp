#include "editor_document.h"
#include "markdown.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QString>
#include <QTextDocument>

#include <cstddef>
#include <cstdio>
#include <string>

namespace {

std::string largeMarkdown(std::size_t targetBytes, bool withTables) {
  std::string out;
  while (out.size() < targetBytes) {
    out += "## Section heading\n\n";
    out += "A paragraph with **bold** text and a `code span` describing the behaviour.";
    out += " It continues with more prose so the block stays a realistic size.\n\n";
    out += "- list item one\n- list item two\n\n";
    if (withTables) out += "| a | b |\n| --- | --- |\n| 1 | 2 |\n\n";
    out += "```c\nint main(void) { return 0; }\n```\n\n";
  }
  return out;
}

struct ShapeResult {
  std::size_t bytes = 0;
  std::size_t blocks = 0;
  std::size_t tables = 0;
  long long parseMs = 0;
  long long buildMs = 0;
  long long serializeMs = 0;
  bool fidelity = false;
};

ShapeResult measureShape(bool withTables) {
  ShapeResult result;
  result.bytes = 0;

  const std::string source = largeMarkdown(1000000, withTables);
  result.bytes = source.size();

  QElapsedTimer timer;
  timer.start();
  const auto parsed = typist::core::parseMarkdown(source);
  result.parseMs = static_cast<long long>(timer.elapsed());
  result.blocks = parsed.blocks.size();
  for (const typist::core::Block& block : parsed.blocks) {
    if (block.type == typist::core::BlockType::Table) ++result.tables;
  }

  QTextDocument document;
  timer.restart();
  typist::editor::buildDocument(parsed.blocks, &document);
  result.buildMs = static_cast<long long>(timer.elapsed());

  timer.restart();
  const QString serialized = typist::editor::serializeDocument(&document);
  result.serializeMs = static_cast<long long>(timer.elapsed());

  const auto reparsed = typist::core::parseMarkdown(serialized.toStdString());
  result.fidelity = parsed.ok && reparsed.ok && typist::core::astEqual(parsed.blocks, reparsed.blocks);
  if (!parsed.ok) std::printf("error=%s\n", parsed.error.c_str());
  return result;
}

void report(const char* label, const ShapeResult& result) {
  std::printf("%s bytes=%zu blocks=%zu tables=%zu parse_ms=%lld build_ms=%lld serialize_ms=%lld total_ms=%lld fidelity=%s\n",
              label, result.bytes, result.blocks, result.tables, result.parseMs, result.buildMs, result.serializeMs,
              result.parseMs + result.buildMs + result.serializeMs, result.fidelity ? "ok" : "fail");
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);

  // Two shapes: a realistic document and an adversarial one with a table every ~230 bytes,
  // because QTextTable frames are the expensive part of the build.
  const ShapeResult realistic = measureShape(false);
  const ShapeResult tableHeavy = measureShape(true);
  report("realistic", realistic);
  report("tableheavy", tableHeavy);

  if (!realistic.fidelity || !tableHeavy.fidelity) return 1;
  // The acceptance budget is open 1MB md < 250ms; it is asserted on the realistic shape.
  if (realistic.parseMs + realistic.buildMs + realistic.serializeMs > 250) return 1;
  // The adversarial shape only guards against gross regressions; CI machines vary.
  if (tableHeavy.parseMs + tableHeavy.buildMs + tableHeavy.serializeMs > 1500) return 1;
  return 0;
}
