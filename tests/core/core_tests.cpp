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
  for (const auto& entry : std::filesystem::directory_iterator(dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".md") out.push_back(entry.path());
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool containsBlock(const std::vector<typist::core::Block>& blocks,
                   typist::core::BlockType type, const std::string& text) {
  for (const auto& block : blocks) {
    if (block.type == type && (text.empty() || block.text == text)) return true;
    if (containsBlock(block.children, type, text)) return true;
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

  for (const auto& file : mdFilesIn(corpus)) {
    const std::string source = readFile(file);
    const auto first = typist::core::parseMarkdown(source);
    const auto again = typist::core::parseMarkdown(source);
    check(first.ok && typist::core::astEqual(first.blocks, again.blocks),
          "parse stable: " + file.filename().string());
    const std::string roundTripped = typist::core::serializeMarkdown(first.blocks);
    const auto reparsed = typist::core::parseMarkdown(roundTripped);
    check(reparsed.ok && typist::core::astEqual(first.blocks, reparsed.blocks),
          "round trip: " + file.filename().string());
  }

  const auto table = typist::core::parseMarkdown(readFile(corpus / "gfm" / "table.md"));
  check(table.ok && containsBlock(table.blocks, typist::core::BlockType::Unsupported, "table"),
        "gfm table parsed");

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

  std::cout << (g_failures == 0 ? "all tests passed\n"
                                : "failed: " + std::to_string(g_failures) + "\n");
  return g_failures == 0 ? 0 : 1;
}
