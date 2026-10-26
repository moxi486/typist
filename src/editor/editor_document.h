#pragma once

#include "markdown.h"

#include <QString>
#include <QTextDocument>
#include <vector>

namespace typist::editor {

// Structure Qt does not model natively is tagged through QTextFormat custom
// property ids, which must stay above QTextFormat::UserProperty (0x100000):
// ids below it fall inside Qt's own property enum ranges. Code and quote blocks
// carry a group id as the property value so adjacent blocks never merge.
constexpr uint kCodeBlockProperty = 0x100001;  // value: group id
constexpr uint kQuoteProperty = 0x100002;      // value: group id
constexpr uint kRuleProperty = 0x100003;
constexpr uint kTaskProperty = 0x100004;       // value: bool checked

// Builds a QTextDocument from parsed Markdown blocks.
void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document);

// Serializes a document back to Markdown. Table cells are skipped rather than
// emitted: table fidelity lives in the block tree (docs/ROADMAP.md open item).
QString serializeDocument(const QTextDocument* document);

// Native table probe: inserts a rows x cols table and reports whether the cells
// stayed addressable and readable through the document text.
bool insertTableProbe(QTextDocument* document, int rows, int cols);

}  // namespace typist::editor
