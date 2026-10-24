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
  Unsupported,
};

struct Block {
  BlockType type = BlockType::Paragraph;
  int level = 0;          // heading level
  bool ordered = false;   // ordered list
  bool task = false;      // GFM task list item
  bool checked = false;
  std::string text;       // flattened inline content
  std::string info;       // fenced code info string
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

// Serializes a block tree back to Markdown. Inline emphasis, links and images are
// flattened to plain text for now; tables and task lists serialize to nothing yet.
// Fidelity gaps are tracked in docs/ROADMAP.md.
std::string serializeMarkdown(const std::vector<Block>& blocks);

bool astEqual(const std::vector<Block>& lhs, const std::vector<Block>& rhs);

}  // namespace typist::core
