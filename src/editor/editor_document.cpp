#include "editor_document.h"

#include <QFont>
#include <QString>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextListFormat>
#include <QTextTable>
#include <QTextTableFormat>

#include <string>

namespace typist::editor {
namespace {

void insertBlock(const typist::core::Block& block, QTextCursor& cursor, int depth) {
  QTextBlockFormat blockFormat;
  blockFormat.setLeftMargin(depth * 16);

  switch (block.type) {
    case typist::core::BlockType::Heading: {
      QTextBlockFormat headingFormat;
      headingFormat.setHeadingLevel(block.level);
      cursor.insertBlock(headingFormat);
      cursor.insertText(QString::fromStdString(block.text));
      break;
    }
    case typist::core::BlockType::CodeBlock: {
      QTextCharFormat codeFormat;
      QFont mono(QStringLiteral("monospace"));
      mono.setStyleHint(QFont::TypeWriter);
      codeFormat.setFont(mono);
      cursor.insertBlock(blockFormat);
      cursor.insertText(QString::fromStdString(block.text), codeFormat);
      break;
    }
    case typist::core::BlockType::ThematicBreak:
      cursor.insertBlock(blockFormat);
      cursor.insertText(QStringLiteral("\u2014"));
      break;
    case typist::core::BlockType::ListItem: {
      QTextListFormat listFormat;
      listFormat.setStyle(block.ordered ? QTextListFormat::ListDecimal
                                          : QTextListFormat::ListDisc);
      cursor.insertList(listFormat);
      const std::string prefix = block.task ? (block.checked ? "[x] " : "[ ] ") : "";
      cursor.insertText(QString::fromStdString(prefix + block.text));
      break;
    }
    case typist::core::BlockType::Unsupported:
      break;
    default:
      cursor.insertBlock(blockFormat);
      cursor.insertText(QString::fromStdString(block.text));
      break;
  }

  for (const typist::core::Block& child : block.children) insertBlock(child, cursor, depth + 1);
}

}  // namespace

void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document) {
  document->clear();
  QTextCursor cursor(document);
  for (const typist::core::Block& block : blocks) insertBlock(block, cursor, 0);
}

bool insertTableProbe(QTextDocument* document, int rows, int cols) {
  QTextCursor cursor(document);
  cursor.movePosition(QTextCursor::End);
  cursor.insertBlock();

  QTextTableFormat tableFormat;
  tableFormat.columns(cols);
  tableFormat.rows(rows);
  QTextTable* table = cursor.insertTable(rows, cols, tableFormat);
  if (table == nullptr) return false;

  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      QTextCursor cellCursor(table->cellTextCursor(row, col));
      cellCursor.insertText(QStringLiteral("r%1c%2").arg(row).arg(col));
    }
  }

  const QString text = document->toPlainText();
  return text.contains(QStringLiteral("r0c0")) && text.contains(QStringLiteral("r1c1"));
}

}  // namespace typist::editor
