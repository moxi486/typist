#include "markdown.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
int g_failures = 0;

void check(bool condition, const std::string& what) {
  if (!condition) {
    ++g_failures;
    std::cout << "FAIL: " << what << "\n";
  } else {
    std::cout << "ok: " << what << "\n";
  }
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream stream(path);
  return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

std::vector<std::filesystem::path> mdFilesIn(const std::filesystem::path& dir) {
  std::vector<std::filesystem::path> out;
  if (!std::filesystem::exists(dir)) return out;
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".md") out.push_back(entry.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

void checkRoundTrip(const std::filesystem::path& file) {
  const std::string source = readFile(file);
  const auto first = typist::core::parseMarkdown(source);
  const auto again = typist::core::parseMarkdown(source);
  check(first.ok && typist::core::astEqual(first.blocks, again.blocks),
        "parse stable: " + file.filename().string());
  const auto reparsed = typist::core::parseMarkdown(typist::core::serializeMarkdown(first.blocks));
  check(reparsed.ok && typist::core::astEqual(first.blocks, reparsed.blocks),
        "round trip: " + file.filename().string());
}

bool containsBlock(const std::vector<typist::core::Block>& blocks, typist::core::BlockType type) {
  for (const auto& block : blocks) {
    if (block.type == type) return true;
    if (containsBlock(block.children, type)) return true;
  }
  return false;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout << "usage: typist_core_tests <corpus-dir>\n";
    return 2;
  }
  const std::filesystem::path corpus = argv[1];

  for (const auto& file : mdFilesIn(corpus)) checkRoundTrip(file);
  for (const auto& file : mdFilesIn(corpus / "gfm")) checkRoundTrip(file);

  const auto table = typist::core::parseMarkdown(readFile(corpus / "gfm" / "table.md"));
  check(table.ok && containsBlock(table.blocks, typist::core::BlockType::Table),
        "gfm table parsed");
  check(table.ok && containsBlock(table.blocks, typist::core::BlockType::TableCell),
        "gfm table cells parsed");

  const auto tasks = typist::core::parseMarkdown(readFile(corpus / "gfm" / "tasklist.md"));
  bool checkedSeen = false;
  bool uncheckedSeen = false;
  for (const auto& list : tasks.blocks) {
    for (const auto& item : list.children) {
      if (item.task && item.checked) checkedSeen = true;
      if (item.task && !item.checked) uncheckedSeen = true;
    }
  }
  check(tasks.ok && checkedSeen && uncheckedSeen, "gfm task list parsed");

  const std::string tableHtml = typist::core::toHtml(readFile(corpus / "gfm" / "table.md"));
  check(tableHtml.find("<table>") != std::string::npos, "html table rendered");
  check(tableHtml.find("<th>") != std::string::npos, "html table header rendered");

  const std::string escapedHtml =
      typist::core::toHtml(readFile(corpus / "gfm" / "table_escapes.md"));
  check(escapedHtml.find("a | b") != std::string::npos, "html escaped pipe becomes text");
  check(escapedHtml.find("<br>") != std::string::npos, "html keeps br inside cell");

  const std::string taskHtml = typist::core::toHtml(readFile(corpus / "gfm" / "tasklist.md"));
  check(taskHtml.find("type=\"checkbox\"") != std::string::npos, "html checkbox rendered");
  check(taskHtml.find("checked") != std::string::npos, "html checked box rendered");

  const std::string codeHtml = typist::core::toHtml(readFile(corpus / "code_info.md"));
  check(codeHtml.find("language-c") != std::string::npos, "html fence info becomes class");

  const std::string page = typist::core::wrapHtmlDocument(tableHtml, "Doc <title>");
  check(page.find("<meta charset=\"utf-8\">") != std::string::npos, "html page declares charset");
  check(page.find("<title>Doc &lt;title&gt;</title>") != std::string::npos,
        "html page escapes title");

  std::cout << (g_failures == 0 ? "all tests passed\n"
                                : "failed: " + std::to_string(g_failures) + "\n");
  return g_failures == 0 ? 0 : 1;
}
