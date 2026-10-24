#pragma once

#include "markdown.h"

#include <QTextDocument>
#include <vector>

namespace typist::editor {

// Builds a QTextDocument from parsed Markdown blocks. This is the M0 feasibility
// path: headings, paragraphs, code and lists are rendered; tables are probed
// separately below and by tests/editor/roundtrip_spike.cpp.
void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document);

// Native table probe: inserts a rows x cols table and reports whether the Qt
// document model kept the cells addressable.
bool insertTableProbe(QTextDocument* document, int rows, int cols);

}  // namespace typist::editor
