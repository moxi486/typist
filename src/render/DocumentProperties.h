#pragma once

#include <QTextBlock>

namespace typist::render {

// QTextFormat user properties shared by the editor and document renderers.
constexpr int kCodeBlockProperty = 0x100001;           // group id
constexpr int kQuoteProperty = 0x100002;               // group id
constexpr int kCodeInfoProperty = 0x100005;            // fence info string
constexpr int kFootnoteDefinitionProperty = 0x10000e;  // footnote id
constexpr int kPageBreakProperty = 0x100017;           // original Markdown
constexpr char16_t kPageBreakMarker = u'\u200b';

inline bool isPageBreak(const QTextBlock& block) {
  return block.blockFormat().hasProperty(kPageBreakProperty) &&
         block.text() == QChar(kPageBreakMarker);
}

}  // namespace typist::render
