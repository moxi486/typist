#pragma once

#include <QTextCursor>
#include <QVector>

class QTextTable;
namespace typist::editor {
constexpr int kMaxTableRows = 500;
constexpr int kMaxTableColumns = 40;
struct TableCells {
  int row = 0, rows = 0, column = 0, columns = 0;
  bool valid() const { return rows > 0 && columns > 0; }
};
TableCells tableCells(const QTextCursor& cursor);
void styleTable(QTextTable* table);
bool pasteTableCells(QTextCursor& cursor, const QString& text, QString* error = nullptr);
QString tableMarkdown(const QTextCursor& cursor);
QString tableTsv(const QTextCursor& cursor, bool selectionOnly = false);
bool clearTableCells(QTextCursor& cursor);
bool exitTable(QTextCursor& cursor);
bool moveTableCell(QTextCursor& cursor, bool backwards);
}  // namespace typist::editor
