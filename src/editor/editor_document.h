#pragma once

#include "markdown.h"

#include <QString>
#include <QTextCursor>
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
constexpr uint kImageProperty = 0x100006;       // value: image target
constexpr uint kImageAltProperty = 0x100007;    // value: image alt text        // value: image destination, on an anchored run
constexpr uint kTaskMarkProperty = 0x100008;    // value: checked state of the rendered checkbox glyph

// Qt exposes no undo history cap, so the step count is checked against this limit
// and the undo stack dropped when it is exceeded.
constexpr int kUndoStepLimit = 200;

// Drops the undo stack once the recorded step count passes the limit, keeping a
// long editing session from growing memory without bound.

// Builds a QTextDocument from parsed Markdown blocks.
void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document);

// Serializes a document back to Markdown: tables emit their header row and a
// | --- | divider, cells escape '|' as \| and line breaks as <br>.
QString serializeDocument(const QTextDocument* document);

// Parses Markdown and inserts the blocks at the cursor, on one undo step. The cursor
// lands after the pasted content. Returns false on a parse failure without touching
// the document.
bool insertMarkdownAtCursor(const QString& source, QTextCursor& cursor, QString* error);

// Task state of a list item block lives on its block format; false when the block
// is not a task item. checkedOut reports the current state.
bool taskCheckboxState(const QTextDocument* document, int blockNumber, bool* checkedOut);

// Flip the task state of the block at blockNumber; false when it is not a task item.
bool toggleTaskCheckbox(QTextDocument* document, int blockNumber);

// Parses Markdown and rebuilds the document in place, clearing undo history so a
// switch or load never becomes an undo step. Returns false on a parse error and
// leaves the document untouched.
bool loadMarkdown(const QString& source, QTextDocument* document, QString* error);

void boundUndoHistory(QTextDocument* document);

// Native table probe: inserts a rows x cols table and reports whether the cells
// stayed addressable and readable through the document text.
bool insertTableProbe(QTextDocument* document, int rows, int cols);

}  // namespace typist::editor
