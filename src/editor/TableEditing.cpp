#include "TableEditing.h"

#include "Editing.h"
#include "editor_document.h"

#include <QCoreApplication>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFragment>
#include <QTextTable>
#include <optional>

namespace typist::editor {
namespace {
QString tableText(const char* source) {
  return QCoreApplication::translate("TableEditing", source);
}
QString plainCell(const QTextTableCell& cell) {
  auto cursor = cell.firstCursorPosition();
  cursor.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
  return cursor.selectedText()
      .replace(QChar::ParagraphSeparator, '\n')
      .replace(QChar::LineSeparator, '\n');
}
QString quoted(QString text) {
  if (text.contains('\t') || text.contains('\n') || text.contains('"')) {
    text.replace('"', QStringLiteral("\"\""));
    return '"' + text + '"';
  }
  return text;
}
bool grid(QString text, QVector<QStringList>* rows, QString* error) {
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace('\r', '\n');
  QStringList row;
  QString value;
  bool quotedCell = false, closed = false, start = true;
  const auto fail = [&] {
    if (error)
      *error = tableText(
          QT_TRANSLATE_NOOP("TableEditing", "The table data contains an unfinished quoted cell"));
    return false;
  };
  for (int index = 0; index < text.size(); ++index) {
    const QChar ch = text.at(index);
    if (quotedCell) {
      if (ch == '"') {
        if (index + 1 < text.size() && text.at(index + 1) == '"') {
          value += '"';
          ++index;
        } else {
          quotedCell = false;
          closed = true;
        }
      } else
        value += ch;
      continue;
    }
    if (start && ch == '"') {
      quotedCell = true;
      start = false;
      continue;
    }
    if (ch == '\t' || ch == '\n') {
      row.append(value);
      value.clear();
      closed = false;
      start = true;
      if (row.size() > kMaxTableColumns) break;
      if (ch == '\n') {
        rows->append(row);
        row.clear();
      }
      if (rows->size() > kMaxTableRows) break;
      continue;
    }
    if (closed) return fail();
    value += ch;
    start = false;
  }
  if (quotedCell) return fail();
  if (!row.isEmpty() || !value.isEmpty() || !text.endsWith('\n')) {
    row.append(value);
    rows->append(row);
  }
  int width = 0;
  for (const auto& line : *rows) width = qMax(width, int(line.size()));
  if (rows->size() > kMaxTableRows || width > kMaxTableColumns || text.size() > 2 * 1024 * 1024) {
    if (error)
      *error = tableText(
          QT_TRANSLATE_NOOP("TableEditing", "A table can contain up to 500 rows and 40 columns"));
    return false;
  }
  for (auto& line : *rows)
    while (line.size() < width) line.append(QString());
  return !rows->isEmpty() && width > 0;
}
void replaceCell(const QTextTableCell& cell, const QString& text) {
  auto cursor = cell.firstCursorPosition();
  const auto blockFormat = cursor.blockFormat();
  cursor.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
  cursor.removeSelectedText();
  cursor.setBlockFormat(blockFormat);
  QTextCharFormat format;
  format.setProperty(kLiteralTextProperty, true);
  if (cell.row() == 0) {
    format.setFontWeight(QFont::Bold);
    format.setProperty(kPresentationBoldProperty, true);
  }
  QString value = text;
  value.replace('\n', QChar::LineSeparator);
  cursor.insertText(value, format);
}

struct MoveRange {
  bool rows;
  int direction;
  int first;
  int count;
};

std::optional<MoveRange> moveRange(const QTextTable* table, const TableCells& cells,
                                   TableCommand command) {
  if (!table || !cells.valid()) return std::nullopt;
  const bool rows = command == TableCommand::MoveRowsUp || command == TableCommand::MoveRowsDown;
  if (!rows && command != TableCommand::MoveColumnsLeft &&
      command != TableCommand::MoveColumnsRight)
    return std::nullopt;
  const int direction =
      command == TableCommand::MoveRowsUp || command == TableCommand::MoveColumnsLeft ? -1 : 1;
  const int start = rows ? cells.row : cells.column;
  const int count = rows ? cells.rows : cells.columns;
  const int neighbor = direction < 0 ? start - 1 : start + count;
  const int firstMovable = rows ? 1 : 0;
  if (start < firstMovable || neighbor < firstMovable ||
      neighbor >= (rows ? table->rows() : table->columns()))
    return std::nullopt;
  return MoveRange{rows, direction, qMin(neighbor, start), count + 1};
}

struct CellSnapshot {
  QTextDocumentFragment text;
  QTextBlockFormat block;
  QTextCharFormat blockText;
  QTextCharFormat cell;
};
}  // namespace
TableCells tableCells(const QTextCursor& cursor) {
  auto* table = cursor.currentTable();
  if (!table) return {};
  if (cursor.hasComplexSelection()) {
    TableCells cells;
    cursor.selectedTableCells(&cells.row, &cells.rows, &cells.column, &cells.columns);
    return cells;
  }
  if (cursor.hasSelection() &&
      (QTextCursor(cursor.document()->findBlock(cursor.selectionStart())).currentTable() != table ||
       QTextCursor(cursor.document()->findBlock(cursor.selectionEnd() - 1)).currentTable() !=
           table))
    return {};
  const auto cell = table->cellAt(cursor.position());
  return cell.isValid() ? TableCells{cell.row(), 1, cell.column(), 1} : TableCells{};
}
void styleTable(QTextTable* table) {
  if (!table) return;
  auto format = table->format();
  format.setHeaderRowCount(1);
  format.setTopMargin(8);
  format.setBottomMargin(16);
  table->setFormat(format);
  for (int column = 0; column < table->columns(); ++column) {
    const auto header = table->cellAt(0, column);
    const int alignment =
        header.firstCursorPosition().blockFormat().intProperty(kTableAlignmentProperty);
    for (int row = 0; row < table->rows(); ++row) {
      auto cell = table->cellAt(row, column);
      if (cell.row() != row || cell.column() != column) continue;
      auto cellFormat = cell.format().toTableCellFormat();
      cellFormat.setBorder(0.5);
      cellFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
      cellFormat.setBorderBrush(QColor("#a5abb5"));
      cell.setFormat(cellFormat);
      for (auto block = cell.firstCursorPosition().block();
           block.isValid() && block.position() <= cell.lastPosition(); block = block.next()) {
        auto blockFormat = block.blockFormat();
        blockFormat.setBottomMargin(0);
        blockFormat.setLineHeight(160, QTextBlockFormat::ProportionalHeight);
        blockFormat.setProperty(kTableAlignmentProperty, alignment);
        blockFormat.setAlignment(alignment == 2   ? Qt::AlignHCenter
                                 : alignment == 3 ? Qt::AlignRight
                                                  : Qt::AlignLeft);
        QTextCursor line(block);
        line.setBlockFormat(blockFormat);
        auto character = block.charFormat();
        character.setFontWeight(row == 0 ? QFont::Bold : QFont::Normal);
        character.setProperty(kPresentationBoldProperty, row == 0);
        character.clearProperty(kLiteralTextProperty);
        line.setBlockCharFormat(character);
        struct Span {
          int position, length;
          QTextCharFormat format;
        };
        QList<Span> spans;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
          const auto fragment = it.fragment();
          auto run = fragment.charFormat();
          const bool semanticBold =
              run.fontWeight() >= QFont::Bold && !run.boolProperty(kPresentationBoldProperty);
          if (row == 0) {
            run.setFontWeight(QFont::Bold);
            run.setProperty(kPresentationBoldProperty, !semanticBold);
          } else if (run.boolProperty(kPresentationBoldProperty)) {
            run.setFontWeight(QFont::Normal);
            run.clearProperty(kPresentationBoldProperty);
          }
          if (run != fragment.charFormat())
            spans.append({fragment.position(), fragment.length(), run});
        }
        for (const auto& span : spans) {
          line.setPosition(span.position);
          line.setPosition(span.position + span.length, QTextCursor::KeepAnchor);
          line.setCharFormat(span.format);
        }
      }
    }
  }
}
bool pasteTableCells(QTextCursor& cursor, const QString& text, QString* error) {
  const bool selected = cursor.hasComplexSelection() && cursor.currentTable();
  if (!text.contains('\t') && !selected) return false;
  if (text.size() > 2 * 1024 * 1024) {
    if (error) *error = tableText(QT_TRANSLATE_NOOP("TableEditing", "The table data is too large"));
    return false;
  }
  QVector<QStringList> rows;
  if (selected && !text.contains('\t'))
    rows.append({text});
  else if (!grid(text, &rows, error))
    return false;
  auto* table = cursor.currentTable();
  const auto selection = tableCells(cursor);
  const int firstRow = table ? selection.row : 0, firstColumn = table ? selection.column : 0;
  if (table && !selection.valid()) return false;
  int height = int(rows.size()), width = int(rows.first().size());
  const bool repeat = selected && height == 1 && width == 1;
  if (repeat) {
    height = selection.rows;
    width = selection.columns;
  }
  if (firstRow + height > kMaxTableRows || firstColumn + width > kMaxTableColumns) {
    if (error)
      *error = tableText(
          QT_TRANSLATE_NOOP("TableEditing", "A table can contain up to 500 rows and 40 columns"));
    return false;
  }
  if (!table &&
      (cursor.block().textList() || cursor.blockFormat().hasProperty(kCodeBlockProperty) ||
       cursor.blockFormat().hasProperty(kQuoteProperty) ||
       cursor.blockFormat().hasProperty(kSpecialBlockProperty)))
    return false;
  cursor.beginEditBlock();
  if (!table) {
    table = insertTable(cursor, qMax(2, height), width);
  }
  if (firstRow + height > table->rows()) table->appendRows(firstRow + height - table->rows());
  if (firstColumn + width > table->columns())
    table->appendColumns(firstColumn + width - table->columns());
  for (int row = 0; row < height; ++row)
    for (int column = 0; column < width; ++column)
      replaceCell(table->cellAt(firstRow + row, firstColumn + column),
                  rows[repeat ? 0 : row][repeat ? 0 : column]);
  styleTable(table);
  cursor = table->cellAt(firstRow + height - 1, firstColumn + width - 1).lastCursorPosition();
  cursor.endEditBlock();
  return true;
}
QString tableMarkdown(const QTextCursor& cursor) {
  auto* table = cursor.currentTable();
  if (!table) return {};
  QTextCursor entire(cursor.document());
  entire.setPosition(table->firstPosition() - 1);
  entire.setPosition(table->lastPosition() + 1, QTextCursor::KeepAnchor);
  return selectedMarkdown(entire);
}
QString tableTsv(const QTextCursor& cursor, bool selectionOnly) {
  auto* table = cursor.currentTable();
  if (!table) return {};
  const auto cells =
      selectionOnly ? tableCells(cursor) : TableCells{0, table->rows(), 0, table->columns()};
  if (!cells.valid()) return {};
  QStringList rows;
  for (int row = cells.row; row < cells.row + cells.rows; ++row) {
    QStringList columns;
    for (int column = cells.column; column < cells.column + cells.columns; ++column)
      columns.append(quoted(plainCell(table->cellAt(row, column))));
    rows.append(columns.join('\t'));
  }
  return rows.join('\n');
}
bool clearTableCells(QTextCursor& cursor) {
  const auto cells = tableCells(cursor);
  if (!cursor.hasComplexSelection() || !cells.valid()) return false;
  cursor.beginEditBlock();
  for (int row = cells.row; row < cells.row + cells.rows; ++row)
    for (int column = cells.column; column < cells.column + cells.columns; ++column)
      replaceCell(cursor.currentTable()->cellAt(row, column), {});
  cursor.endEditBlock();
  return true;
}
bool exitTable(QTextCursor& cursor) {
  auto* table = cursor.currentTable();
  if (!table) return false;
  const int end = table->lastPosition() + 1;
  if (end < cursor.document()->characterCount()) {
    cursor.setPosition(end);
    if (!cursor.currentTable() && !cursor.block().textList() &&
        cursor.blockFormat().headingLevel() == 0 &&
        !cursor.blockFormat().hasProperty(kSpecialBlockProperty) &&
        !cursor.blockFormat().hasProperty(kQuoteProperty) &&
        !cursor.blockFormat().hasProperty(kCodeBlockProperty) &&
        !cursor.blockFormat().hasProperty(kRuleProperty)) {
      cursor.setCharFormat(QTextCharFormat());
      return true;
    }
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.beginEditBlock();
    const auto followingFormat = cursor.blockFormat();
    const auto followingCharacters = cursor.block().charFormat();
    cursor.insertBlock(followingFormat, followingCharacters);
    cursor.movePosition(QTextCursor::PreviousBlock);
    setParagraphStyle(cursor, ParagraphStyle::Body);
    cursor.endEditBlock();
    return true;
  }
  return false;
}
bool moveTableCell(QTextCursor& cursor, bool backwards) {
  auto* table = cursor.currentTable();
  if (!table) return false;
  const auto cell = table->cellAt(cursor.position());
  int next = cell.row() * table->columns() + cell.column() + (backwards ? -1 : 1);
  if (next < 0) {
    cursor.setPosition(qMax(0, table->firstPosition() - 1));
    return true;
  }
  if (next >= table->rows() * table->columns()) {
    if (table->rows() >= kMaxTableRows) return exitTable(cursor);
    cursor.beginEditBlock();
    table->appendRows(1);
    styleTable(table);
    cursor = table->cellAt(table->rows() - 1, 0).firstCursorPosition();
    cursor.endEditBlock();
  } else
    cursor = table->cellAt(next / table->columns(), next % table->columns()).firstCursorPosition();
  return true;
}

bool canMoveTable(const QTextCursor& cursor, TableCommand command) {
  return moveRange(cursor.currentTable(), tableCells(cursor), command).has_value();
}

bool moveTable(QTextCursor& cursor, TableCommand command) {
  auto* table = cursor.currentTable();
  const auto selection = tableCells(cursor);
  const auto range = moveRange(table, selection, command);
  if (!range) return false;
  const int lanes = range->rows ? table->columns() : table->rows();
  const auto cellAt = [&](int track, int lane) {
    return range->rows ? table->cellAt(range->first + track, lane)
                       : table->cellAt(lane, range->first + track);
  };

  // Move the formatted content within the existing frame. In particular, a column
  // carries its header and the Markdown alignment property stored on each block.
  QVector<CellSnapshot> snapshots;
  snapshots.reserve(range->count * lanes);
  for (int track = 0; track < range->count; ++track) {
    for (int lane = 0; lane < lanes; ++lane) {
      const auto cell = cellAt(track, lane);
      if (cell.rowSpan() != 1 || cell.columnSpan() != 1) return false;
      auto text = cell.firstCursorPosition();
      text.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
      const auto first = cell.firstCursorPosition().block();
      snapshots.append(
          {QTextDocumentFragment(text), first.blockFormat(), first.charFormat(), cell.format()});
    }
  }

  const bool rectangular = cursor.hasComplexSelection();
  const bool reversed = cursor.position() < cursor.anchor();
  const int originalStart = table->cellAt(selection.row, selection.column).firstPosition();
  const int anchorOffset = cursor.anchor() - originalStart;
  const int caretOffset = cursor.position() - originalStart;
  const int shift = range->direction < 0 ? 1 : range->count - 1;
  cursor.beginEditBlock();
  for (int track = 0; track < range->count; ++track) {
    const int from = (track + shift) % range->count;
    for (int lane = 0; lane < lanes; ++lane) {
      auto cell = cellAt(track, lane);
      const auto& snapshot = snapshots.at(from * lanes + lane);
      auto text = cell.firstCursorPosition();
      text.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
      text.removeSelectedText();
      text.setBlockFormat(snapshot.block);
      text.setBlockCharFormat(snapshot.blockText);
      text.insertFragment(snapshot.text);
      cell.setFormat(snapshot.cell);
    }
  }
  if (!range->rows) {
    auto format = table->format();
    auto widths = format.columnWidthConstraints();
    if (widths.size() == table->columns()) {
      const auto previous = widths;
      for (int i = 0; i < range->count; ++i)
        widths[range->first + i] = previous.at(range->first + (i + shift) % range->count);
      format.setColumnWidthConstraints(widths);
      table->setFormat(format);
    }
  }
  cursor.endEditBlock();

  const int row = selection.row + (range->rows ? range->direction : 0);
  const int column = selection.column + (range->rows ? 0 : range->direction);
  const auto first = table->cellAt(row, column);
  if (rectangular) {
    const auto last = table->cellAt(row + selection.rows - 1, column + selection.columns - 1);
    cursor.setPosition(reversed ? last.lastPosition() : first.firstPosition());
    cursor.setPosition(reversed ? first.firstPosition() : last.lastPosition(),
                       QTextCursor::KeepAnchor);
  } else {
    const int length = first.lastPosition() - first.firstPosition();
    cursor.setPosition(first.firstPosition() + qBound(0, anchorOffset, length));
    cursor.setPosition(first.firstPosition() + qBound(0, caretOffset, length),
                       QTextCursor::KeepAnchor);
  }
  return true;
}
}  // namespace typist::editor
