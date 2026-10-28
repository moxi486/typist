#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace typist::core {

enum class BlockType {
  Heading,
  Paragraph,
  List,
  ListItem,
  CodeBlock,
  ThematicBreak,
  BlockQuote,
  Table,
  TableRow,
  TableCell,
  Unsupported,
};

// One styled inline span. Nesting collapses to flags, so bold+italic is one run
// with both flags rather than a nested tree.
struct InlineRun {
  std::string text;
  std::string url;  // link or image destination
  bool bold = false;
  bool italic = false;
  bool strike = false;
  bool code = false;
  bool link = false;
  bool image = false;
};

struct Block {
  BlockType type = BlockType::Paragraph;
  int level = 0;         // heading level
  bool ordered = false;  // ordered list
  bool task = false;     // GFM task list item
  bool checked = false;
  std::string text;             // flattened inline content
  std::vector<InlineRun> runs;  // styled inline spans; empty when there is no styling
  std::string info;             // fenced code info string
  std::vector<Block> children;
};

struct ParseOptions {
  bool gfmTables = true;
  bool gfmTaskList = true;
  bool gfmStrikethrough = true;
  bool autolink = true;
};

struct ParseResult {
  std::vector<Block> blocks;
  std::string error;
  bool ok = false;
};

ParseResult parseMarkdown(std::string_view source, const ParseOptions& options = {});

// Serializes a block tree back to Markdown, re-emitting emphasis, inline code,
// links and images from the inline runs; table alignment is dropped.
// Fidelity gaps are tracked in docs/ROADMAP.md.
std::string serializeMarkdown(const std::vector<Block>& blocks);

// Renders Markdown to an HTML fragment through cmark-gfm with the same GFM
// extensions as parsing. CMARK_OPT_UNSAFE is set so raw inline HTML (such as the
// <br> used inside table cells) survives; callers decide where the fragment goes.
std::string toHtml(std::string_view source, const ParseOptions& options = {});

// Wraps an HTML fragment in a standalone document with a UTF-8 charset and a
// neutral default stylesheet.
std::string wrapHtmlDocument(std::string_view body, std::string_view title);

// As above, but embedding a caller-supplied stylesheet instead of the neutral default.
std::string wrapHtmlDocument(std::string_view body, std::string_view title, std::string_view css);

bool astEqual(const std::vector<Block>& lhs, const std::vector<Block>& rhs);

}  // namespace typist::core
