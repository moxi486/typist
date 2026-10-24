#include "markdown.h"

#include <cmark-gfm.h>
#include <cmark-gfm-extension_api.h>

#include <memory>
#include <string>
#include <vector>

namespace typist::core {
namespace {

std::string flattenInline(cmark_node* node) {
  std::string out;
  for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
       child = cmark_node_next(child)) {
    const std::string type = cmark_node_get_type_string(child);
    const char* literal = cmark_node_get_literal(child);
    if (type == "text" && literal != nullptr) {
      out += literal;
    } else if (type == "softbreak") {
      out += " ";
    } else if (type == "linebreak") {
      out += "\n";
    } else {
      out += flattenInline(child);
    }
  }
  return out;
}

Block buildBlock(cmark_node* node) {
  Block block;
  const std::string type = cmark_node_get_type_string(node);

  if (type == "heading") {
    block.type = BlockType::Heading;
    block.level = cmark_node_get_heading_level(node);
    block.text = flattenInline(node);
  } else if (type == "paragraph") {
    block.type = BlockType::Paragraph;
    block.text = flattenInline(node);
  } else if (type == "list") {
    block.type = BlockType::List;
    block.ordered = cmark_node_get_list_type(node) == CMARK_LIST_TYPE_ORDERED;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      block.children.push_back(buildBlock(child));
    }
  } else if (type == "item") {
    block.type = BlockType::ListItem;
    std::string flat;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      flat += flattenInline(child);
    }
    if (flat.rfind("[ ] ", 0) == 0) {
      block.task = true;
      flat = flat.substr(4);
    } else if (flat.rfind("[x] ", 0) == 0 || flat.rfind("[X] ", 0) == 0) {
      block.task = true;
      block.checked = true;
      flat = flat.substr(4);
    }
    block.text = flat;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      if (cmark_node_get_type_string(child) == "list") {
        block.children.push_back(buildBlock(child));
      }
    }
  } else if (type == "code_block") {
    block.type = BlockType::CodeBlock;
    const char* literal = cmark_node_get_literal(node);
    block.text = literal != nullptr ? literal : "";
    // Fence info is exposed through string content; verify against the installed
    // libcmark-gfm headers on the first CI run (docs/ROADMAP.md open item).
    const char* info = cmark_node_get_string_content(node);
    block.info = info != nullptr ? info : "";
  } else if (type == "thematic_break") {
    block.type = BlockType::ThematicBreak;
  } else if (type == "blockquote") {
    block.type = BlockType::BlockQuote;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      block.children.push_back(buildBlock(child));
    }
  } else if (type == "table" || type == "table_row") {
    block.type = type == "table" ? BlockType::Table : BlockType::TableRow;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      block.children.push_back(buildBlock(child));
    }
  } else if (type == "table_cell") {
    block.type = BlockType::TableCell;
    block.text = flattenInline(node);
  } else {
    block.type = BlockType::Unsupported;
    block.text = type;
  }

  return block;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::string line;
  for (char c : text) {
    if (c == '\n') {
      if (!line.empty()) lines.push_back(line);
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty()) lines.push_back(line);
  return lines;
}

std::string serializeRow(const Block& row) {
  std::string out = "|";
  for (const Block& cell : row.children) out += " " + cell.text + " |";
  out += "\n";
  return out;
}

std::string serializeTable(const Block& table) {
  if (table.children.empty()) return "\n";
  std::string out = serializeRow(table.children.front());
  out += "|";
  for (std::size_t i = 0; i < table.children.front().children.size(); ++i) out += " --- |";
  out += "\n";
  for (std::size_t i = 1; i < table.children.size(); ++i) out += serializeRow(table.children[i]);
  out += "\n";
  return out;
}

std::string serializeBlock(const Block& block, int indent, int itemIndex) {
  const std::string prefix(static_cast<std::size_t>(indent), ' ');
  std::string out;

  switch (block.type) {
    case BlockType::Heading:
      out += prefix + std::string(static_cast<std::size_t>(block.level), '#');
      out += " " + block.text + "\n\n";
      break;
    case BlockType::Paragraph:
      out += block.text.empty() ? "\n" : prefix + block.text + "\n\n";
      break;
    case BlockType::CodeBlock:
      out += prefix + "```" + block.info + "\n";
      out += block.text;
      if (block.text.empty() || block.text.back() != '\n') out += "\n";
      out += prefix + "```\n\n";
      break;
    case BlockType::ThematicBreak:
      out += prefix + "---\n\n";
      break;
    case BlockType::Table:
      out += serializeTable(block);
      break;
    case BlockType::TableRow:
    case BlockType::TableCell:
      break;
    case BlockType::BlockQuote:
      for (const Block& child : block.children) {
        for (const std::string& line : splitLines(serializeBlock(child, indent + 2, 1))) {
          out += prefix + "> " + line + "\n";
        }
      }
      out += "\n";
      break;
    case BlockType::List: {
      int index = 1;
      for (const Block& item : block.children) {
        out += serializeBlock(item, indent, index++);
      }
      out += "\n";
      break;
    }
    case BlockType::ListItem: {
      // Nested content must line up with the text after this item's marker, so the
      // child indent follows the marker width, not a fixed two spaces.
      const std::string marker = block.ordered ? std::to_string(itemIndex) + ". " : "- ";
      out += prefix + marker;
      if (block.task) out += block.checked ? "[x] " : "[ ] ";
      out += block.text + "\n";
      const int childIndent = indent + static_cast<int>(marker.size());
      for (const Block& child : block.children) out += serializeBlock(child, childIndent, 1);
      break;
    }
    case BlockType::Unsupported:
      break;
  }

  return out;
}

}  // namespace

ParseResult parseMarkdown(std::string_view source, const ParseOptions& options) {
  ParseResult result;
  cmark_gfm_core_extensions_ensure_registered();

  std::unique_ptr<cmark_parser, decltype(&cmark_parser_free)> parser(
      cmark_parser_new(CMARK_OPT_DEFAULT), &cmark_parser_free);
  if (!parser) {
    result.error = "cmark_parser_new failed";
    return result;
  }

  const char* names[] = {
      options.gfmTables ? "table" : nullptr,
      options.gfmTaskList ? "tasklist" : nullptr,
      options.gfmStrikethrough ? "strikethrough" : nullptr,
      options.autolink ? "autolink" : nullptr,
  };
  for (const char* name : names) {
    if (name == nullptr) continue;
    cmark_syntax_extension* extension = cmark_find_syntax_extension(name);
    if (extension != nullptr) cmark_parser_attach_syntax_extension(parser.get(), extension);
  }

  cmark_parser_feed(parser.get(), source.data(), source.size());
  std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root(
      cmark_parser_finish(parser.get()), &cmark_node_free);
  if (!root) {
    result.error = "cmark_parser_finish failed";
    return result;
  }

  for (cmark_node* child = cmark_node_first_child(root.get()); child != nullptr;
       child = cmark_node_next(child)) {
    result.blocks.push_back(buildBlock(child));
  }
  result.ok = true;
  return result;
}

std::string serializeMarkdown(const std::vector<Block>& blocks) {
  std::string out;
  for (const Block& block : blocks) out += serializeBlock(block, 0, 1);
  if (!out.empty() && out.back() != '\n') out += "\n";
  return out;
}

bool astEqual(const std::vector<Block>& lhs, const std::vector<Block>& rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const Block& a = lhs[i];
    const Block& b = rhs[i];
    if (a.type != b.type || a.level != b.level || a.ordered != b.ordered ||
        a.task != b.task || a.checked != b.checked || a.text != b.text ||
        a.info != b.info) {
      return false;
    }
    if (!astEqual(a.children, b.children)) return false;
  }
  return true;
}

}  // namespace typist::core
