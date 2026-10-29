#pragma once
#include <string>
#include <string_view>

namespace typist::core {
std::string syntaxHex(std::string_view text);
std::string syntaxUnhex(std::string_view text);
std::string prepareAdvancedMarkdown(std::string_view source);
std::string decorateAdvancedHtml(std::string html);
}  // namespace typist::core
