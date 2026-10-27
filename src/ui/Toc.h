#pragma once

#include <QString>
#include <QTextDocument>
#include <QVector>

namespace typist::ui {

struct TocEntry {
  int level = 0;
  QString text;
  int blockNumber = 0;
  int position = 0;
};

// Headings in document order, taken from the block format heading level.
QVector<TocEntry> outlineFromDocument(const QTextDocument* document, int maxEntries = 400);

}  // namespace typist::ui
