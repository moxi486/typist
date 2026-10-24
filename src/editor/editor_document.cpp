#include "editor_document.h"

#include <QFont>
#include <QString>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextList>
#include <QTextListFormat>
#include <QTextTable>
#include <QTextTableFormat>
#include <QVariant>

#include <algorithm>
#include <string>
#include <vector>

namespace typist::editor {
namespace {

struct BuildContext {
  int groupCounter = 0;
};

void insertTaggedBlock(QTextCursor& cursor, const QString& text, uint property,
                       const QVariant& value, const QTextCharFormat& charFormat) {
  QTextBlockFormat blockFormat;
  if (property != 0) blockFormat.setUserProperty(property, value);
  cursor.insertBlock(blockFormat);
  cursor.insertText(text, charFormat);
}

void insertCodeBlock(const std::string& code, QTextCursor& cursor, BuildContext& ctx) {
  QTextCharFormat codeFormat;
  QFont mono(QStringLiteral("monospace"));
  mono.setStyleHint(QFont::TypeWriter);
  codeFormat.setFont(mono);

  const QVariant groupId = QVariant(++ctx.groupCounter);
  std::string line;
  for (char c : code) {
    if (c == '\n') {
      if (!line.empty()) {
        insertTaggedBlock(cursor, QString::fromStdString(line), kCodeBlockProperty, groupId, codeFormat);
        line.clear();
      }
    } else {
      line += c;
    }
  }
  if (!line.empty()) {
    insertTaggedBlock(cursor, QString::fromStdString(line), kCodeBlockProperty, groupId, codeFormat);
  }
}

void insertTable(const typist::core::Block& table, QTextCursor& cursor) {
  int cols = 0;
  for (const typist::core::Block& row : table.children) {
    cols = std::max(cols, static_cast<int>(row.children.size()));
  }
  const int rows = static_cast<int>(table.children.size());
  if (rows == 0 || cols == 0) return;

  QTextTableFormat tableFormat;
  tableFormat.columns(cols);
  tableFormat.rows(rows);
  cursor.insertBlock();
  QTextTable* qtTable = cursor.insertTable(rows, cols, tableFormat);
  if (qtTable == nullptr) return;

  for (int row = 0; row < rows; ++row) {
    const int cellCount = static_cast<int>(table.children[row].children.size());
    for (int col = 0; col < cellCount; ++col) {
      QTextCursor cellCursor = qtTable->cellTextCursor(row, col);
      cellCursor.insertText(QStringLiteral("r%1c%2").arg(row).arg(col));
    }
  }
}

void insertBlock(const typist::core::Block& block, QTextCursor& cursor, int depth,
                 BuildContext& ctx) {
  const QTextCharFormat plain;

  switch (block.type) {
    case typist::core::BlockType::Heading: {
      QTextBlockFormat headingFormat;
      headingFormat.setHeadingLevel(block.level);
      cursor.insertBlock(headingFormat);
      cursor.insertText(QString::fromStdString(block.text));
      break;
    }
    case typist::core::BlockType::CodeBlock:
      insertCodeBlock(block.text, cursor, ctx);
      break;
    case typist::core::BlockType::ThematicBreak:
      insertTaggedBlock(cursor, QStringLiteral("\u2014"), kRuleProperty, QVariant(true), plain);
      break;
    case typist::core::BlockType::BlockQuote:
      ++ctx.groupCounter;
      for (const typist::core::Block& child : block.children) {
        insertTaggedBlock(cursor, QString::fromStdString(child.text), kQuoteProperty,
                          QVariant(ctx.groupCounter), plain);
      }
      break;
    case typist::core::BlockType::Table:
      insertTable(block, cursor);
      break;
    case typist::core::BlockType::ListItem: {
      QTextListFormat listFormat;
      listFormat.setStyle(block.ordered ? QTextListFormat::ListDecimal
                                          : QTextListFormat::ListDisc);
      listFormat.setIndent(static_cast<uint>(depth + 1));
      cursor.insertList(listFormat);
      if (block.task) {
        QTextBlockFormat itemFormat = cursor.blockFormat();
        itemFormat.setUserProperty(kTaskProperty, QVariant(block.checked));
        cursor.setBlockFormat(itemFormat);
      }
      cursor.insertText(QString::fromStdString(block.text));
      for (const typist::core::Block& child : block.children) {
        insertBlock(child, cursor, depth + 1, ctx);
      }
      break;
    }
    case typist::core::BlockType::TableRow:
    case typist::core::BlockType::TableCell:
    case typist::core::BlockType::Unsupported:
      break;
    default:
      insertTaggedBlock(cursor, QString::fromStdString(block.text), 0, QVariant(), plain);
      break;
  }
}

}  // namespace

void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document) {
  document->clear();
  QTextCursor cursor(document);
  BuildContext ctx;
  for (const typist::core::Block& block : blocks) insertBlock(block, cursor, 0, ctx);
}

QString serializeDocument(const QTextDocument* document) {
  QString out;
  QTextBlock block = document->firstBlock();

  while (block.isValid()) {
    if (document->findTable(block.textCursor()) != nullptr) {
      // Table cells stay out of the Markdown stream; table fidelity lives in the
      // block tree. Emitting from QTextTable needs its dimension API verified.
      block = block.next();
      continue;
    }

    const QTextBlockFormat format = block.blockFormat();
    const QString text = block.text();

    if (format.userProperty(kRuleProperty).toBool()) {
      out += QStringLiteral("---\n\n");
      block = block.next();
      continue;
    }

    if (format.userProperty(kCodeBlockProperty).toInt() > 0) {
      const int groupId = format.userProperty(kCodeBlockProperty).toInt();
      QTextBlock current = block;
      out += QStringLiteral("```\n");
      while (current.isValid() &&
             current.blockFormat().userProperty(kCodeBlockProperty).toInt() == groupId) {
        out += current.text() + QStringLiteral("\n");
        current = current.next();
      }
      out += QStringLiteral("```\n\n");
      block = current;
      continue;
    }

    if (format.userProperty(kQuoteProperty).toInt() > 0) {
      const int groupId = format.userProperty(kQuoteProperty).toInt();
      QTextBlock current = block;
      while (current.isValid() &&
             current.blockFormat().userProperty(kQuoteProperty).toInt() == groupId) {
        out += QLatin1Char('>');
        out += QLatin1Char(' ');
        out += current.text() + QStringLiteral("\n");
        current = current.next();
      }
      out += QStringLiteral("\n");
      block = current;
      continue;
    }

    QTextList* list = format.list();
    if (list != nullptr) {
      const int depth = std::max(0, static_cast<int>(list->format().indent()) - 1);
      const bool ordered = list->format().style() == QTextListFormat::ListDecimal;
      QTextBlock current = block;
      int index = 1;
      while (current.isValid()) {
        const QTextBlockFormat itemFormat = current.blockFormat();
        QTextList* itemList = itemFormat.list();
        if (itemList == nullptr ||
            static_cast<int>(itemList->format().indent()) - 1 != depth ||
            (itemList->format().style() == QTextListFormat::ListDecimal) != ordered) {
          break;
        }
        const QString itemText = current.text();
        if (itemText.isEmpty() && !itemFormat.userProperty(kTaskProperty).isValid()) {
          // Qt keeps an empty block around when a list starts the document; it is
          // an artifact, not a list item.
          current = current.next();
          continue;
        }
        out += QString(depth * 2, QLatin1Char(' '));
        out += ordered ? QString::number(index++) + QStringLiteral(". ") : QStringLiteral("- ");
        if (itemFormat.userProperty(kTaskProperty).isValid()) {
          out += itemFormat.userProperty(kTaskProperty).toBool() ? QStringLiteral("[x] ")
                                                                  : QStringLiteral("[ ] ");
        }
        out += itemText + QStringLiteral("\n");
        current = current.next();
      }
      if (current.isValid() && current.blockFormat().list() == nullptr) {
        out += QStringLiteral("\n");
      }
      block = current;
      continue;
    }

    if (format.headingLevel() > 0) {
      out += QString(format.headingLevel(), QLatin1Char('#')) + QStringLiteral(" ") + text;
      out += QStringLiteral("\n\n");
      block = block.next();
      continue;
    }

    if (!text.isEmpty()) out += text + QStringLiteral("\n\n");
    block = block.next();
  }

  return out;
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
      QTextCursor cellCursor = table->cellTextCursor(row, col);
      cellCursor.insertText(QStringLiteral("r%1c%2").arg(row).arg(col));
    }
  }

  const QString text = document->toPlainText();
  return text.contains(QStringLiteral("r0c0")) && text.contains(QStringLiteral("r1c1"));
}

}  // namespace typist::editor
