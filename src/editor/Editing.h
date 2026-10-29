#pragma once

#include <QTextCursor>
class QTextTable;

namespace typist::editor {

enum class InlineStyle { Bold, Italic, Strike, Code, Clear };
enum class ParagraphStyle { Body, Heading, BulletList, OrderedList, TaskList, Quote, Code, Rule };
enum class TableCommand {
  RowAbove,
  RowBelow,
  DeleteRow,
  ColumnBefore,
  ColumnAfter,
  DeleteColumn,
  AlignLeft,
  AlignCenter,
  AlignRight,
  DeleteTable
};

bool setParagraphStyle(QTextCursor& cursor, ParagraphStyle style, int heading = 0);
void toggleInlineStyle(QTextCursor& cursor, InlineStyle style);
void setLink(QTextCursor& cursor, const QString& destination, const QString& label = QString());
bool indentList(QTextCursor& cursor, int delta);
QTextTable* insertTable(QTextCursor& cursor, int rows, int columns);
bool editTable(QTextCursor& cursor, TableCommand command);
bool renderInlineMarkdown(QTextCursor& cursor);
QString selectedMarkdown(const QTextCursor& cursor);

}  // namespace typist::editor
