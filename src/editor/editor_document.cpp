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
  if (property != 0) blockFormat.setProperty(property, value);
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

  // Verified against Qt 6.12 headers: QTextTableFormat has setColumns only; row
  // count comes from insertTable, and cells are reached through cellAt() plus
  // firstCursorPosition().
  QTextTableFormat tableFormat;
  tableFormat.setColumns(cols);
  cursor.insertBlock();
  QTextTable* qtTable = cursor.insertTable(rows, cols, tableFormat);
  if (qtTable == nullptr) return;

  for (int row = 0; row < rows; ++row) {
    const int cellCount = static_cast<int>(table.children[row].children.size());
    for (int col = 0; col < cellCount; ++col) {
      QTextCursor cellCursor = qtTable->cellAt(row, col).firstCursorPosition();
      cellCursor.insertText(QString::fromStdString(table.children[row].children[col].text));
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
    case typist::core::BlockType::List: {
      // Orderedness lives on the list node, not on its items, so the list drives
      // the marker style while each item contributes text and task state.
      QTextListFormat listFormat;
      listFormat.setStyle(block.ordered ? QTextListFormat::ListDecimal
                                          : QTextListFormat::ListDisc);
      listFormat.setIndent(static_cast<uint>(depth + 1));
      for (const typist::core::Block& item : block.children) {
        cursor.insertList(listFormat);
        if (item.task) {
          QTextBlockFormat itemFormat = cursor.blockFormat();
          itemFormat.setProperty(kTaskProperty, QVariant(item.checked));
          cursor.setBlockFormat(itemFormat);
        }
        cursor.insertText(QString::fromStdString(item.text));
        for (const typist::core::Block& child : item.children) {
          insertBlock(child, cursor, depth + 1, ctx);
        }
      }
      break;
    }
    case typist::core::BlockType::ListItem:
      // Items are reached through their parent list.
      break;
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

  // Nested content must line up with the text after the parent item's marker, so each
  // nesting level contributes its own marker width. Ordered numbering continues across
  // groups the document interleaves. Marker width assumes single-digit item numbers.
  std::vector<int> markerWidths;
  std::vector<int> orderedCounters;
  bool prevWasList = false;

  while (block.isValid()) {
    // A table is a frame: its start and end blocks carry no table object type, so the
    // frame itself is what identifies the table. Emit once at the frame's first position.
    QTextTable* table = QTextCursor(block).currentTable();
    if (table != nullptr) {
      prevWasList = false;
      if (block.position() == table->firstPosition()) {
        const int rows = table->rows();
        const int cols = table->columns();
        for (int row = 0; row < rows; ++row) {
          out += QLatin1Char('|');
          for (int col = 0; col < cols; ++col) {
            // QTextTableCell exposes no text(); the content is read as a selection
            // between the cell's own first and last position.
            const QTextTableCell cell = table->cellAt(row, col);
            QTextCursor cellCursor(cell.firstCursorPosition());
            cellCursor.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
            QString cellText = cellCursor.selectedText();
            cellText.remove(QChar::ObjectReplacementCharacter);
            cellText = cellText.trimmed();
            if (cellText.isEmpty()) cellText = QStringLiteral(" ");
            out += QLatin1Char(' ');
            out += cellText;
            out += QStringLiteral(" |");
          }
          out += QLatin1Char('\n');
          if (row == 0) {
            out += QLatin1Char('|');
            for (int col = 0; col < cols; ++col) out += QStringLiteral(" --- |");
            out += QLatin1Char('\n');
          }
        }
        out += QLatin1Char('\n');
      }
      QTextBlock current = block;
      while (current.isValid() && current.position() < table->lastPosition()) {
        current = current.next();
      }
      if (current.isValid()) current = current.next();
      block = current;
      continue;
    }

    const QTextBlockFormat format = block.blockFormat();
    const QString text = block.text();

    if (format.property(kRuleProperty).toBool()) {
      out += QStringLiteral("---\n\n");
      prevWasList = false;
      block = block.next();
      continue;
    }

    if (format.property(kCodeBlockProperty).toInt() > 0) {
      const int groupId = format.property(kCodeBlockProperty).toInt();
      QTextBlock current = block;
      out += QStringLiteral("```\n");
      while (current.isValid() &&
             current.blockFormat().property(kCodeBlockProperty).toInt() == groupId) {
        out += current.text() + QStringLiteral("\n");
        current = current.next();
      }
      out += QStringLiteral("```\n\n");
      prevWasList = false;
      block = current;
      continue;
    }

    if (format.property(kQuoteProperty).toInt() > 0) {
      const int groupId = format.property(kQuoteProperty).toInt();
      QTextBlock current = block;
      while (current.isValid() &&
             current.blockFormat().property(kQuoteProperty).toInt() == groupId) {
        out += QLatin1Char('>');
        out += QLatin1Char(' ');
        out += current.text() + QStringLiteral("\n");
        current = current.next();
      }
      out += QStringLiteral("\n");
      prevWasList = false;
      block = current;
      continue;
    }

    QTextList* list = block.textList();
    if (list != nullptr) {
      const int depth = std::max(0, static_cast<int>(list->format().indent()) - 1);
      const bool ordered = list->format().style() == QTextListFormat::ListDecimal;
      const std::size_t level = static_cast<std::size_t>(depth);

      int indentSpaces = 0;
      for (std::size_t below = 0; below < level && below < markerWidths.size(); ++below) {
        indentSpaces += markerWidths[below];
      }
      // Resize before reading: a nested list introduces a level the counter
      // vector has not seen yet, and indexing past the end produced garbage.
      if (orderedCounters.size() <= level) orderedCounters.resize(level + 1, 0);
      if (!prevWasList) orderedCounters[level] = 0;
      int index = ordered ? orderedCounters[level] + 1 : 1;

      QTextBlock current = block;
      while (current.isValid()) {
        const QTextBlockFormat itemFormat = current.blockFormat();
        QTextList* itemList = current.textList();
        if (itemList == nullptr ||
            static_cast<int>(itemList->format().indent()) - 1 != depth ||
            (itemList->format().style() == QTextListFormat::ListDecimal) != ordered) {
          break;
        }
        const QString itemText = current.text();
        if (itemText.isEmpty() && !itemFormat.property(kTaskProperty).isValid()) {
          // Qt keeps an empty block around when a list starts the document; it is
          // an artifact, not a list item.
          current = current.next();
          continue;
        }
        out += QString(indentSpaces, QLatin1Char(' '));
        out += ordered ? QString::number(index++) + QStringLiteral(". ") : QStringLiteral("- ");
        if (itemFormat.property(kTaskProperty).isValid()) {
          out += itemFormat.property(kTaskProperty).toBool() ? QStringLiteral("[x] ")
                                                                  : QStringLiteral("[ ] ");
        }
        out += itemText + QStringLiteral("\n");
        current = current.next();
      }
      while (markerWidths.size() < level) markerWidths.push_back(2);
      markerWidths.resize(level + 1);
      markerWidths[level] = ordered ? 3 : 2;
      orderedCounters.resize(level + 1, 0);
      orderedCounters[level] = ordered ? index - 1 : 0;
      prevWasList = true;

      if (current.isValid() && current.textList() == nullptr) {
        out += QStringLiteral("\n");
      }
      block = current;
      continue;
    }

    if (format.headingLevel() > 0) {
      out += QString(format.headingLevel(), QLatin1Char('#')) + QStringLiteral(" ") + text;
      out += QStringLiteral("\n\n");
      prevWasList = false;
      block = block.next();
      continue;
    }

    if (!text.isEmpty()) {
      out += text + QStringLiteral("\n\n");
      prevWasList = false;
    }
    block = block.next();
  }

  return out;
}

bool insertTableProbe(QTextDocument* document, int rows, int cols) {
  QTextCursor cursor(document);
  cursor.movePosition(QTextCursor::End);
  cursor.insertBlock();

  QTextTableFormat tableFormat;
  tableFormat.setColumns(cols);
  QTextTable* table = cursor.insertTable(rows, cols, tableFormat);
  if (table == nullptr) return false;

  for (int row = 0; row < rows; ++row) {
    for (int col = 0; col < cols; ++col) {
      QTextCursor cellCursor = table->cellAt(row, col).firstCursorPosition();
      cellCursor.insertText(QStringLiteral("r%1c%2").arg(row).arg(col));
    }
  }

  const QString text = document->toPlainText();
  return text.contains(QStringLiteral("r0c0")) && text.contains(QStringLiteral("r1c1"));
}

}  // namespace typist::editor
