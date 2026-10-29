#include "Editing.h"

#include "TableEditing.h"
#include "editor_document.h"

#include <QFontDatabase>
#include <QTextBlock>
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
}  // namespace

bool setParagraphStyle(QTextCursor& cursor, ParagraphStyle style, int heading) {
  if (cursor.currentTable()) return false;
  cursor.beginEditBlock();
  const QVector<QTextBlock> blocks = selectedBlocks(cursor);
  QTextList* list = nullptr;
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
    const bool isList = style == ParagraphStyle::BulletList ||
                        style == ParagraphStyle::OrderedList || style == ParagraphStyle::TaskList;
    if (isList) {
      format.setBottomMargin(5);
      line.setBlockFormat(format);
      if (!list) {
        QTextListFormat listFormat;
        listFormat.setIndent(1);
        listFormat.setStyle(style == ParagraphStyle::OrderedList ? QTextListFormat::ListDecimal
                            : style == ParagraphStyle::TaskList
                                ? QTextListFormat::ListStyleUndefined
                                : QTextListFormat::ListDisc);
        // Continue an adjacent list instead of restarting its numbering.
        QTextList* previous = block.previous().textList();
        if (previous && previous->format().style() == listFormat.style() &&
            previous->format().indent() == 1)
          list = previous;
        else
          list = line.createList(listFormat);
      }
      list->add(block);
      if (style == ParagraphStyle::TaskList) {
        format = line.blockFormat();
        format.setProperty(kTaskProperty, false);
        line.setBlockFormat(format);
        QTextCharFormat marker;
        marker.setProperty(kTaskMarkProperty, false);
        line.movePosition(QTextCursor::StartOfBlock);
        line.insertText(QString(QChar(0x2610)) + QLatin1Char(' '), marker);
      }
    }
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

bool indentList(QTextCursor& cursor, int delta) {
  const QVector<QTextBlock> blocks = selectedBlocks(cursor);
  if (blocks.isEmpty() || !blocks.front().textList()) return false;
  const int oldIndent = blocks.front().textList()->format().indent();
  if (delta > 0 && (!blocks.front().previous().textList() ||
                    blocks.front().previous().textList()->format().indent() < oldIndent))
    return false;
  if (oldIndent + delta < 1) return setParagraphStyle(cursor, ParagraphStyle::Body);
  cursor.beginEditBlock();
  QTextList* nested = nullptr;
  for (const QTextBlock& block : blocks) {
    if (!block.textList()) continue;
    QTextListFormat format = block.textList()->format();
    format.setIndent(qMax(1, format.indent() + delta));
    QTextCursor line(block);
    if (!nested)
      nested = line.createList(format);
    else
      nested->add(block);
  }
  cursor.endEditBlock();
  return true;
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
  cursor.beginEditBlock();
  switch (command) {
    case TableCommand::RowAbove:
      table->insertRows(qMax(1, row), 1);
      break;
    case TableCommand::RowBelow:
      table->insertRows(row + cells.rows, 1);
      break;
    case TableCommand::DeleteRow:
      if (row == 0 || table->rows() - cells.rows < 2) {
        cursor.endEditBlock();
        return false;
      }
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
