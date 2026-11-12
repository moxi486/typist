#include "markdown.h"

#include "advanced_syntax.h"

#include <cmark-gfm-core-extensions.h>
#include <cmark-gfm-extension_api.h>
#include <cmark-gfm.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace typist::core {
namespace {

bool replaceWithUnderlineTag(cmark_node* node, const char* tag) {
  // The safe HTML renderer can emit this fixed tag without enabling raw HTML.
  std::unique_ptr<cmark_node, decltype(&cmark_node_free)> marker(
      cmark_node_new(CMARK_NODE_CUSTOM_INLINE), &cmark_node_free);
  if (!cmark_node_set_on_enter(marker.get(), tag) || !cmark_node_replace(node, marker.get()))
    return false;
  marker.release();
  cmark_node_free(node);
  return true;
}

bool isUnderlineTag(std::string_view tag, std::size_t name) {
  return tag.size() > name + 1 && (tag[name] == 'u' || tag[name] == 'U') &&
         (tag[name + 1] == '>' || std::isspace(static_cast<unsigned char>(tag[name + 1])));
}

void recognizeUnderlineTags(cmark_node* parent) {
  std::vector<cmark_node*> openings;
  for (auto* child = cmark_node_first_child(parent); child;) {
    auto* next = cmark_node_next(child);
    const auto type = cmark_node_get_type(child);
    if (type == CMARK_NODE_HTML_INLINE) {
      const char* literal = cmark_node_get_literal(child);
      const std::string_view tag = literal ? literal : "";
      if (isUnderlineTag(tag, 1) && tag.substr(tag.size() - 2) != "/>") {
        openings.push_back(child);
      } else if (tag.size() > 2 && tag[1] == '/' && isUnderlineTag(tag, 2) && !openings.empty()) {
        auto* opening = openings.back();
        openings.pop_back();
        // Attributed tags still participate in pairing so a nested raw tag
        // cannot consume the closing tag of a surrounding semantic underline.
        if ((tag == "</u>" || tag == "</U>") && cmark_node_next(opening) != child &&
            std::string_view(cmark_node_get_literal(opening)).size() == 3 &&
            replaceWithUnderlineTag(opening, "<u>"))
          replaceWithUnderlineTag(child, "</u>");
      }
    } else if (type != CMARK_NODE_IMAGE && cmark_node_first_child(child)) {
      recognizeUnderlineTags(child);
    }
    child = next;
  }
}

void recognizeUnderlines(cmark_node* root, std::string_view source) {
  // Let cmark distinguish HTML tokens from escaped text, code and link destinations.
  if (source.find("<u>") != std::string_view::npos || source.find("<U>") != std::string_view::npos)
    recognizeUnderlineTags(root);
}

void pushRun(std::vector<InlineRun>& runs, const InlineRun& run) {
  if (run.text.empty()) return;
  if (!runs.empty() && runs.back().bold == run.bold && runs.back().italic == run.italic &&
      runs.back().strike == run.strike && runs.back().underline == run.underline &&
      runs.back().code == run.code && runs.back().link == run.link &&
      runs.back().image == run.image && !run.math && !run.footnote && !runs.back().math &&
      !runs.back().footnote && !run.html && !runs.back().html && runs.back().url == run.url) {
    runs.back().text += run.text;
    return;
  }
  runs.push_back(run);
}

// Inline children become styled runs; structural children stay blocks.
void collectInline(cmark_node* node, const InlineRun& inherited, std::vector<InlineRun>& runs) {
  InlineRun base = inherited;
  int underlineDepth = 0;
  for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
       child = cmark_node_next(child)) {
    if (cmark_node_get_type(child) == CMARK_NODE_CUSTOM_INLINE) {
      const char* literal = cmark_node_get_on_enter(child);
      const std::string_view tag = literal ? literal : "";
      if (tag == "<u>" || tag == "</u>") {
        underlineDepth += tag == "<u>" ? 1 : -1;
        base.underline = inherited.underline || underlineDepth > 0;
        continue;
      }
    }
    const std::string_view type = cmark_node_get_type_string(child);
    if (type == "list" || type == "item" || type == "tasklist" || type == "table" ||
        type == "table_row" || type == "table_header" || type == "table_cell") {
      continue;
    }
    const char* literal = cmark_node_get_literal(child);
    if (type == "text" && literal != nullptr) {
      InlineRun run = base;
      run.text = literal;
      pushRun(runs, run);
    } else if (type == "code" && literal != nullptr) {
      InlineRun run = base;
      run.code = true;
      run.text = literal;
      pushRun(runs, run);
    } else if (type == "softbreak") {
      InlineRun run = base;
      run.text = " ";
      pushRun(runs, run);
    } else if (type == "linebreak") {
      InlineRun run = base;
      run.text = "\n";
      pushRun(runs, run);
    } else if (cmark_node_get_type(child) == CMARK_NODE_HTML_INLINE && literal != nullptr) {
      InlineRun run = base;
      run.text = literal;
      run.html = true;
      pushRun(runs, run);
    } else if (type == "emph" || type == "emphasis") {  // cmark-gfm reports emph, not emphasis
      InlineRun run = base;
      run.italic = true;
      collectInline(child, run, runs);
    } else if (type == "strong") {
      InlineRun run = base;
      run.bold = true;
      collectInline(child, run, runs);
    } else if (type == "strikethrough" || type == "strikespan") {
      InlineRun run = base;
      run.strike = true;
      collectInline(child, run, runs);
    } else if (type == "link") {
      InlineRun run = base;
      const char* url = cmark_node_get_url(child);
      const std::string target = url ? url : "";
      if (target.compare(0, 12, "typist-math:") == 0 ||
          target.compare(0, 16, "typist-footnote:") == 0) {
        run.math = target.compare(0, 12, "typist-math:") == 0;
        run.footnote = !run.math;
        run.text = syntaxUnhex(target.substr(run.math ? 12 : 16));
        pushRun(runs, run);
        continue;
      }
      run.link = true;
      if (url != nullptr) run.url = url;
      collectInline(child, run, runs);
    } else if (type == "image") {
      InlineRun run = base;
      run.image = true;
      const char* url = cmark_node_get_url(child);
      if (url != nullptr) run.url = url;
      collectInline(child, run, runs);
    } else {
      collectInline(child, base, runs);
    }
  }
}

std::vector<InlineRun> inlineRuns(cmark_node* node) {
  std::vector<InlineRun> runs;
  collectInline(node, InlineRun(), runs);
  return runs;
}

std::string flattenRuns(const std::vector<InlineRun>& runs) {
  std::string out;
  for (const InlineRun& run : runs) out += run.text;
  return out;
}

// Inline code is fenced with one more backtick than the longest run inside it.
std::string wrapCode(const std::string& text) {
  std::size_t longest = 0;
  std::size_t current = 0;
  for (const char ch : text) {
    if (ch == '`')
      ++current;
    else
      current = 0;
    longest = std::max(longest, current);
  }
  const std::size_t fence = longest + 1;
  const std::string marker(fence, '`');
  const bool pad = !text.empty() && (text.front() == '`' || text.back() == '`');
  if (pad) return marker + " " + text + " " + marker;
  return marker + text + marker;
}

std::string renderRun(const InlineRun& run) {
  if (run.math) return "$" + run.text + "$";
  if (run.footnote) return "[^" + run.text + "]";
  if (run.image) {
    std::string alt;
    for (const char ch : run.text) {
      if (ch == '\\' || ch == '[' || ch == ']') alt += '\\';
      alt += ch;
    }
    return "![" + alt + "](" + run.url + ")";
  }
  std::string body = run.text;
  if (!run.code && !run.html) {
    std::string escaped;
    const auto word = [](unsigned char ch) {
      return ch >= 128 || (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'z') ||
             (ch >= 'A' && ch <= 'Z');
    };
    for (std::size_t i = 0; i < body.size(); ++i) {
      const char ch = body[i];
      const bool innerUnderscore =
          ch == '_' && i > 0 && i + 1 < body.size() && word(body[i - 1]) && word(body[i + 1]);
      if (!innerUnderscore && std::string_view("\\`*_[]<>!~$&").find(ch) != std::string_view::npos)
        escaped += '\\';
      escaped += ch;
    }
    body = std::move(escaped);
  }
  if (!run.code) {
    std::size_t position = 0;
    while ((position = body.find('\n', position)) != std::string::npos) {
      body.insert(position, "  ");
      position += 3;
    }
  }
  if (run.code) body = wrapCode(body);
  if (run.bold) body = "**" + body + "**";
  if (run.italic) body = "*" + body + "*";
  if (run.strike) body = "~~" + body + "~~";
  if (run.link) body = "[" + body + "](" + run.url + ")";
  return body;
}

// Runs carry the styling; the flattened text is the fallback when there are none.
std::string renderInline(const std::vector<InlineRun>& runs, const std::string& fallback) {
  if (runs.empty()) return fallback;
  std::string out;
  std::vector<int> open;
  static const char* opening[] = {"**", "*", "~~", "<u>"};
  static const char* closing[] = {"**", "*", "~~", "</u>"};
  const auto active = [](const InlineRun& run, int style) {
    switch (style) {
      case 0:
        return run.bold;
      case 1:
        return run.italic;
      case 2:
        return run.strike;
      default:
        return run.underline;
    }
  };
  for (std::size_t index = 0; index < runs.size(); ++index) {
    const InlineRun& run = runs[index];
    std::size_t common = 0;
    while (common < open.size() && active(run, open[common])) ++common;
    while (open.size() > common) {
      out += closing[open.back()];
      open.pop_back();
    }
    std::vector<std::pair<int, std::size_t>> additions;
    for (int style = 0; style < 4; ++style) {
      if (!active(run, style) || std::find(open.begin(), open.end(), style) != open.end()) continue;
      std::size_t end = index + 1;
      while (end < runs.size() && active(runs[end], style)) ++end;
      additions.push_back({style, end});
    }
    // Longer-lived styles wrap shorter-lived ones. This reconstructs nesting even
    // when several styles begin on the same run, without adjacent closing/opening
    // markers that would change how CommonMark parses the text.
    std::stable_sort(additions.begin(), additions.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& addition : additions) {
      out += opening[addition.first];
      open.push_back(addition.first);
    }
    InlineRun body = run;
    body.bold = body.italic = body.strike = false;
    out += renderRun(body);
  }
  while (!open.empty()) {
    out += closing[open.back()];
    open.pop_back();
  }
  return out;
}

// Prefer the parser's info string, including fences nested in quotes/lists.
// Keep the source-line fallback for cmark builds that return an empty string.
std::string fenceInfo(cmark_node* node, const std::vector<std::string>& lines) {
  const char* info = cmark_node_get_fence_info(node);
  if (info && *info) return std::string(info).substr(0, std::string(info).find_first_of(" \t"));
  const int line = cmark_node_get_start_line(node);
  if (line < 1 || static_cast<std::size_t>(line) > lines.size()) return "";
  const std::string& raw = lines[static_cast<std::size_t>(line) - 1];
  const std::size_t start = raw.find_first_not_of(" \t");
  if (start == std::string::npos) return "";
  if (raw.compare(start, 3, "```") != 0 && raw.compare(start, 3, "~~~") != 0) return "";
  const auto fenceEnd = raw.find_first_not_of(raw[start], start);
  std::string rest = fenceEnd == std::string::npos ? "" : raw.substr(fenceEnd);
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

std::string_view trimAscii(std::string_view text) {
  constexpr std::string_view space = " \t\r\n";
  const auto start = text.find_first_not_of(space);
  return start == std::string_view::npos
             ? std::string_view()
             : text.substr(start, text.find_last_not_of(space) - start + 1);
}

bool isPageBreakDiv(std::string_view html) {
  // Only an empty standalone div with the pagination declarations is a marker.
  // Do not interpret arbitrary HTML or CSS in the native editor.
  if (html.find_first_of("\r\n") != std::string_view::npos) return false;
  std::string lower(html);
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  const std::string_view text(lower);
  if (text.size() < 5 || text.substr(0, 4) != "<div" || (text[4] != ' ' && text[4] != '\t'))
    return false;
  const auto equal = text.find('=', 4);
  if (equal == std::string_view::npos || trimAscii(text.substr(4, equal - 4)) != "style")
    return false;
  const auto rest = trimAscii(text.substr(equal + 1));
  if (rest.empty() || (rest.front() != '"' && rest.front() != '\'')) return false;
  const auto end = rest.find(rest.front(), 1);
  if (end == std::string_view::npos) return false;
  auto close = trimAscii(rest.substr(end + 1));
  if (close.empty() || close.front() != '>') return false;
  close = trimAscii(close.substr(1));
  if (close.size() < 6 || close.substr(0, 5) != "</div" || trimAscii(close.substr(5)) != ">")
    return false;

  auto declarations = trimAscii(rest.substr(1, end - 1));
  if (declarations.empty()) return false;
  while (!declarations.empty()) {
    const auto separator = declarations.find(';');
    const auto declaration = declarations.substr(0, separator);
    const auto colon = declaration.find(':');
    if (colon == std::string_view::npos) return false;
    const auto property = trimAscii(declaration.substr(0, colon));
    const auto value = trimAscii(declaration.substr(colon + 1));
    if (!((property == "page-break-after" && value == "always") ||
          (property == "break-after" && value == "page")))
      return false;
    if (separator == std::string_view::npos) break;
    declarations = trimAscii(declarations.substr(separator + 1));
  }
  return true;
}

std::string_view pageBreakSource(cmark_node* node) {
  if (cmark_node_get_type(node) != CMARK_NODE_HTML_BLOCK ||
      cmark_node_get_type(cmark_node_parent(node)) != CMARK_NODE_DOCUMENT)
    return {};
  const char* literal = cmark_node_get_literal(node);
  const auto html = literal ? trimAscii(literal) : std::string_view();
  return isPageBreakDiv(html) ? html : std::string_view();
}

Block buildBlock(cmark_node* node, const std::vector<std::string>& lines) {
  Block block;
  const std::string_view type = cmark_node_get_type_string(node);

  if (type == "heading") {
    block.type = BlockType::Heading;
    block.level = cmark_node_get_heading_level(node);
    block.runs = inlineRuns(node);
    block.text = flattenRuns(block.runs);
  } else if (type == "paragraph") {
    block.type = BlockType::Paragraph;
    block.runs = inlineRuns(node);
    block.text = flattenRuns(block.runs);
    if (block.text == "[TOC]" || block.text == "[toc]") {
      block.type = BlockType::TableOfContents;
      block.text.clear();
      block.runs.clear();
    }
  } else if (type == "list") {
    block.type = BlockType::List;
    block.ordered = cmark_node_get_list_type(node) == CMARK_ORDERED_LIST;
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      block.children.push_back(buildBlock(child, lines));
    }
  } else if (type == "item") {
    block.type = BlockType::ListItem;
    // fallthrough below collects inline runs from every child
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      const std::string_view childType = cmark_node_get_type_string(child);
      if (childType == "list") {
        block.children.push_back(buildBlock(child, lines));
        continue;
      }
      if (childType == "tasklist") {
        block.task = true;
        block.checked = cmark_gfm_extensions_get_tasklist_item_checked(child);
      }
      collectInline(child, InlineRun(), block.runs);
    }
    block.text = flattenRuns(block.runs);
  } else if (type == "tasklist") {
    // The GFM tasklist extension replaces the item node itself, so the checked
    // flag comes from the extension accessor rather than the text prefix.
    block.type = BlockType::ListItem;
    block.task = true;
    block.checked = cmark_gfm_extensions_get_tasklist_item_checked(node);
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      const std::string_view childType = cmark_node_get_type_string(child);
      if (childType == "list") {
        block.children.push_back(buildBlock(child, lines));
        continue;
      }
      collectInline(child, InlineRun(), block.runs);
    }
    block.text = flattenRuns(block.runs);
  } else if (type == "code_block") {
    block.type = BlockType::CodeBlock;
    const char* literal = cmark_node_get_literal(node);
    block.text = literal != nullptr ? literal : "";
    block.info = fenceInfo(node, lines);
    if (block.info == "typist-math") {
      block.type = BlockType::Math;
      block.info.clear();
    } else if (block.info == "mermaid") {
      block.type = BlockType::Diagram;
      block.info.clear();
    } else if (block.info.compare(0, 16, "typist-footnote-") == 0) {
      block.type = BlockType::Footnote;
      block.info = syntaxUnhex(block.info.substr(16));
    }
    if ((block.type == BlockType::Math || block.type == BlockType::Footnote) &&
        !block.text.empty() && block.text.back() == '\n')
      block.text.pop_back();
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
    if (type == "table") {
      const uint16_t columns = cmark_gfm_extensions_get_table_columns(node);
      const uint8_t* alignments = cmark_gfm_extensions_get_table_alignments(node);
      for (uint16_t column = 0; column < columns; ++column) {
        const char value = alignments ? static_cast<char>(alignments[column]) : 0;
        block.alignments.push_back(value == 'l' ? 1 : value == 'c' ? 2 : value == 'r' ? 3 : 0);
      }
    }
    for (cmark_node* child = cmark_node_first_child(node); child != nullptr;
         child = cmark_node_next(child)) {
      block.children.push_back(buildBlock(child, lines));
    }
  } else if (type == "table_cell") {
    block.type = BlockType::TableCell;
    block.runs = inlineRuns(node);
    block.text = flattenRuns(block.runs);
  } else if (const auto pageBreak = pageBreakSource(node); !pageBreak.empty()) {
    block.type = BlockType::PageBreak;
    block.text = pageBreak;
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
    if (c == '|')
      out += "\\|";
    else if (c == '\n')
      out += "<br>";
    else
      out += c;
  }
  return out;
}

std::string serializeRow(const Block& row) {
  std::string out = "|";
  for (const Block& cell : row.children)
    out += " " + escapeTableCell(renderInline(cell.runs, cell.text)) + " |";
  out += "\n";
  return out;
}

std::string serializeTable(const Block& table) {
  if (table.children.empty()) return "\n";
  std::string out = serializeRow(table.children.front());
  out += "|";
  for (std::size_t i = 0; i < table.children.front().children.size(); ++i) {
    const int alignment = i < table.alignments.size() ? table.alignments[i] : 0;
    out += alignment == 1   ? " :--- |"
           : alignment == 2 ? " :---: |"
           : alignment == 3 ? " ---: |"
                            : " --- |";
  }
  out += "\n";
  for (std::size_t i = 1; i < table.children.size(); ++i) out += serializeRow(table.children[i]);
  out += "\n";
  return out;
}

std::string serializeBlock(const Block& block, int indent) {
  const std::string prefix(static_cast<std::size_t>(indent), ' ');
  std::string out;

  switch (block.type) {
    case BlockType::Math:
      out += "$$\n" + block.text + "\n$$\n\n";
      break;
    case BlockType::Diagram:
      out += "```mermaid\n" + block.text +
             (block.text.empty() || block.text.back() != '\n' ? "\n" : "") + "```\n\n";
      break;
    case BlockType::Footnote: {
      auto body = block.text;
      std::size_t p = 0;
      while ((p = body.find('\n', p)) != std::string::npos) {
        body.insert(p + 1, "    ");
        p += 5;
      }
      out += "[^" + block.info + "]: " + body + "\n\n";
      break;
    }
    case BlockType::TableOfContents:
      out += "[TOC]\n\n";
      break;
    case BlockType::PageBreak:
      out += block.text + "\n\n";
      break;
    case BlockType::Heading:
      out += prefix + std::string(static_cast<std::size_t>(block.level), '#');
      out += " " + renderInline(block.runs, block.text) + "\n\n";
      break;
    case BlockType::Paragraph:
      out += block.text.empty() ? "\n" : prefix + renderInline(block.runs, block.text) + "\n\n";
      break;
    case BlockType::CodeBlock: {
      const auto fence = codeFence(block.text, block.info);
      out += prefix + fence + block.info + "\n";
      out += block.text;
      if (!block.text.empty() && block.text.back() != '\n') out += "\n";
      out += prefix + fence + "\n\n";
      break;
    }
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
        const int childIndent = indent + static_cast<int>(marker.size());
        auto text = renderInline(item.runs, item.text);
        std::size_t newline = 0;
        while ((newline = text.find('\n', newline)) != std::string::npos) {
          text.insert(++newline, static_cast<std::size_t>(childIndent), ' ');
          newline += static_cast<std::size_t>(childIndent);
        }
        out += text + "\n";
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

std::string escapeHtml(std::string_view text) {
  std::string out;
  for (char c : text) {
    if (c == '&')
      out += "&amp;";
    else if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else
      out += c;
  }
  return out;
}

std::vector<const char*> enabledExtensions(const ParseOptions& options) {
  std::vector<const char*> names;
  if (options.gfmTables) names.push_back("table");
  if (options.gfmTaskList) names.push_back("tasklist");
  if (options.gfmStrikethrough) names.push_back("strikethrough");
  if (options.autolink) names.push_back("autolink");
  return names;
}

}  // namespace

std::string serializeInline(const std::vector<InlineRun>& runs) { return renderInline(runs, {}); }

std::string codeFence(std::string_view code, std::string_view info) {
  const char marker = info.find('`') == std::string_view::npos ? '`' : '~';
  std::size_t longest = 0, run = 0;
  for (const char ch : code) {
    run = ch == marker ? run + 1 : 0;
    longest = std::max(longest, run);
  }
  return std::string(std::max(std::size_t(3), longest + 1), marker);
}

std::vector<ImageSource> imageSources(std::string_view source) {
  std::vector<std::size_t> lines{0};
  for (std::size_t i = 0; i < source.size(); ++i)
    if (source[i] == '\n') lines.push_back(i + 1);
  cmark_gfm_core_extensions_ensure_registered();
  std::unique_ptr<cmark_parser, decltype(&cmark_parser_free)> parser(
      cmark_parser_new(CMARK_OPT_DEFAULT), &cmark_parser_free);
  if (!parser) return {};
  if (auto* table = cmark_find_syntax_extension("table"))
    cmark_parser_attach_syntax_extension(parser.get(), table);
  cmark_parser_feed(parser.get(), source.data(), source.size());
  std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root(cmark_parser_finish(parser.get()),
                                                               &cmark_node_free);
  std::vector<ImageSource> images;
  if (!root) return images;
  auto* iterator = cmark_iter_new(root.get());
  if (!iterator) return images;
  while (cmark_iter_next(iterator) != CMARK_EVENT_DONE) {
    if (cmark_iter_get_event_type(iterator) != CMARK_EVENT_ENTER) continue;
    auto* node = cmark_iter_get_node(iterator);
    if (cmark_node_get_type(node) != CMARK_NODE_IMAGE) continue;
    ImageSource image;
    image.target = cmark_node_get_url(node);
    const int line = cmark_node_get_start_line(node), column = cmark_node_get_start_column(node);
    const int endLine = cmark_node_get_end_line(node), endColumn = cmark_node_get_end_column(node);
    if (line > 0 && endLine >= line && std::size_t(endLine) <= lines.size() && column > 0 &&
        endColumn > 0) {
      const auto start = lines[std::size_t(line - 1)] + std::size_t(column - 1);
      const auto end =
          std::min(source.size(), lines[std::size_t(endLine - 1)] + std::size_t(endColumn));
      if (start < end && source.substr(start, 2) == "![") {
        const auto span = source.substr(start, end - start);
        std::size_t opening = std::string_view::npos;
        int brackets = 1;
        for (std::size_t i = 2; i < span.size(); ++i) {
          if (span[i] == '\\' && i + 1 < span.size()) {
            ++i;
            continue;
          }
          if (span[i] == '[') ++brackets;
          if (span[i] == ']' && --brackets == 0) {
            if (i + 1 < span.size() && span[i + 1] == '(') opening = i;
            break;
          }
        }
        if (opening != std::string_view::npos) {
          std::size_t first = opening + 2;
          while (first < span.size() &&
                 (span[first] == ' ' || span[first] == '\n' || span[first] == '\t'))
            ++first;
          std::size_t last = first;
          if (first < span.size() && span[first] == '<') {
            ++first;
            last = span.find('>', first);
          } else {
            int depth = 0;
            for (; last < span.size(); ++last) {
              const char ch = span[last];
              if (ch == '\\' && last + 1 < span.size()) {
                ++last;
                continue;
              }
              if (ch == '(') ++depth;
              if (ch == ')') {
                if (!depth) break;
                --depth;
              }
              if ((ch == ' ' || ch == '\n' || ch == '\t') && depth == 0) break;
            }
          }
          if (last != std::string_view::npos && last >= first && last < span.size()) {
            image.start = start + first;
            image.length = last - first;
          }
        }
      }
    }
    images.push_back(std::move(image));
  }
  cmark_iter_free(iterator);
  std::sort(images.begin(), images.end(),
            [](const auto& a, const auto& b) { return a.start < b.start; });
  return images;
}

ParseResult parseMarkdown(std::string_view source, const ParseOptions& options) {
  const std::string prepared = prepareAdvancedMarkdown(source);
  source = prepared;
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
  std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root(cmark_parser_finish(parser.get()),
                                                               &cmark_node_free);
  if (!root) {
    result.error = "cmark_parser_finish failed";
    return result;
  }

  recognizeUnderlines(root.get(), source);
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

namespace {
void makeHtmlLiteral(cmark_node* root) {
  // Collect first so replacing a node never invalidates the active iterator.
  std::vector<cmark_node*> rawNodes;
  auto* iterator = cmark_iter_new(root);
  while (cmark_iter_next(iterator) != CMARK_EVENT_DONE) {
    if (cmark_iter_get_event_type(iterator) != CMARK_EVENT_ENTER) continue;
    auto* node = cmark_iter_get_node(iterator);
    const auto type = cmark_node_get_type(node);
    if (type == CMARK_NODE_HTML_INLINE || type == CMARK_NODE_HTML_BLOCK) rawNodes.push_back(node);
  }
  cmark_iter_free(iterator);
  for (auto* node : rawNodes) {
    const char* literal = cmark_node_get_literal(node);
    if (!literal) {
      cmark_node_free(node);
      continue;
    }
    const bool inlineHtml = cmark_node_get_type(node) == CMARK_NODE_HTML_INLINE;
    if (inlineHtml) {
      std::string tag(literal);
      std::transform(tag.begin(), tag.end(), tag.begin(),
                     [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
      if (tag == "<br>" || tag == "<br/>" || tag == "<br />") continue;
    }
    std::unique_ptr<cmark_node, decltype(&cmark_node_free)> replacement(
        cmark_node_new(inlineHtml ? CMARK_NODE_TEXT : CMARK_NODE_CODE_BLOCK), &cmark_node_free);
    if (cmark_node_set_literal(replacement.get(), literal) &&
        cmark_node_replace(node, replacement.get()))
      replacement.release();
    cmark_node_free(node);
  }
}

std::string restoreHtmlBreaks(std::string html) {
  // GFM table cells reject native line-break nodes. Only approved <br> nodes
  // remain raw, so each omission marker from cmark's safe renderer is a break.
  constexpr std::string_view omitted = "<!-- raw HTML omitted -->";
  auto next = html.find(omitted);
  if (next == std::string::npos) return html;
  std::string result;
  result.reserve(html.size());
  std::size_t start = 0;
  while (next != std::string::npos) {
    result.append(html, start, next - start);
    result += "<br />";
    start = next + omitted.size();
    next = html.find(omitted, start);
  }
  result.append(html, start, std::string::npos);
  return result;
}

std::string renderHtml(std::string_view source, const ParseOptions& options, bool preserveRawHtml) {
  const int flags = preserveRawHtml ? CMARK_OPT_UNSAFE : CMARK_OPT_DEFAULT;
  cmark_gfm_core_extensions_ensure_registered();
  cmark_mem* mem = cmark_get_default_mem_allocator();

  std::unique_ptr<cmark_parser, decltype(&cmark_parser_free)> parser(cmark_parser_new(flags),
                                                                     &cmark_parser_free);
  if (!parser) return "";

  cmark_llist* extensions = nullptr;
  for (const char* name : enabledExtensions(options)) {
    cmark_syntax_extension* extension = cmark_find_syntax_extension(name);
    if (extension == nullptr) continue;
    cmark_parser_attach_syntax_extension(parser.get(), extension);
    extensions = cmark_llist_append(mem, extensions, extension);
  }

  cmark_parser_feed(parser.get(), source.data(), source.size());
  std::unique_ptr<cmark_node, decltype(&cmark_node_free)> root(cmark_parser_finish(parser.get()),
                                                               &cmark_node_free);
  if (!root) {
    cmark_llist_free(mem, extensions);
    return "";
  }

  if (!preserveRawHtml) {
    recognizeUnderlines(root.get(), source);
    makeHtmlLiteral(root.get());
  }
  char* html = cmark_render_html(root.get(), flags, extensions);
  std::string out = html != nullptr ? std::string(html) : std::string();
  if (html != nullptr) mem->free(html);
  cmark_llist_free(mem, extensions);
  if (!preserveRawHtml) return restoreHtmlBreaks(std::move(out));
  return out;
}
}  // namespace

std::string toHtml(std::string_view source, const ParseOptions& options) {
  return decorateAdvancedHtml(renderHtml(prepareAdvancedMarkdown(source), options, true));
}

std::string toSemanticHtml(std::string_view source, const ParseOptions& options) {
  return renderHtml(source, options, false);
}

std::string wrapHtmlDocument(std::string_view body, std::string_view title, std::string_view css) {
  std::string out;
  out += "<!doctype html>\n<html lang=\"en\">\n<head>\n";
  out += "<meta charset=\"utf-8\">\n";
  out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
  out += "<title>" + escapeHtml(title) + "</title>\n";
  out += "<style>" + std::string(css) + "</style>\n";
  out += "</head>\n<body>\n";
  out += std::string(body);
  out += "</body>\n</html>\n";
  return out;
}

std::string wrapHtmlDocument(std::string_view body, std::string_view title) {
  return wrapHtmlDocument(
      body, title,
      "body{max-width:68ch;margin:2rem auto;padding:0 1rem;line-height:1.6}"
      "code,pre{font-family:monospace}pre{overflow:auto}"
      "table{border-collapse:collapse}th,td{border:1px solid #bbb;padding:.4rem .6rem}");
}

bool runsEqual(const std::vector<InlineRun>& lhs, const std::vector<InlineRun>& rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const InlineRun& a = lhs[i];
    const InlineRun& b = rhs[i];
    if (a.text != b.text || a.url != b.url || a.bold != b.bold || a.italic != b.italic ||
        a.strike != b.strike || a.underline != b.underline || a.code != b.code ||
        a.link != b.link || a.image != b.image || a.math != b.math || a.footnote != b.footnote ||
        a.html != b.html) {
      return false;
    }
  }
  return true;
}

bool astEqual(const std::vector<Block>& lhs, const std::vector<Block>& rhs) {
  if (lhs.size() != rhs.size()) return false;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    const Block& a = lhs[i];
    const Block& b = rhs[i];
    if (a.type != b.type || a.level != b.level || a.ordered != b.ordered || a.task != b.task ||
        a.checked != b.checked || a.text != b.text || a.info != b.info ||
        !runsEqual(a.runs, b.runs)) {
      return false;
    }
    if (!astEqual(a.children, b.children)) return false;
    if (a.type == BlockType::Table && !a.children.empty()) {
      for (std::size_t column = 0; column < a.children.front().children.size(); ++column) {
        const int left = column < a.alignments.size() ? a.alignments[column] : 0;
        const int right = column < b.alignments.size() ? b.alignments[column] : 0;
        if (left != right) return false;
      }
    }
  }
  return true;
}

}  // namespace typist::core
