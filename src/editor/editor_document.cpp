#include "editor_document.h"

#include <QFont>
#include <QFont>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QFileInfo>
#include <QImageReader>
#include <QTextImageFormat>
#include <QTextLayout>
#include <QUrl>
#include <QTextList>
#include <QTextListFormat>
#include <QTextTable>
#include <QTextTableFormat>
#include <QVariant>
#include <QVector>

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


// Inline runs carry styling that Qt models as character formats: weight, slant,
// strike, a monospace family for code, and an anchor for links and images.
QTextCharFormat formatForRun(const typist::core::InlineRun& run) {
  QTextCharFormat fmt;
  if (run.bold) fmt.setFontWeight(QFont::Bold);
  if (run.italic) fmt.setFontItalic(true);
  if (run.strike) fmt.setFontStrikeOut(true);
  if (run.code) fmt.setFontFamilies({QStringLiteral("Consolas"), QStringLiteral("Courier New")});
  if (run.image) {
    fmt.setProperty(kImageProperty, QVariant(QString::fromStdString(run.url)));
    fmt.setAnchor(true);
    fmt.setAnchorHref(QString::fromStdString(run.url));
    fmt.setProperty(QTextFormat::TextUnderlineStyle, QVariant::fromValue(QTextCharFormat::SingleUnderline));
  } else if (run.link) {
    fmt.setAnchor(true);
    fmt.setAnchorHref(QString::fromStdString(run.url));
    fmt.setProperty(QTextFormat::TextUnderlineStyle, QVariant::fromValue(QTextCharFormat::SingleUnderline));
  }
  return fmt;
}

void insertInline(const std::vector<typist::core::InlineRun>& runs, const QString& fallback,
                  QTextCursor& cursor) {
  if (runs.empty()) {
    cursor.insertText(fallback);
    return;
  }
  for (const typist::core::InlineRun& run : runs) {
    const QString text = QString::fromStdString(run.text);
    const QString target = QString::fromStdString(run.url);
    if (run.image && !target.isEmpty() && QFileInfo(target).isFile()) {
      // Show the image itself; the object run keeps target and alt so serialization
      // can re-emit the Markdown link.
      // A large photo is scaled down so the model stays bounded; the aspect ratio holds.
      QImageReader reader(target);
      QSize natural = reader.size();
      if (natural.width() > 640 || natural.height() > 480) {
        const double scale = qMin(640.0 / double(natural.width()), 480.0 / double(natural.height()));
        natural = QSize(int(double(natural.width()) * scale), int(double(natural.height()) * scale));
      }
      const QImage loaded = reader.read();
      const QUrl resourceUrl = QUrl::fromLocalFile(target);
      cursor.document()->addResource(QVariant::Url, resourceUrl, loaded);
      QTextImageFormat imageFormat;
      imageFormat.setName(resourceUrl.toString());
      if (natural.width() > 0 && natural.height() > 0) {
        imageFormat.setWidth(natural.width());
        imageFormat.setHeight(natural.height());
      }
      cursor.insertImage(imageFormat);
      const QString blockText = cursor.block().text();
      const int objectIndex = blockText.indexOf(QChar(0xFFFC));
      if (objectIndex >= 0) {
        // Reuse the object format: a fresh format would drop the image name and size.
        cursor.setPosition(cursor.block().position() + objectIndex, QTextCursor::MoveAnchor);
        cursor.setPosition(cursor.block().position() + objectIndex + 1, QTextCursor::KeepAnchor);
        QTextCharFormat objectFormat = cursor.charFormat();
        objectFormat.setProperty(kImageProperty, QVariant(target));
        objectFormat.setProperty(kImageAltProperty, QVariant(text));
        cursor.setCharFormat(objectFormat);
      }
      continue;
    }
    cursor.insertText(text, formatForRun(run));
  }
}

void insertTaggedInline(QTextCursor& cursor, const std::vector<typist::core::InlineRun>& runs,
                        const QString& text, uint property, const QVariant& value) {
  QTextBlockFormat blockFormat;
  if (property != 0) blockFormat.setProperty(property, value);
  cursor.insertBlock(blockFormat);
  insertInline(runs, text, cursor);
}
void insertCodeBlock(const std::string& code, const std::string& info, QTextCursor& cursor,
                     BuildContext& ctx) {
  QTextCharFormat codeFormat;
  QFont mono(QStringLiteral("monospace"));
  mono.setStyleHint(QFont::TypeWriter);
  codeFormat.setFont(mono);

  const QVariant groupId = QVariant(++ctx.groupCounter);
  const QVariant infoValue = QString::fromStdString(info);
  const auto emitLine = [&](const std::string& line) {
    QTextBlockFormat blockFormat;
    blockFormat.setProperty(kCodeBlockProperty, groupId);
    // cmark exposes no accessor for the fence label, so it travels as a document
    // property on the group's first block.
    if (!info.empty()) blockFormat.setProperty(kCodeInfoProperty, infoValue);
    cursor.insertBlock(blockFormat);
    cursor.insertText(QString::fromStdString(line), codeFormat);
  };

  std::string line;
  for (char c : code) {
    if (c == '\n') {
      if (!line.empty()) {
        emitLine(line);
        line.clear();
      }
    } else {
      line += c;
    }
  }
  if (!line.empty()) emitLine(line);
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
      const typist::core::Block& cell = table.children[row].children[col];
      insertInline(cell.runs, QString::fromStdString(cell.text), cellCursor);
    }
  }

  // insertTable leaves the cursor inside the frame, so the next insertBlock would land
  // in the last cell and swallow everything that follows the table. Moving to the end
  // first lets Qt place the following block outside the frame; positioning at
  // lastPosition instead grew the frame over the following blocks.
  cursor.movePosition(QTextCursor::End);
  cursor.insertBlock(QTextBlockFormat());
}

void insertBlock(const typist::core::Block& block, QTextCursor& cursor, int depth,
                 BuildContext& ctx) {
  const QTextCharFormat plain;

  switch (block.type) {
    case typist::core::BlockType::Heading: {
      QTextBlockFormat headingFormat;
      headingFormat.setHeadingLevel(block.level);
      cursor.insertBlock(headingFormat);
      insertInline(block.runs, QString::fromStdString(block.text), cursor);
      break;
    }
    case typist::core::BlockType::CodeBlock:
      insertCodeBlock(block.text, block.info, cursor, ctx);
      break;
    case typist::core::BlockType::ThematicBreak:
      insertTaggedBlock(cursor, QStringLiteral("\u2014"), kRuleProperty, QVariant(true), plain);
      break;
    case typist::core::BlockType::BlockQuote:
      ++ctx.groupCounter;
      for (const typist::core::Block& child : block.children) {
        insertTaggedInline(cursor, child.runs, QString::fromStdString(child.text), kQuoteProperty,
                            QVariant(ctx.groupCounter));
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
        insertInline(item.runs, QString::fromStdString(item.text), cursor);
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
      insertTaggedInline(cursor, block.runs, QString::fromStdString(block.text), 0, QVariant());
      break;
  }
}


struct MarkdownRun {
  QString text;
  QString url;
  bool bold = false;
  bool italic = false;
  bool strike = false;
  bool code = false;
  bool link = false;
  bool image = false;
};

void pushMarkdownRun(QVector<MarkdownRun>& runs, const MarkdownRun& run) {
  if (run.text.isEmpty()) return;
  if (!runs.empty() && runs.back().bold == run.bold && runs.back().italic == run.italic &&
      runs.back().strike == run.strike && runs.back().code == run.code &&
      runs.back().link == run.link && runs.back().image == run.image && runs.back().url == run.url) {
    runs.back().text += run.text;
    return;
  }
  runs.push_back(run);
}

QString wrapCodeText(const QString& text) {
  int longest = 0;
  int current = 0;
  for (const QChar ch : text) {
    if (ch == QLatin1Char('`')) ++current; else current = 0;
    longest = std::max(longest, current);
  }
  const QString marker(longest + 1, QLatin1Char('`'));
  const bool pad = !text.isEmpty() && (text.front() == QLatin1Char('`') || text.back() == QLatin1Char('`'));
  if (pad) return marker + QLatin1Char(' ') + text + QLatin1Char(' ') + marker;
  return marker + text + marker;
}

QString wrapMarkdownRun(const MarkdownRun& run) {
  QString body = run.text;
  if (run.code) body = wrapCodeText(body);
  if (run.bold) body = QStringLiteral("**") + body + QStringLiteral("**");
  if (run.italic) body = QLatin1Char('*') + body + QLatin1Char('*');
  if (run.strike) body = QStringLiteral("~~") + body + QStringLiteral("~~");
  if (run.image) return QStringLiteral("![") + body + QStringLiteral("](") + run.url + QStringLiteral(")");
  if (run.link) return QStringLiteral("[") + body + QStringLiteral("](") + run.url + QStringLiteral(")");
  return body;
}

QString wrapMarkdownRuns(const QVector<MarkdownRun>& runs) {
  QString out;
  for (const MarkdownRun& run : runs) out += wrapMarkdownRun(run);
  return out;
}

// Inline code is the one family the builder sets; code blocks are separate blocks.
bool isInlineCodeFormat(const QTextCharFormat& fmt) {
  for (const QString& family : fmt.fontFamilies().toStringList()) {
    if (family.compare(QLatin1String("Consolas"), Qt::CaseInsensitive) == 0) return true;
  }
  return false;
}

// textFormats gives styled ranges over the block text, so classification never walks chars.
QString inlineFromBlock(const QTextBlock& block) {
  const QString text = block.text();
  // Most blocks carry no styling at all; the fast path keeps serialization
  // allocation-free for them.
  if (text.isEmpty() || block.textFormats().isEmpty()) return text;
  QVector<MarkdownRun> runs;
  int pos = 0;
  for (const QTextLayout::FormatRange& range : block.textFormats()) {
    const int end = range.start + range.length;
    if (range.start < pos || end <= pos) continue;
    if (range.start > pos) {
      MarkdownRun plain;
      plain.text = text.mid(pos, range.start - pos);
      pushMarkdownRun(runs, plain);
      pos = range.start;
    }
    const QTextCharFormat& fmt = range.format;
    MarkdownRun run;
    run.text = text.mid(pos, end - pos);
    run.bold = fmt.fontWeight() >= QFont::Bold;
    run.italic = fmt.fontItalic();
    run.strike = fmt.fontStrikeOut();
    run.code = isInlineCodeFormat(fmt);
    if (fmt.hasProperty(kImageProperty)) {
      run.image = true;
      run.url = fmt.property(kImageProperty).toString();
      const QString alt = fmt.property(kImageAltProperty).toString();
      if (!alt.isEmpty()) run.text = alt;  // the block text here is the object placeholder
    } else if (fmt.isAnchor()) {
      run.link = true;
      run.url = fmt.anchorHref();
    }
    pushMarkdownRun(runs, run);
    pos = end;
  }
  if (pos < text.size()) {
    MarkdownRun tail;
    tail.text = text.mid(pos);
    pushMarkdownRun(runs, tail);
  }
  return wrapMarkdownRuns(runs);
}
}  // namespace

void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document) {
  document->clear();

  // A build is a bulk load. Undo commands and change notifications per block cost
  // seconds on a large document, so the whole build is one edit block with undo off;
  // loadMarkdown clears the stack afterwards anyway.
  const bool undoWasEnabled = document->isUndoRedoEnabled();
  document->setUndoRedoEnabled(false);

  QTextCursor cursor(document);
  cursor.beginEditBlock();
  BuildContext ctx;
  for (const typist::core::Block& block : blocks) insertBlock(block, cursor, 0, ctx);
  cursor.endEditBlock();

  document->setUndoRedoEnabled(undoWasEnabled);
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
            QString cellText;
            QTextBlock cellBlock = cell.firstCursorPosition().block();
            const int cellEnd = cell.lastPosition();
            while (cellBlock.isValid() && cellBlock.position() < cellEnd) {
              if (!cellText.isEmpty()) cellText += QStringLiteral("<br>");
              cellText += inlineFromBlock(cellBlock);
              cellBlock = cellBlock.next();
            }
            cellText.remove(QChar::ObjectReplacementCharacter);
            cellText = cellText.trimmed();
            // A merged cell repeats its content across the span; Markdown has no
            // merge, so only the origin cell keeps the text and the rest go empty.
            if (cell.row() != row || cell.column() != col) cellText.clear();
            cellText.replace(QLatin1Char('|'), QStringLiteral("\\|"));
            cellText.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
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
      // lastPosition is the start of the block after the table, so landing on it is
      // already past the table. Advancing one more dropped every block that followed a
      // table, which collapsed long documents to a run of tables.
      QTextBlock current = block;
      while (current.isValid() && current.position() < table->lastPosition()) {
        current = current.next();
      }
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
      out += QStringLiteral("```") + format.property(kCodeInfoProperty).toString() + QStringLiteral("\n");
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
        out += inlineFromBlock(current) + QStringLiteral("\n");
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
        const QString itemText = inlineFromBlock(current);
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
      out += inlineFromBlock(block) + QStringLiteral("\n\n");
      prevWasList = false;
    }
    block = block.next();
  }

  return out;
}

bool insertMarkdownAtCursor(const QString& source, QTextCursor& cursor, QString* error) {
  const typist::core::ParseResult parsed =
      typist::core::parseMarkdown(source.toStdString(), typist::core::ParseOptions{});
  if (!parsed.ok) {
    if (error != nullptr) *error = QString::fromStdString(parsed.error);
    return false;
  }
  // One undo step for the whole paste; the pasted content starts on a fresh line.
  cursor.beginEditBlock();
  cursor.insertBlock();
  BuildContext ctx;
  for (const typist::core::Block& block : parsed.blocks) insertBlock(block, cursor, 0, ctx);
  cursor.endEditBlock();
  return true;
}

bool loadMarkdown(const QString& source, QTextDocument* document, QString* error) {
  const auto parsed = typist::core::parseMarkdown(source.toStdString());
  if (!parsed.ok) {
    if (error != nullptr) *error = QString::fromStdString(parsed.error);
    return false;
  }
  buildDocument(parsed.blocks, document);
  // A rebuild is not an edit: undo must stop at this boundary.
  document->clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);
  return true;
}

void boundUndoHistory(QTextDocument* document) {
  if (document->availableUndoSteps() > kUndoStepLimit) {
    document->clearUndoRedoStacks(QTextDocument::UndoStack);
  }
}

bool taskCheckboxState(const QTextDocument* document, int blockNumber, bool* checkedOut) {
  if (document == nullptr) return false;
  const QTextBlock block = document->findBlockByNumber(blockNumber);
  if (!block.isValid()) return false;
  const QVariant value = block.blockFormat().property(kTaskProperty);
  if (!value.isValid()) return false;
  if (checkedOut != nullptr) *checkedOut = value.toBool();
  return true;
}

bool toggleTaskCheckbox(QTextDocument* document, int blockNumber) {
  bool checked = false;
  if (!taskCheckboxState(document, blockNumber, &checked)) return false;
  QTextCursor cursor(document->findBlockByNumber(blockNumber));
  QTextBlockFormat format = cursor.blockFormat();
  format.setProperty(kTaskProperty, !checked);
  cursor.beginEditBlock();
  cursor.setBlockFormat(format);
  cursor.endEditBlock();
  return true;
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
