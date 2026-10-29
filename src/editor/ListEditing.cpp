#include "ListEditing.h"

#include "editor_document.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QTextList>

namespace typist::editor {
namespace {
QVector<QTextBlock> selected(const QTextCursor& cursor) {
  QVector<QTextBlock> blocks;
  const int end = cursor.hasSelection() ? cursor.selectionEnd() - 1 : cursor.position();
  for (auto block = cursor.document()->findBlock(cursor.selectionStart());
       block.isValid() && block.position() <= end; block = block.next())
    blocks.append(block);
  return blocks;
}
int level(const QTextBlock& block) {
  return block.textList() ? block.textList()->format().indent() : 0;
}
void attach(const QTextBlock& block, const QTextListFormat& format) {
  // A sibling can be separated from this item by the sibling's descendants.
  auto previous = block.previous();
  while (level(previous) > format.indent()) previous = previous.previous();
  auto* list = previous.textList();
  if (list && list->format().indent() == format.indent() &&
      list->format().style() == format.style())
    list->add(block);
  else
    QTextCursor(block).createList(format);
}
void removeMarker(const QTextBlock& block) {
  const int length = listContentStart(block) - block.position();
  if (!length) return;
  QTextCursor marker(block);
  marker.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, length);
  marker.removeSelectedText();
}
void insertMarker(const QTextBlock& block, bool checked) {
  QTextCursor marker(block);
  QTextCharFormat format;
  format.setProperty(kTaskMarkProperty, checked);
  marker.insertText(QString(checked ? QChar(0x2611) : QChar(0x2610)) + QLatin1Char(' '), format);
}
}  // namespace

int listContentStart(const QTextBlock& block) {
  if (block.blockFormat().hasProperty(kTaskProperty)) {
    const auto text = block.text();
    if (text.startsWith(QChar(0x2610)) || text.startsWith(QChar(0x2611)))
      return block.position() + (text.size() > 1 && text[1].isSpace() ? 2 : 1);
  }
  return block.position();
}

bool selectionHasList(const QTextCursor& cursor) {
  for (const auto& block : selected(cursor))
    if (block.textList()) return true;
  return false;
}

bool setListStyle(QTextCursor& cursor, ParagraphStyle style) {
  const auto blocks = selected(cursor);
  for (const auto& block : blocks)
    if (QTextCursor(block).currentTable()) return false;
  const bool task = style == ParagraphStyle::TaskList;
  cursor.beginEditBlock();
  for (const auto& block : blocks) {
    const int indent = qMax(1, level(block));
    const bool wasTask = block.blockFormat().hasProperty(kTaskProperty);
    const bool checked = block.blockFormat().boolProperty(kTaskProperty);
    if (!block.textList()) {
      QTextCursor body(block);
      setParagraphStyle(body, ParagraphStyle::Body);
    }
    if (wasTask && !task) removeMarker(block);
    auto format = block.blockFormat();
    format.setBottomMargin(5);
    format.setLineHeight(160, QTextBlockFormat::ProportionalHeight);
    format.setIndent(0);
    if (task)
      format.setProperty(kTaskProperty, wasTask && checked);
    else
      format.clearProperty(kTaskProperty);
    QTextCursor line(block);
    line.setBlockFormat(format);
    QTextListFormat listFormat;
    listFormat.setIndent(indent);
    listFormat.setStyle(task                                   ? QTextListFormat::ListStyleUndefined
                        : style == ParagraphStyle::OrderedList ? QTextListFormat::ListDecimal
                                                               : QTextListFormat::ListDisc);
    attach(block, listFormat);
    if (task && !wasTask) insertMarker(block, false);
  }
  if (!cursor.hasSelection() && cursor.position() < listContentStart(cursor.block()))
    cursor.setPosition(listContentStart(cursor.block()));
  cursor.endEditBlock();
  return true;
}

bool indentList(QTextCursor& cursor, int delta) {
  if (delta != -1 && delta != 1) return false;
  auto blocks = selected(cursor);
  if (blocks.isEmpty()) return false;
  int minimum = level(blocks.first());
  for (const auto& block : blocks) {
    if (!level(block) || QTextCursor(block).currentTable()) return false;
    minimum = qMin(minimum, level(block));
  }
  // Moving a parent carries its descendants, including children beyond the selection.
  for (auto next = blocks.last().next(); level(next) > minimum; next = next.next())
    blocks.append(next);
  if (delta > 0) {
    auto previous = blocks.first().previous();
    while (level(previous) > level(blocks.first())) previous = previous.previous();
    if (level(previous) != level(blocks.first())) return false;
  }
  QVector<QTextListFormat> formats;
  for (const auto& block : blocks) {
    auto format = block.textList()->format();
    format.setIndent(format.indent() + delta);
    formats.append(format);
  }
  cursor.beginEditBlock();
  for (int index = 0; index < blocks.size(); ++index) {
    if (formats[index].indent() < 1) {
      QTextCursor body(blocks[index]);
      setParagraphStyle(body, ParagraphStyle::Body);
    } else
      attach(blocks[index], formats[index]);
  }
  cursor.endEditBlock();
  return true;
}

bool setTaskStatus(QTextCursor& cursor, TaskStatus status) {
  const auto blocks = selected(cursor);
  bool found = false, allDone = true;
  for (const auto& block : blocks)
    if (block.blockFormat().hasProperty(kTaskProperty)) {
      found = true;
      allDone &= block.blockFormat().boolProperty(kTaskProperty);
    }
  if (!found) return false;
  const bool done = status == TaskStatus::Done || (status == TaskStatus::Toggle && !allDone);
  cursor.beginEditBlock();
  for (const auto& block : blocks)
    if (block.blockFormat().hasProperty(kTaskProperty) &&
        block.blockFormat().boolProperty(kTaskProperty) != done)
      toggleTaskCheckbox(cursor.document(), block.blockNumber());
  cursor.endEditBlock();
  return true;
}

bool insertListItem(QTextCursor& cursor) {
  if (!cursor.block().textList() || cursor.hasSelection()) return false;
  const bool task = cursor.blockFormat().hasProperty(kTaskProperty);
  const int contentStart = listContentStart(cursor.block());
  if (cursor.block().text().mid(contentStart - cursor.block().position()).trimmed().isEmpty())
    return indentList(cursor, -1);
  cursor.beginEditBlock();
  if (cursor.position() < contentStart) cursor.setPosition(contentStart);
  auto format = cursor.blockFormat();
  if (task) format.setProperty(kTaskProperty, false);
  cursor.insertBlock(format, QTextCharFormat());
  if (task) insertMarker(cursor.block(), false);
  if (task) cursor.setPosition(listContentStart(cursor.block()));
  cursor.setCharFormat(QTextCharFormat());
  cursor.endEditBlock();
  return true;
}
}  // namespace typist::editor
