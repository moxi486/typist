#include "advanced_syntax.h"

#include "markdown.h"

#include <cmark-gfm.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <regex>
#include <sstream>
#include <vector>

namespace typist::core {
std::string syntaxHex(std::string_view text) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (unsigned char c : text) {
    out += digits[c >> 4];
    out += digits[c & 15];
  }
  return out;
}
std::string syntaxUnhex(std::string_view text) {
  auto digit = [](char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
  };
  if (text.size() % 2) return {};
  std::string out;
  for (std::size_t i = 0; i < text.size(); i += 2) {
    const int a = digit(text[i]), b = digit(text[i + 1]);
    if (a < 0 || b < 0) return {};
    out += static_cast<char>(a * 16 + b);
  }
  return out;
}
namespace {
std::string trim(const std::string& line) {
  const auto a = line.find_first_not_of(" \t\r"), b = line.find_last_not_of(" \t\r");
  return a == std::string::npos ? "" : line.substr(a, b - a + 1);
}
std::string fence(const std::string& tag, const std::string& body) {
  std::string marker = "```";
  while (body.find(marker) != std::string::npos) marker += '`';
  return "\n" + marker + tag + "\n" + body + (body.empty() || body.back() != '\n' ? "\n" : "") +
         marker + "\n\n";
}
std::string inlineSyntax(const std::string& line) {
  std::string out;
  for (std::size_t i = 0; i < line.size();) {
    if (line[i] == '\\' && i + 1 < line.size()) {
      out += line.substr(i, 2);
      i += 2;
      continue;
    }
    if (line[i] == '`') {
      std::size_t end = i;
      while (end < line.size() && line[end] == '`') ++end;
      const std::string marker = line.substr(i, end - i);
      const auto close = line.find(marker, end);
      if (close == std::string::npos) {
        out += line.substr(i);
        break;
      }
      out += line.substr(i, close + marker.size() - i);
      i = close + marker.size();
      continue;
    }
    if (line.compare(i, 2, "[^") == 0) {
      const auto close = line.find(']', i + 2);
      if (close != std::string::npos && close > i + 2) {
        const auto label = line.substr(i + 2, close - i - 2);
        out += "[note](typist-footnote:" + syntaxHex(label) + ")";
        i = close + 1;
        continue;
      }
    }
    if (line[i] == '$' && i + 1 < line.size() &&
        !std::isspace(static_cast<unsigned char>(line[i + 1]))) {
      auto close = i + 1;
      while ((close = line.find('$', close)) != std::string::npos) {
        if (line[close - 1] == '\\') {
          ++close;
          continue;
        }
        if (line.substr(i + 1, close - i - 1).find('`') != std::string::npos) {
          close = std::string::npos;
          break;
        }
        if (close > i + 1 && !std::isspace(static_cast<unsigned char>(line[close - 1])) &&
            (close + 1 == line.size() ||
             !std::isdigit(static_cast<unsigned char>(line[close + 1])))) {
          out += "[formula](typist-math:" + syntaxHex(line.substr(i + 1, close - i - 1)) + ")";
          i = close + 1;
          break;
        }
        close = std::string::npos;
        break;
      }
      if (close != std::string::npos) continue;
    }
    out += line[i++];
  }
  return out;
}
std::string replaceMatches(const std::string& in, const std::regex& pattern,
                           const std::function<std::string(const std::smatch&)>& replace) {
  std::string out;
  std::size_t last = 0;
  for (std::sregex_iterator it(in.begin(), in.end(), pattern), end; it != end; ++it) {
    out += in.substr(last, it->position() - last);
    out += replace(*it);
    last = it->position() + it->length();
  }
  return out + in.substr(last);
}
std::string escape(const std::string& text) {
  std::string out;
  for (char c : text)
    out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string(1, c);
  return out;
}
std::string unescape(std::string text) {
  for (const auto& pair : {std::pair<std::string, std::string>{"&lt;", "<"},
                           {"&gt;", ">"},
                           {"&quot;", "\""},
                           {"&#39;", "'"},
                           {"&amp;", "&"}}) {
    std::size_t p = 0;
    while ((p = text.find(pair.first, p)) != std::string::npos) {
      text.replace(p, pair.first.size(), pair.second);
      p += pair.second.size();
    }
  }
  return text;
}
}  // namespace
std::string prepareAdvancedMarkdown(std::string_view source) {
  // Keep fenced/indented code literal. Special inline content is protected before
  // CommonMark consumes backslash escapes or interprets TeX punctuation as markup.
  if (source.find('$') == std::string_view::npos && source.find("[^") == std::string_view::npos)
    return std::string(source);
  std::vector<std::string> lines;
  std::istringstream stream{std::string(source)};
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  std::vector<bool> protectedLines(lines.size(), false);
  // Let CommonMark identify code nested in quotes/lists and multiline code spans.
  // A leading-fence-only scanner cannot protect those source ranges reliably.
  cmark_node* document = cmark_parse_document(source.data(), source.size(), CMARK_OPT_DEFAULT);
  if (document) {
    cmark_iter* iter = cmark_iter_new(document);
    while (cmark_iter_next(iter) != CMARK_EVENT_DONE) {
      cmark_node* node = cmark_iter_get_node(iter);
      const auto type = cmark_node_get_type(node);
      const int first = cmark_node_get_start_line(node), last = cmark_node_get_end_line(node);
      if (type == CMARK_NODE_CODE_BLOCK || type == CMARK_NODE_HTML_BLOCK ||
          (type == CMARK_NODE_CODE && first != last))
        for (int line = std::max(1, first); line <= last && line <= static_cast<int>(lines.size());
             ++line)
          protectedLines[line - 1] = true;
    }
    cmark_iter_free(iter);
    cmark_node_free(document);
  }
  std::string out, codeMarker;
  static const std::regex definition(R"(^ {0,3}\[\^([^\]\s]+)\]:[ \t]*(.*)$)");
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (protectedLines[i]) {
      out += lines[i] + "\n";
      continue;
    }
    const auto clean = trim(lines[i]);
    const auto first = lines[i].find_first_not_of(' ');
    if (!codeMarker.empty()) {
      out += lines[i] + "\n";
      if (clean.find_first_not_of(codeMarker[0]) == std::string::npos &&
          clean.size() >= codeMarker.size())
        codeMarker.clear();
      continue;
    }
    if (first != std::string::npos && first <= 3 &&
        (clean.compare(0, 3, "```") == 0 || clean.compare(0, 3, "~~~") == 0)) {
      codeMarker = clean.substr(0, clean.find_first_not_of(clean[0]));
      out += lines[i] + "\n";
      continue;
    }
    if (lines[i].compare(0, 4, "    ") == 0 || (!lines[i].empty() && lines[i][0] == '\t')) {
      out += lines[i] + "\n";
      continue;
    }
    if (clean.compare(0, 2, "$$") == 0) {
      std::string body;
      std::size_t end = i;
      if (clean.size() > 3 && clean.compare(clean.size() - 2, 2, "$$") == 0)
        body = clean.substr(2, clean.size() - 4);
      else if (clean == "$$") {
        for (end = i + 1; end < lines.size() && trim(lines[end]) != "$$"; ++end)
          body += lines[end] + "\n";
      } else
        end = lines.size();
      if (end < lines.size()) {
        out += fence("typist-math", body);
        i = end;
        continue;
      }
    }
    std::smatch match;
    if (std::regex_match(lines[i], match, definition)) {
      std::string body = match[2];
      while (i + 1 < lines.size()) {
        const auto& next = lines[i + 1];
        if (next.compare(0, 4, "    ") == 0) {
          body += "\n" + next.substr(4);
          ++i;
        } else if (!next.empty() && next[0] == '\t') {
          body += "\n" + next.substr(1);
          ++i;
        } else if (trim(next).empty() && i + 2 < lines.size() &&
                   (lines[i + 2].compare(0, 4, "    ") == 0 ||
                    lines[i + 2].compare(0, 1, "\t") == 0)) {
          body += "\n";
          ++i;
        } else
          break;
      }
      out += fence("typist-footnote-" + syntaxHex(match[1].str()), body);
      continue;
    }
    out += inlineSyntax(lines[i]) + "\n";
  }
  return out;
}
std::string decorateAdvancedHtml(std::string html) {
  int heading = 0;
  std::string toc = "<nav class=\"table-of-contents\"><ul>";
  html =
      replaceMatches(html, std::regex(R"(<h([1-6])>([\s\S]*?)</h\1>)"), [&](const std::smatch& m) {
        const auto id = "heading-" + std::to_string(++heading);
        toc += "<li style=\"margin-left:" + std::to_string((std::stoi(m[1]) - 1) * 16) +
               "px\"><a href=\"#" + id + "\">" + m[2].str() + "</a></li>";
        return "<h" + m[1].str() + " id=\"" + id + "\">" + m[2].str() + "</h" + m[1].str() + ">";
      });
  toc += "</ul></nav>";
  html = replaceMatches(html, std::regex(R"(<p>\[(?:TOC|toc)\]</p>)"),
                        [&](const std::smatch&) { return toc; });
  html = replaceMatches(html, std::regex(R"rx(<a href="typist-math:([0-9a-f]+)">formula</a>)rx"),
                        [](const std::smatch& m) {
                          return "<span class=\"typist-math\" data-source=\"" + m[1].str() + "\">" +
                                 escape(syntaxUnhex(m[1].str())) + "</span>";
                        });
  html = replaceMatches(html, std::regex(R"rx(<a href="typist-footnote:([0-9a-f]+)">note</a>)rx"),
                        [](const std::smatch& m) {
                          return "<sup><a href=\"#fn-" + m[1].str() + "\">[" +
                                 escape(syntaxUnhex(m[1].str())) + "]</a></sup>";
                        });
  html = replaceMatches(
      html,
      std::regex(
          R"rx(<pre><code class="language-(typist-math|mermaid|typist-footnote-[0-9a-f]+)">([\s\S]*?)</code></pre>)rx"),
      [](const std::smatch& m) {
        const auto tag = m[1].str(), body = unescape(m[2]);
        if (tag.find("typist-footnote-") == 0) {
          const auto id = tag.substr(16);
          return "<aside class=\"footnote\" id=\"fn-" + id + "\"><b>[" + escape(syntaxUnhex(id)) +
                 "]</b> " + toHtml(body) + "</aside>";
        }
        return "<div class=\"typist-" + std::string(tag == "mermaid" ? "diagram" : "math") +
               "\" data-source=\"" + syntaxHex(body) + "\"><pre>" + escape(body) + "</pre></div>";
      });
  return html;
}
}  // namespace typist::core
