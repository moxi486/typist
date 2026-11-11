#include "Editing.h"

#include "CodeEditing.h"
#include "ListEditing.h"
#include "TableEditing.h"
#include "editor_document.h"

#include <QFontDatabase>
#include <QTextBlock>
#include <QTextBoundaryFinder>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>
#include <atomic>

namespace typist::editor {
namespace {
std::atomic<int> nextGroup{1000000};

QVector<QTextBlock> selectedBlocks(const QTextCursor& cursor) {
  QVector<QTextBlock> blocks;
  const int end = cursor.hasSelection() ? cursor.selectionEnd() - 1 : cursor.position();
  for (QTextBlock block = cursor.document()->findBlock(cursor.selectionStart()); block.isValid();
       block = block.next()) {
    if (block.position() > end) break;
    blocks.append(block);
  }
  return blocks;
}

void stripTaskMarker(QTextBlock block) {
  if (!block.blockFormat().hasProperty(kTaskProperty)) return;
  if (block.text().startsWith(QChar(0x2610)) || block.text().startsWith(QChar(0x2611))) {
    QTextCursor mark(block);
    mark.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor,
                      block.text().size() > 1 && block.text().at(1).isSpace() ? 2 : 1);
    mark.removeSelectedText();
  }
}

void styleCharacters(QTextBlock block, int heading, bool code, bool wasCode) {
  struct Span {
    int position;
    int length;
    QTextCharFormat format;
  };
  QVector<Span> spans;
  for (auto it = block.begin(); !it.atEnd(); ++it) {
    QTextFragment run = it.fragment();
    if (run.isValid()) spans.append({run.position(), run.length(), run.charFormat()});
  }
  for (const Span& span : spans) {
    QTextCharFormat format = span.format;
    const bool semanticBold =
        format.fontWeight() >= QFont::Bold && !format.boolProperty(kPresentationBoldProperty);
    if (code) {
      format = QTextCharFormat();
      format.setFontFamilies(
          {QStringLiteral("Consolas"), QStringLiteral("Menlo"), QStringLiteral("monospace")});
      format.setFontPointSize(10.5);
    } else {
      if (wasCode) format.clearProperty(QTextFormat::FontFamilies);
      format.clearProperty(QTextFormat::FontPointSize);
      if (heading > 0) {
        static const double sizes[] = {28, 22, 18, 15, 13, 12};
        format.setFontPointSize(sizes[qBound(1, heading, 6) - 1]);
        format.setFontWeight(QFont::Bold);
        format.setProperty(kPresentationBoldProperty, !semanticBold);
      } else if (format.boolProperty(kPresentationBoldProperty)) {
        format.setFontWeight(QFont::Normal);
        format.clearProperty(kPresentationBoldProperty);
      }
    }
    QTextCursor run(block);
    run.setPosition(span.position);
    run.setPosition(span.position + span.length, QTextCursor::KeepAnchor);
    run.setCharFormat(format);
  }
}

QTextCharFormat inlineScopeStyle(const QTextCharFormat& format) {
  QTextCharFormat style;
  if (format.fontWeight() >= QFont::Bold && !format.boolProperty(kPresentationBoldProperty))
    style.setFontWeight(QFont::Bold);
  if (format.fontItalic()) style.setFontItalic(true);
  if (format.fontStrikeOut()) style.setFontStrikeOut(true);
  if (isInlineCodeFormat(format)) style.setFontFixedPitch(true);
  if (format.isAnchor()) style.setAnchorHref(format.anchorHref());
  for (const auto property : {kMathSourceProperty, kImageProperty, kFootnoteReferenceProperty})
    if (format.hasProperty(property)) style.setProperty(property, format.property(property));
  return style;
}

bool selectInlineScope(QTextCursor& cursor) {
  const auto block = cursor.block();
  const int probe = qMin(cursor.position(), block.position() + block.length() - 2);
  QVector<QTextFragment> fragments;
  int current = -1;
  for (auto it = block.begin(); !it.atEnd(); ++it) {
    const auto fragment = it.fragment();
    if (!fragment.isValid()) continue;
    if (probe >= fragment.position() && probe < fragment.position() + fragment.length())
      current = static_cast<int>(fragments.size());
    fragments.append(fragment);
  }
  if (current < 0) return false;
  const auto style = inlineScopeStyle(fragments[current].charFormat());
  if (style.isEmpty()) return false;
  int first = current, last = current;
  while (first > 0 && inlineScopeStyle(fragments[first - 1].charFormat()) == style) --first;
  while (last + 1 < fragments.size() && inlineScopeStyle(fragments[last + 1].charFormat()) == style)
    ++last;
  cursor.setPosition(fragments[first].position());
  cursor.setPosition(fragments[last].position() + fragments[last].length(),
                     QTextCursor::KeepAnchor);
  return true;
}
}  // namespace

void selectRange(QTextCursor& cursor, SelectionRange range, bool rendered) {
  if (cursor.isNull()) return;
  cursor.clearSelection();
  auto* table = rendered ? cursor.currentTable() : nullptr;
  if (table && range != SelectionRange::Word) {
    const auto cell = table->cellAt(cursor.position());
    if (!cell.isValid()) return;
    if (range == SelectionRange::LineOrSentence) {
      cursor = table->cellAt(cell.row(), 0).firstCursorPosition();
      cursor.setPosition(table->cellAt(cell.row(), table->columns() - 1).lastPosition(),
                         QTextCursor::KeepAnchor);
    } else if (range == SelectionRange::StyledScope) {
      cursor = cell.firstCursorPosition();
      cursor.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
    } else {
      cursor.setPosition(table->firstPosition() - 1);
      cursor.setPosition(table->lastPosition() + 1, QTextCursor::KeepAnchor);
    }
    return;
  }
  if (rendered && (range == SelectionRange::Paragraph || range == SelectionRange::StyledScope)) {
    const auto code = codeRange(cursor);
    if (code.valid()) {
      cursor.setPosition(code.first.position());
      cursor.setPosition(code.last.position() + code.last.length() - 1, QTextCursor::KeepAnchor);
      return;
    }
  }
  const auto block = cursor.block();
  const int contentStart = rendered ? listContentStart(block) : block.position();
  if (cursor.position() < contentStart) cursor.setPosition(contentStart);
  if (range == SelectionRange::StyledScope && rendered && selectInlineScope(cursor)) return;
  if (range == SelectionRange::Word || range == SelectionRange::StyledScope) {
    cursor.select(QTextCursor::WordUnderCursor);
    if (cursor.selectionStart() < contentStart) {
      const int end = qMax(contentStart, cursor.selectionEnd());
      cursor.setPosition(contentStart);
      cursor.setPosition(end, QTextCursor::KeepAnchor);
    }
    return;
  }
  const QString text = block.text().mid(contentStart - block.position());
  int start = 0, end = static_cast<int>(text.size());
  const int position = qBound(0, cursor.position() - contentStart, end);
  if (range == SelectionRange::LineOrSentence) {
    if (!rendered || block.blockFormat().hasProperty(kCodeBlockProperty)) {
      if (position > 0) start = text.lastIndexOf(QChar::LineSeparator, position - 1) + 1;
      const int next = text.indexOf(QChar::LineSeparator, position);
      if (next >= 0) end = next;
    } else if (!block.blockFormat().hasProperty(kSpecialBlockProperty)) {
      QTextBoundaryFinder finder(QTextBoundaryFinder::Sentence, text);
      finder.setPosition(position);
      start = position < end && finder.isAtBoundary() ? position : finder.toPreviousBoundary();
      start = qMax(0, start);
      finder.setPosition(start);
      const int next = static_cast<int>(finder.toNextBoundary());
      if (next >= 0) end = next;
      while (start < end && text.at(start).isSpace()) ++start;
      while (end > start && text.at(end - 1).isSpace()) --end;
    }
  }
  cursor.setPosition(contentStart + start);
  cursor.setPosition(contentStart + end, QTextCursor::KeepAnchor);
}

namespace {
bool eraseSelection(QTextCursor& cursor) {
  if (!cursor.hasSelection()) return false;
  cursor.beginEditBlock();
  cursor.removeSelectedText();
  cursor.endEditBlock();
  return true;
}

bool atomicBlock(const QTextBlock& block) {
  return block.blockFormat().hasProperty(kSpecialBlockProperty) ||
         block.blockFormat().hasProperty(kRuleProperty);
}

bool eraseBlocks(QTextCursor& cursor, const QTextBlock& first, const QTextBlock& last,
                 bool rendered) {
  const auto* document = cursor.document();
  int start = first.position(), end = last.position() + last.length() - 1;
  bool keepBlock = false;
  // Delete a paragraph separator when available, leaving table frame markers alone.
  if (end < document->characterCount() - 1 &&
      document->characterAt(end) == QChar::ParagraphSeparator) {
    ++end;
  } else if (start > 0 && document->characterAt(start - 1) == QChar::ParagraphSeparator) {
    --start;
  } else {
    keepBlock = true;
  }
  const auto format = first.blockFormat();
  const bool reset =
      rendered && keepBlock &&
      (start < end || first.textList() || format.headingLevel() ||
       format.hasProperty(kCodeBlockProperty) || format.hasProperty(kQuoteProperty) ||
       format.hasProperty(kFootnoteDefinitionProperty) || atomicBlock(first));
  if (start == end && !reset) return false;
  cursor.beginEditBlock();
  cursor.setPosition(start);
  cursor.setPosition(end, QTextCursor::KeepAnchor);
  cursor.removeSelectedText();
  if (reset) setParagraphStyle(cursor, ParagraphStyle::Body);
  if (rendered) cursor.setPosition(qMax(cursor.position(), listContentStart(cursor.block())));
  cursor.endEditBlock();
  return true;
}

QTextBlock listSubtreeEnd(const QTextBlock& first) {
  auto last = first;
  if (const auto* list = first.textList()) {
    const int level = list->format().indent();
    for (auto child = first.next();
         child.isValid() && child.textList() && child.textList()->format().indent() > level;
         child = child.next())
      last = child;
  }
  return last;
}

bool eraseParagraph(QTextCursor& cursor) {
  const auto code = codeRange(cursor);
  if (code.valid()) return eraseBlocks(cursor, code.first, code.last, true);
  const auto first = cursor.block();
  return eraseBlocks(cursor, first, listSubtreeEnd(first), true);
}

bool eraseLine(QTextCursor& cursor, bool rendered) {
  const auto block = cursor.block();
  const auto code = rendered ? codeRange(cursor) : CodeRange();
  selectRange(cursor, SelectionRange::LineOrSentence, false);
  const int start = cursor.selectionStart(), end = cursor.selectionEnd();
  if (cursor.document()->characterAt(end) == QChar::LineSeparator) {
    cursor.setPosition(start);
    cursor.setPosition(end + 1, QTextCursor::KeepAnchor);
    return eraseSelection(cursor);
  }
  if (start > block.position() &&
      cursor.document()->characterAt(start - 1) == QChar::LineSeparator) {
    cursor.setPosition(start - 1);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    return eraseSelection(cursor);
  }
  if (!code.valid()) return eraseBlocks(cursor, block, block, rendered);
  if (code.first == code.last) {
    if (!cursor.hasSelection()) return eraseBlocks(cursor, block, block, true);
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    auto format = cursor.blockFormat();
    format.setProperty(kEmptyCodeFenceProperty, true);
    cursor.setBlockFormat(format);
    cursor.endEditBlock();
    return true;
  }
  QTextCursor boundary;
  QTextBlockFormat spacing;
  if (block == code.first) {
    boundary = QTextCursor(block.next());
    spacing = boundary.blockFormat();
    spacing.setTopMargin(block.blockFormat().topMargin());
    spacing.setProperty(kCodeInfoProperty, code.language());
  } else if (block == code.last) {
    boundary = QTextCursor(block.previous());
    spacing = boundary.blockFormat();
    spacing.setBottomMargin(block.blockFormat().bottomMargin());
  }
  cursor.beginEditBlock();
  const bool changed = eraseBlocks(cursor, block, block, true);
  if (changed && !boundary.isNull()) boundary.setBlockFormat(spacing);
  cursor.endEditBlock();
  return changed;
}

bool horizontalSpace(QChar ch) { return ch.isSpace() && ch != QChar::LineSeparator; }

void includeSentenceSpaces(QTextCursor& cursor, const QTextBlock& block) {
  const auto text = block.text();
  int start = cursor.selectionStart() - block.position();
  int end = cursor.selectionEnd() - block.position();
  while (end < text.size() && horizontalSpace(text.at(end))) ++end;
  if (end == text.size()) {
    const int first = listContentStart(block) - block.position();
    while (start > first && horizontalSpace(text.at(start - 1))) --start;
  }
  cursor.setPosition(block.position() + start);
  cursor.setPosition(block.position() + end, QTextCursor::KeepAnchor);
}

void includeStyledEdgeSpaces(QTextCursor& cursor, const QTextBlock& block) {
  const auto* document = cursor.document();
  int start = cursor.selectionStart(), end = cursor.selectionEnd();
  // A remaining emphasis run must not start or end with a space at the cut.
  if (start > listContentStart(block) && horizontalSpace(document->characterAt(start - 1))) {
    QTextCursor left(block);
    left.setPosition(start - 1);
    if (selectInlineScope(left) && left.selectionEnd() <= end) {
      while (start > left.selectionStart() && horizontalSpace(document->characterAt(start - 1)))
        --start;
    }
  }
  if (end < block.position() + block.length() - 1 && horizontalSpace(document->characterAt(end))) {
    QTextCursor right(block);
    right.setPosition(end);
    if (selectInlineScope(right) && right.selectionStart() >= start) {
      while (end < right.selectionEnd() && horizontalSpace(document->characterAt(end))) ++end;
    }
  }
  cursor.setPosition(start);
  cursor.setPosition(end, QTextCursor::KeepAnchor);
}
}  // namespace

bool deleteRange(QTextCursor& cursor, SelectionRange range, bool rendered) {
  if (cursor.isNull()) return false;
  const auto block = cursor.block();
  auto* table = rendered ? cursor.currentTable() : nullptr;
  const bool cells = table && cursor.hasComplexSelection();
  if (table && range == SelectionRange::LineOrSentence && (!cursor.hasSelection() || cells))
    return editTable(cursor, TableCommand::DeleteRow);
  if (cells) return clearTableCells(cursor);
  if (cursor.hasSelection()) {
    if (rendered && atomicBlock(block) && cursor.selectionStart() == block.position() &&
        cursor.selectionEnd() == block.position() + block.length() - 1)
      return eraseBlocks(cursor, block, block, true);
    return eraseSelection(cursor);
  }
  if (range == SelectionRange::Paragraph) {
    if (table) return editTable(cursor, TableCommand::DeleteTable);
    return rendered ? eraseParagraph(cursor) : eraseLine(cursor, false);
  }
  if (range == SelectionRange::LineOrSentence) {
    if (!rendered || block.blockFormat().hasProperty(kCodeBlockProperty))
      return eraseLine(cursor, rendered);
    if (atomicBlock(block)) return eraseBlocks(cursor, block, block, true);
  }
  if (range == SelectionRange::StyledScope && rendered &&
      block.blockFormat().hasProperty(kCodeBlockProperty))
    return eraseParagraph(cursor);
  selectRange(cursor, range, rendered);
  if (range == SelectionRange::LineOrSentence && !cursor.hasSelection())
    return listSubtreeEnd(block) == block && eraseBlocks(cursor, block, block, true);
  const int start = cursor.selectionStart();
  if (rendered && cursor.hasSelection() && !block.blockFormat().hasProperty(kCodeBlockProperty) &&
      (range == SelectionRange::Word || range == SelectionRange::LineOrSentence))
    includeStyledEdgeSpaces(cursor, block);
  if (range == SelectionRange::LineOrSentence && cursor.selectionStart() == start)
    includeSentenceSpaces(cursor, block);
  return eraseSelection(cursor);
}

bool setParagraphStyle(QTextCursor& cursor, ParagraphStyle style, int heading) {
  if (cursor.currentTable()) return false;
  if (style == ParagraphStyle::BulletList || style == ParagraphStyle::OrderedList ||
      style == ParagraphStyle::TaskList)
    return setListStyle(cursor, style);
  cursor.beginEditBlock();
  const QVector<QTextBlock> blocks = selectedBlocks(cursor);
  const int group = ++nextGroup;
  for (QTextBlock block : blocks) {
    const bool wasCode = block.blockFormat().hasProperty(kCodeBlockProperty);
    stripTaskMarker(block);
    QTextCursor line(block);
    if (block.textList()) block.textList()->remove(block);
    QTextBlockFormat format;
    format.setLineHeight(160, QTextBlockFormat::ProportionalHeight);
    format.setBottomMargin(12);
    if (style == ParagraphStyle::Heading) {
      format.setHeadingLevel(qBound(1, heading, 6));
      format.setTopMargin(heading == 1 ? 8 : 22);
      format.setBottomMargin(heading == 1 ? 18 : 10);
      format.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
    } else if (style == ParagraphStyle::Quote) {
      format.setProperty(kQuoteProperty, group);
      format.setLeftMargin(20);
    } else if (style == ParagraphStyle::Code) {
      format.setProperty(kCodeBlockProperty, group);
      format.setLeftMargin(16);
      format.setRightMargin(16);
      format.setTopMargin(block == blocks.front() ? 10 : 0);
      format.setBottomMargin(block == blocks.back() ? 10 : 0);
      format.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
    } else if (style == ParagraphStyle::Rule) {
      line.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
      line.insertText(QString(QChar(0x2014)), QTextCharFormat());
      format.setProperty(kRuleProperty, true);
    }
    line.setBlockFormat(format);
    styleCharacters(block, style == ParagraphStyle::Heading ? heading : 0,
                    style == ParagraphStyle::Code, wasCode);
    if (block.text().isEmpty() || style == ParagraphStyle::Code || wasCode) {
      QTextCharFormat character;
      if (style == ParagraphStyle::Code) {
        character.setFontFamilies({QStringLiteral("Consolas"), QStringLiteral("monospace")});
        character.setFontPointSize(10.5);
      } else if (style == ParagraphStyle::Heading) {
        static const double sizes[] = {28, 22, 18, 15, 13, 12};
        character.setFontPointSize(sizes[qBound(1, heading, 6) - 1]);
        character.setFontWeight(QFont::Bold);
        character.setProperty(kPresentationBoldProperty, true);
      }
      line.setBlockCharFormat(character);
      if (block.text().isEmpty() || !cursor.hasSelection()) cursor.setCharFormat(character);
    }
  }
  cursor.endEditBlock();
  return true;
}

void toggleInlineStyle(QTextCursor& cursor, InlineStyle style) {
  QTextCharFormat format;
  const QTextCharFormat current = cursor.charFormat();
  switch (style) {
    case InlineStyle::Bold:
      format.setFontWeight(current.fontWeight() >= QFont::Bold &&
                                   !current.boolProperty(kPresentationBoldProperty)
                               ? QFont::Normal
                               : QFont::Bold);
      format.setProperty(kPresentationBoldProperty, false);
      break;
    case InlineStyle::Italic:
      format.setFontItalic(!current.fontItalic());
      break;
    case InlineStyle::Strike:
      format.setFontStrikeOut(!current.fontStrikeOut());
      break;
    case InlineStyle::Code: {
      const bool code = current.fontFamilies().toStringList().contains(QStringLiteral("Consolas"));
      format.setFontFamilies(
          code ? cursor.document()->defaultFont().families()
               : QStringList{QStringLiteral("Consolas"), QStringLiteral("monospace")});
      break;
    }
    case InlineStyle::Clear:
      cursor.setCharFormat(QTextCharFormat());
      return;
  }
  cursor.mergeCharFormat(format);
}

void setLink(QTextCursor& cursor, const QString& destination, const QString& label) {
  cursor.beginEditBlock();
  QTextCharFormat format = cursor.charFormat();
  format.setAnchor(!destination.isEmpty());
  format.setAnchorHref(destination);
  format.setFontUnderline(!destination.isEmpty());
  if (!cursor.hasSelection() && !label.isEmpty())
    cursor.insertText(label, format);
  else if (!cursor.hasSelection() && !destination.isEmpty())
    cursor.insertText(destination, format);
  else
    cursor.mergeCharFormat(format);
  cursor.endEditBlock();
}

QTextTable* insertTable(QTextCursor& cursor, int rows, int columns) {
  cursor.beginEditBlock();
  QTextTableFormat format;
  format.setWidth(QTextLength(QTextLength::PercentageLength, 100));
  format.setHeaderRowCount(1);
  format.setCellPadding(9);
  format.setCellSpacing(0);
  format.setBorder(0.5);
  format.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
  format.setBorderBrush(QColor("#a5abb5"));
  QTextTable* table = cursor.insertTable(qBound(2, rows, 100), qBound(1, columns, 20), format);
  for (int row = 0; row < table->rows(); ++row) {
    for (int col = 0; col < table->columns(); ++col) {
      QTextTableCell cell = table->cellAt(row, col);
      QTextTableCellFormat cellFormat;
      cellFormat.setBorder(0.5);
      cellFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
      cellFormat.setBorderBrush(QColor("#a5abb5"));
      cell.setFormat(cellFormat);
      if (row == 0) {
        QTextCursor heading = cell.firstCursorPosition();
        QTextCharFormat character;
        character.setFontWeight(QFont::Bold);
        character.setProperty(kPresentationBoldProperty, true);
        heading.setBlockCharFormat(character);
      }
    }
  }
  cursor = table->cellAt(0, 0).firstCursorPosition();
  cursor.endEditBlock();
  return table;
}

bool editTable(QTextCursor& cursor, TableCommand command) {
  QTextTable* table = cursor.currentTable();
  if (!table) return false;
  const auto cells = tableCells(cursor);
  if (!cells.valid()) return false;
  const int row = cells.row, column = cells.column;
  if ((command == TableCommand::RowAbove || command == TableCommand::RowBelow) &&
      table->rows() >= kMaxTableRows)
    return false;
  if ((command == TableCommand::ColumnBefore || command == TableCommand::ColumnAfter) &&
      table->columns() >= kMaxTableColumns)
    return false;
  if (command == TableCommand::DeleteRow && (row == 0 || table->rows() - cells.rows < 2))
    return false;
  cursor.beginEditBlock();
  switch (command) {
    case TableCommand::RowAbove:
      table->insertRows(qMax(1, row), 1);
      break;
    case TableCommand::RowBelow:
      table->insertRows(row + cells.rows, 1);
      break;
    case TableCommand::DeleteRow:
      table->removeRows(row, cells.rows);
      break;
    case TableCommand::ColumnBefore:
      table->insertColumns(column, 1);
      break;
    case TableCommand::ColumnAfter:
      table->insertColumns(column + cells.columns, 1);
      break;
    case TableCommand::DeleteColumn:
      if (table->columns() - cells.columns < 1) {
        cursor.endEditBlock();
        return false;
      }
      table->removeColumns(column, cells.columns);
      break;
    case TableCommand::DeleteTable: {
      const int start = table->firstPosition() - 1;
      const int end = table->lastPosition() + 1;
      cursor.setPosition(start);
      cursor.setPosition(end, QTextCursor::KeepAnchor);
      cursor.removeSelectedText();
      cursor.endEditBlock();
      return true;
    }
    default: {
      const int alignment = command == TableCommand::AlignCenter  ? 2
                            : command == TableCommand::AlignRight ? 3
                                                                  : 1;
      for (int selectedColumn = column; selectedColumn < column + cells.columns; ++selectedColumn)
        for (int index = 0; index < table->rows(); ++index) {
          QTextCursor text = table->cellAt(index, selectedColumn).firstCursorPosition();
          QTextBlockFormat format = text.blockFormat();
          format.setAlignment(alignment == 2   ? Qt::AlignHCenter
                              : alignment == 3 ? Qt::AlignRight
                                               : Qt::AlignLeft);
          format.setProperty(kTableAlignmentProperty, alignment);
          text.setBlockFormat(format);
        }
      break;
    }
  }
  styleTable(table);
  if (table->rows() > 0 && table->columns() > 0)
    cursor = table->cellAt(qMin(row, table->rows() - 1), qMin(column, table->columns() - 1))
                 .firstCursorPosition();
  cursor.endEditBlock();
  return true;
}

bool renderInlineMarkdown(QTextCursor& cursor) {
  if (cursor.hasSelection() || cursor.block().text().size() > 4096 ||
      cursor.blockFormat().hasProperty(kCodeBlockProperty))
    return false;
  int start = cursor.block().position();
  for (auto it = cursor.block().begin(); !it.atEnd(); ++it) {
    if (it.fragment().position() >= cursor.position()) break;
    const QTextCharFormat format = it.fragment().charFormat();
    if ((format.fontWeight() >= QFont::Bold && !format.boolProperty(kPresentationBoldProperty)) ||
        format.fontItalic() || format.fontStrikeOut() || format.isAnchor() ||
        format.boolProperty(kLiteralTextProperty) || format.isImageFormat() ||
        format.fontFamilies().toStringList().contains(QStringLiteral("Consolas")))
      start = qMin(cursor.position(), it.fragment().position() + it.fragment().length());
  }
  const QString text =
      cursor.block().text().mid(start - cursor.block().position(), cursor.position() - start);
  for (QChar marker : {QChar('*'), QChar('_'), QChar('~'), QChar('`')}) {
    int count = 0;
    for (int index = 0; index < text.size(); ++index)
      if (text.at(index) == marker && (index == 0 || text.at(index - 1) != QLatin1Char('\\')))
        ++count;
    if (count % 2 != 0) return false;
  }
  int leading = 0;
  while (leading < text.size() &&
         (text.at(leading) == QLatin1Char(' ') || text.at(leading) == QLatin1Char('\t')))
    ++leading;
  const auto parsed = core::parseMarkdown(text.toStdString());
  if (!parsed.ok || parsed.blocks.size() != 1 ||
      parsed.blocks.front().type != core::BlockType::Paragraph)
    return false;
  bool styled = false;
  for (const auto& run : parsed.blocks.front().runs)
    styled |= run.bold || run.italic || run.strike || run.code || run.link || run.image ||
              run.math || run.footnote;
  if (!styled) return false;
  QTextDocument temporary;
  temporary.setBaseUrl(cursor.document()->baseUrl());
  temporary.setProperty("typistDark", cursor.document()->property("typistDark"));
  buildDocument(parsed.blocks, &temporary);
  QTextCursor fragment(temporary.begin());
  fragment.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  const QTextBlockFormat blockFormat = cursor.blockFormat();
  cursor.beginEditBlock();
  cursor.setPosition(start, QTextCursor::KeepAnchor);
  cursor.insertText(text.left(leading), QTextCharFormat());
  cursor.insertFragment(QTextDocumentFragment(fragment));
  cursor.setBlockFormat(blockFormat);
  cursor.setCharFormat(QTextCharFormat());
  cursor.endEditBlock();
  return true;
}

QString selectedMarkdown(const QTextCursor& cursor) {
  if (!cursor.hasSelection()) return QString();
  QTextDocument document;
  QTextCursor target(&document);
  target.insertFragment(QTextDocumentFragment(cursor));
  return serializeDocument(&document).trimmed();
}
}  // namespace typist::editor
