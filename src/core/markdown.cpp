#include "markdown.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>
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
    if (type == "list" || type == "item" || type == "tasklist" || type == "table" ||
        type == "table_row" || type == "table_header" || type == "table_cell") {
      // Structural children are modelled as blocks, never folded into text.
      continue;
    }
    const char* literal = cmark_node_get_literal(child);
    if (type == "text" && literal != nullptr) {
      out += literal;
    } else if (type == "code" && literal != nullptr) {
      out += literal;
    } else if (type == "softbreak") {
      out += " ";
    } else if (type == "linebreak") {
      out += "\n";
    } else if (type == "html" && literal != nullptr) {
      // Inline HTML such as <br> is carried verbatim; it is the only way a table
      // cell can hold more than one line.
      out += literal;
    } else {
      out += flattenInline(child);
    }
  }
  return out;
}

// cmark-gfm 0.29.0.gfm.13 exposes no fence info accessor: string_content comes
// back empty for a fenced block, so the tag is read from the fence line itself.
std::string fenceInfo(cmark_node* node, const std::vector<std::string>& lines) {
  const int line = cmark_node_get_start_line(node);
  if (line < 1 || static_cast<std::size_t>(line) > lines.size()) return "";
  const std::string& raw = lines[static_cast<std::size_t>(line) - 1];
  const std::size_t start = raw.find_first_not_of(" \t");
  if (start == std::string::npos) return "";
  if (raw.compare(start, 3, "```") != 0 && raw.compare(start, 3, "~~~") != 0) return "";
  std::string rest = raw.substr(start + 3);
  const std::size_t end = rest.find_first_of(" \t");
  if (end != std::string::npos) rest = rest.substr(0, end);
  return rest;
}

std::vector<std::string> sourceLines(std::string_view source) {
  std::vector<std::string> lines;
  std::size_t pos = 0;
  while (pos < source.size()) {
    const std::size_t next = source.find('\n', pos);
    if (next == std::string_view::npos) {
      lines.push_back(std::string(source.substr(pos)));
      break;
    }
    lines.push_back(std::string(source.substr(pos, next - pos)));
    pos = next + 1;
  }
  return lines;
}

Block buildBlock(cmark_node* node, const std::vector<std::string>& lines) {
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
    block.ordered = cmark_node_get_list_type(node) == CMARK_ORDERED_LIST;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      block.children.push_back(buildBlock(child, lines));
    }
  } else if (type == "item") {
    block.type = BlockType::ListItem;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      const std::string childType = cmark_node_get_type_string(child);
      if (childType == "list") {
        block.children.push_back(buildBlock(child, lines));
        continue;
      }
      if (childType == "tasklist") {
        block.task = true;
        block.checked = cmark_gfm_extensions_get_tasklist_item_checked(child);
      }
      block.text += flattenInline(child);
    }
  } else if (type == "tasklist") {
    // The GFM tasklist extension replaces the item node itself, so the checked
    // flag comes from the extension accessor rather than the text prefix.
    block.type = BlockType::ListItem;
    block.task = true;
    block.checked = cmark_gfm_extensions_get_tasklist_item_checked(node);
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      const std::string childType = cmark_node_get_type_string(child);
      if (childType == "list") {
        block.children.push_back(buildBlock(child, lines));
        continue;
      }
      block.text += flattenInline(child);
    }
  } else if (type == "code_block") {
    block.type = BlockType::CodeBlock;
    const char* literal = cmark_node_get_literal(node);
    block.text = literal != nullptr ? literal : "";
    block.info = fenceInfo(node, lines);
  } else if (type == "thematic_break") {
    block.type = BlockType::ThematicBreak;
  } else if (type == "block_quote") {
    block.type = BlockType::BlockQuote;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      block.children.push_back(buildBlock(child, lines));
    }
  } else if (type == "table" || type == "table_row" || type == "table_header") {
    block.type = type == "table" ? BlockType::Table : BlockType::TableRow;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      block.children.push_back(buildBlock(child, lines));
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

// A Markdown table cell cannot hold a raw pipe or a line break: the pipe is escaped
// as \| and a line break is written as <br>, which parses back as inline HTML.
std::string escapeTableCell(const std::string& text) {
  std::string out;
  for (char c : text) {
    if (c == '|') out += "\\|";
    else if (c == '\n') out += "<br>";
    else out += c;
  }
  return out;
}

std::string serializeRow(const Block& row) {
  std::string out = "|";
  for (const Block& cell : row.children) out += " " + escapeTableCell(cell.text) + " |";
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

std::string serializeBlock(const Block& block, int indent) {
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
        for (const std::string& line : splitLines(serializeBlock(child, indent + 2))) {
          out += prefix + "> " + line + "\n";
        }
      }
      out += "\n";
      break;
    case BlockType::List: {
      int index = 1;
      for (const Block& item : block.children) {
        const std::string marker = block.ordered ? std::to_string(index++) + ". " : "- ";
        out += prefix + marker;
        if (item.task) out += item.checked ? "[x] " : "[ ] ";
        out += item.text + "\n";
        const int childIndent = indent + static_cast<int>(marker.size());
        for (const Block& child : item.children) out += serializeBlock(child, childIndent);
      }
      out += "\n";
      break;
    }
    case BlockType::ListItem:
      // Items are emitted by their parent List, which owns the marker and numbering.
      break;
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

  const std::vector<std::string> lines = sourceLines(source);
  cmark_parser_feed(parser.get(), source.data(), source.size());
  std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root(
      cmark_parser_finish(parser.get()), &cmark_node_free);
  if (!root) {
    result.error = "cmark_parser_finish failed";
    return result;
  }

  for (cmark_node* child = cmark_node_first_child(root.get()); child != nullptr;
       child = cmark_node_next(child)) {
    result.blocks.push_back(buildBlock(child, lines));
  }
  result.ok = true;
  return result;
}

std::string serializeMarkdown(const std::vector<Block>& blocks) {
  std::string out;
  for (const Block& block : blocks) out += serializeBlock(block, 0);
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
