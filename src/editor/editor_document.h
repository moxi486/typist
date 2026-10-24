#pragma once

#include "markdown.h"

#include <QString>
#include <QTextDocument>
#include <vector>

namespace typist::editor {

// Structure Qt does not model natively is tagged on QTextBlockFormat user
// properties so serializeDocument can recognize it. Code and quote blocks carry
// a group id as the property value so adjacent blocks never merge into one group.
constexpr uint kCodeBlockProperty = 0x4001;  // value: group id
constexpr uint kQuoteProperty = 0x4002;      // value: group id
constexpr uint kRuleProperty = 0x4003;
constexpr uint kTaskProperty = 0x4004;       // value: bool checked

// Builds a QTextDocument from parsed Markdown blocks.
void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document);

// Serializes a document back to Markdown. Table cells are skipped rather than
// emitted: table fidelity lives in the block tree (docs/ROADMAP.md open item).
QString serializeDocument(const QTextDocument* document);

// Native table probe: inserts a rows x cols table and reports whether the cells
// stayed addressable and readable through the document text.
bool insertTableProbe(QTextDocument* document, int rows, int cols);

}  // namespace typist::editor
