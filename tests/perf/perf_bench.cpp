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

std::string largeMarkdown(std::size_t targetBytes) {
  std::string out;
  while (out.size() < targetBytes) {
    out += "## Section heading\n\n";
    out += "A paragraph with **bold** text and a `code span` describing the behaviour.";
    out += " It continues with more prose so the block stays a realistic size.\n\n";
    out += "- list item one\n- list item two\n\n";
    out += "| a | b |\n| --- | --- |\n| 1 | 2 |\n\n";
    out += "```c\nint main(void) { return 0; }\n```\n\n";
  }
  return out;
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);

  const std::string source = largeMarkdown(1000000);

  QElapsedTimer timer;
  timer.start();
  const auto parsed = typist::core::parseMarkdown(source);
  const long long parseMs = static_cast<long long>(timer.elapsed());

  QTextDocument document;
  timer.restart();
  typist::editor::buildDocument(parsed.blocks, &document);
  const long long buildMs = static_cast<long long>(timer.elapsed());

  timer.restart();
  const QString serialized = typist::editor::serializeDocument(&document);
  const long long serializeMs = static_cast<long long>(timer.elapsed());

  const auto reparsed = typist::core::parseMarkdown(serialized.toStdString());
  bool fidelity = reparsed.ok && typist::core::astEqual(parsed.blocks, reparsed.blocks);
  if (!fidelity && reparsed.ok && parsed.blocks.size() != reparsed.blocks.size()) {
    std::printf("block_count=%zu vs %zu\n", parsed.blocks.size(), reparsed.blocks.size());
    const std::size_t limit = std::min(parsed.blocks.size(), reparsed.blocks.size());
    for (std::size_t i = 0; i < limit; ++i) {
      const typist::core::Block& a = parsed.blocks[i];
      const typist::core::Block& b = reparsed.blocks[i];
      if (a.type != b.type || a.level != b.level || a.ordered != b.ordered || a.text != b.text || a.info != b.info) {
        std::printf("first_mismatch=%zu type_a=%d type_b=%d text_a=%s text_b=%s\n", i, static_cast<int>(a.type),
                    static_cast<int>(b.type), a.text.substr(0, 40).c_str(), b.text.substr(0, 40).c_str());
        break;
      }
    }
  } else if (!fidelity && reparsed.ok) {
    std::printf("first_mismatch=children_or_counts blocks=%zu\n", parsed.blocks.size());
  }

  std::printf("bytes=%zu\n", source.size());
  std::printf("serialized_bytes=%lld\n", static_cast<long long>(serialized.size()));
  std::printf("head=%s\n", serialized.left(120).toUtf8().constData());
  std::printf("tail=%s\n", serialized.right(120).toUtf8().constData());
  std::printf("parse_ms=%lld\n", parseMs);
  std::printf("build_ms=%lld\n", buildMs);
  std::printf("serialize_ms=%lld\n", serializeMs);
  std::printf("total_ms=%lld\n", parseMs + buildMs + serializeMs);
  std::printf("fidelity=%s\n", fidelity ? "ok" : "fail");

  if (!parsed.ok) {
    std::printf("error=%s\n", parsed.error.c_str());
    return 1;
  }
  if (!fidelity) return 1;
  // Regression ceiling, not the acceptance budget. The budget is open 1MB md < 250ms,
  // which buildDocument does not meet yet (seconds today); the gap is recorded in
  // docs/ROADMAP.md and this bound only catches gross regressions on CI.
  if (parseMs + buildMs + serializeMs > 8000) return 1;
  return 0;
}
