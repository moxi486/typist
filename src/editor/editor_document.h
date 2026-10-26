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
constexpr uint kCodeInfoProperty = 0x100005;   // value: fence info string, first block of a group

// Builds a QTextDocument from parsed Markdown blocks.
void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document);

// Serializes a document back to Markdown: tables emit their header row and a
// | --- | divider, cells escape '|' as \| and line breaks as <br>.
QString serializeDocument(const QTextDocument* document);

// Parses Markdown and rebuilds the document in place, clearing undo history so a
// switch or load never becomes an undo step. Returns false on a parse error and
// leaves the document untouched.
bool loadMarkdown(const QString& source, QTextDocument* document, QString* error);

// Native table probe: inserts a rows x cols table and reports whether the cells
// stayed addressable and readable through the document text.
bool insertTableProbe(QTextDocument* document, int rows, int cols);

}  // namespace typist::editor
