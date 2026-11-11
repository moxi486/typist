#pragma once

namespace typist::render {

// QTextFormat user properties shared by the editor and document renderers.
constexpr int kCodeBlockProperty = 0x100001;           // group id
constexpr int kQuoteProperty = 0x100002;               // group id
constexpr int kCodeInfoProperty = 0x100005;            // fence info string
constexpr int kFootnoteDefinitionProperty = 0x10000e;  // footnote id

}  // namespace typist::render
