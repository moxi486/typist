#include "Toc.h"

#include <QTextBlock>
#include <QTextBlockFormat>

namespace typist::ui {

QVector<TocEntry> outlineFromDocument(const QTextDocument* document, int maxEntries) {
  QVector<TocEntry> entries;
  if (document == nullptr) return entries;
  for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
    const int level = block.blockFormat().headingLevel();
    if (level <= 0) continue;
    TocEntry entry;
    entry.level = level;
    entry.text = block.text().trimmed();
    entry.blockNumber = block.blockNumber();
    entry.position = block.position();
    entries.append(entry);
    if (maxEntries > 0 && entries.size() >= maxEntries) break;
  }
  return entries;
}

}  // namespace typist::ui
