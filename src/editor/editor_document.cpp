#include "editor_document.h"

#include "ImageResources.h"
#include "SpecialContent.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QHash>
#include <QImageReader>
#include <QString>
#include <QStringList>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocumentFragment>
#include <QTextImageFormat>
#include <QTextLayout>
#include <QTextList>
#include <QTextListFormat>
#include <QTextTable>
#include <QTextTableFormat>
#include <QUrl>
#include <QVariant>
#include <QVector>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace typist::editor {
namespace {

struct BuildContext {
  int groupCounter = 0;
};

QTextBlockFormat bodyFormat() {
  QTextBlockFormat format;
  format.setBottomMargin(12);
  format.setLineHeight(160, QTextBlockFormat::ProportionalHeight);
  return format;
}

void insertTaggedBlock(QTextCursor& cursor, const QString& text, uint property,
                       const QVariant& value, const QTextCharFormat& charFormat) {
  QTextBlockFormat blockFormat = bodyFormat();
  if (property != 0) blockFormat.setProperty(property, value);
  if (property == kQuoteProperty) {
    blockFormat.setLeftMargin(20);
    blockFormat.setRightMargin(12);
  }
  cursor.insertBlock(blockFormat, QTextCharFormat());
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
    fmt.setProperty(QTextFormat::TextUnderlineStyle,
                    QVariant::fromValue(QTextCharFormat::SingleUnderline));
  } else if (run.link) {
    fmt.setAnchor(true);
    fmt.setAnchorHref(QString::fromStdString(run.url));
    fmt.setProperty(QTextFormat::TextUnderlineStyle,
                    QVariant::fromValue(QTextCharFormat::SingleUnderline));
  }
  return fmt;
}

void insertInline(const std::vector<typist::core::InlineRun>& runs, const QString& fallback,
                  QTextCursor& cursor, const QTextCharFormat& base = QTextCharFormat()) {
  if (runs.empty()) {
    cursor.insertText(QString(fallback).replace(QLatin1Char('\n'), QChar::LineSeparator), base);
    return;
  }
  for (const typist::core::InlineRun& run : runs) {
    const QString text = QString::fromStdString(run.text);
    if (run.math) {
      const auto rendered = typist::render::math(text, false,
                                                 cursor.document()->property("typistDark").toBool()
                                                     ? QColor("#dde3ec")
                                                     : QColor("#333333"));
      if (!rendered.image.isNull()) {
        const QString name =
            QStringLiteral("typist-math:") +
            QString::fromLatin1(
                QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
        cursor.document()->addResource(QTextDocument::ImageResource, QUrl(name), rendered.image);
        QTextImageFormat format;
        format.setName(name);
        format.setWidth(rendered.image.width() / 2.);
        format.setHeight(rendered.image.height() / 2.);
        format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
        format.setProperty(kMathSourceProperty, text);
        cursor.insertImage(format);
      } else {
        cursor.insertText(QStringLiteral("$") + text + QStringLiteral("$"), base);
      }
      cursor.setCharFormat(base);
      continue;
    }
    if (run.footnote) {
      QTextCharFormat format = base;
      format.setProperty(kFootnoteReferenceProperty, text);
      format.setVerticalAlignment(QTextCharFormat::AlignSuperScript);
      format.setAnchor(true);
      format.setAnchorHref(QStringLiteral("#fn-") + text);
      format.setForeground(QColor("#4d90ce"));
      cursor.insertText(text, format);
      cursor.setCharFormat(base);
      continue;
    }
    const QString target = QString::fromStdString(run.url);
    const QString localTarget = render::localImagePath(target, cursor.document()->baseUrl());
    if (run.image && !target.isEmpty() && QFileInfo(localTarget).isFile()) {
      // Show the image itself; the object run keeps target and alt so serialization
      // can re-emit the Markdown link.
      // A large photo is scaled down so the model stays bounded; the aspect ratio holds.
      const QImage loaded = render::imagePreview(localTarget);
      if (loaded.isNull()) {
        QTextCharFormat fallback = base;
        fallback.merge(formatForRun(run));
        cursor.insertText(text, fallback);
        continue;
      }
      const QUrl resourceUrl = QUrl::fromLocalFile(localTarget);
      cursor.document()->addResource(QTextDocument::ImageResource, resourceUrl, loaded);
      QTextImageFormat imageFormat;
      imageFormat.setName(resourceUrl.toString());
      imageFormat.setWidth(loaded.width());
      imageFormat.setHeight(loaded.height());
      const int imagePosition = cursor.position();
      cursor.insertImage(imageFormat);
      {
        // Reuse the object format: a fresh format would drop the image name and size.
        cursor.setPosition(imagePosition, QTextCursor::MoveAnchor);
        cursor.setPosition(imagePosition + 1, QTextCursor::KeepAnchor);
        QTextCharFormat objectFormat = cursor.charFormat();
        objectFormat.setProperty(kImageProperty, QVariant(target));
        objectFormat.setProperty(kImageAltProperty, QVariant(text));
        cursor.setCharFormat(objectFormat);
      }
      cursor.clearSelection();
      cursor.setCharFormat(base);
      continue;
    }
    QTextCharFormat format = base;
    format.merge(formatForRun(run));
    if (run.bold) format.clearProperty(kPresentationBoldProperty);
    cursor.insertText(QString(text).replace(QLatin1Char('\n'), QChar::LineSeparator), format);
  }
}

void insertTaggedInline(QTextCursor& cursor, const std::vector<typist::core::InlineRun>& runs,
                        const QString& text, uint property, const QVariant& value) {
  QTextBlockFormat blockFormat = bodyFormat();
  if (property != 0) blockFormat.setProperty(property, value);
  if (property == kQuoteProperty) blockFormat.setLeftMargin(20);
  cursor.insertBlock(blockFormat, QTextCharFormat());
  insertInline(runs, text, cursor);
}
void insertCodeBlock(const std::string& code, const std::string& info, QTextCursor& cursor,
                     BuildContext& ctx) {
  QTextCharFormat codeFormat;
  QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  mono.setFamilies(
      {QStringLiteral("Consolas"), QStringLiteral("Menlo"), QStringLiteral("monospace")});
  mono.setStyleHint(QFont::TypeWriter);
  mono.setPointSizeF(10.5);
  codeFormat.setFont(mono);

  const QVariant groupId = QVariant(++ctx.groupCounter);
  const QVariant infoValue = QString::fromStdString(info);
  bool firstLine = true;
  const auto emitLine = [&](const std::string& line) {
    QTextBlockFormat blockFormat;
    blockFormat.setLineHeight(140, QTextBlockFormat::ProportionalHeight);
    blockFormat.setLeftMargin(16);
    blockFormat.setRightMargin(16);
    blockFormat.setTopMargin(firstLine ? 10 : 0);
    firstLine = false;
    blockFormat.setNonBreakableLines(false);
    blockFormat.setProperty(kCodeBlockProperty, groupId);
    if (code.empty()) blockFormat.setProperty(kEmptyCodeFenceProperty, true);
    // cmark exposes no accessor for the fence label, so it travels as a document
    // property on the group's first block.
    if (!info.empty()) blockFormat.setProperty(kCodeInfoProperty, infoValue);
    cursor.insertBlock(blockFormat, codeFormat);
    cursor.setBlockCharFormat(codeFormat);
    cursor.insertText(QString::fromStdString(line), codeFormat);
  };

  std::string line;
  for (char c : code) {
    if (c == '\n') {
      emitLine(line);
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty() || code.empty()) emitLine(line);
  QTextBlockFormat last = cursor.blockFormat();
  last.setBottomMargin(10);
  cursor.setBlockFormat(last);
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
  tableFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100));
  tableFormat.setCellPadding(9);
  tableFormat.setCellSpacing(0);
  tableFormat.setBorder(0.5);
  tableFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
  tableFormat.setBorderBrush(QColor("#a5abb5"));
  tableFormat.setHeaderRowCount(1);
  tableFormat.setTopMargin(8);
  tableFormat.setBottomMargin(16);
  cursor.insertBlock();
  QTextTable* qtTable = cursor.insertTable(rows, cols, tableFormat);
  if (qtTable == nullptr) return;

  for (int row = 0; row < rows; ++row) {
    const int cellCount = static_cast<int>(table.children[row].children.size());
    for (int col = 0; col < cellCount; ++col) {
      QTextCursor cellCursor = qtTable->cellAt(row, col).firstCursorPosition();
      const typist::core::Block& cell = table.children[row].children[col];
      QTextTableCellFormat cellFormat;
      cellFormat.setBorder(0.5);
      cellFormat.setBorderStyle(QTextFrameFormat::BorderStyle_Solid);
      cellFormat.setBorderBrush(QColor("#a5abb5"));
      qtTable->cellAt(row, col).setFormat(cellFormat);
      QTextBlockFormat cellBlock = bodyFormat();
      cellBlock.setBottomMargin(0);
      const int alignment =
          col < static_cast<int>(table.alignments.size()) ? table.alignments[col] : 0;
      cellBlock.setProperty(kTableAlignmentProperty, alignment);
      cellBlock.setAlignment(alignment == 2   ? Qt::AlignHCenter
                             : alignment == 3 ? Qt::AlignRight
                                              : Qt::AlignLeft);
      cellCursor.setBlockFormat(cellBlock);
      QTextCharFormat base;
      if (row == 0) {
        base.setFontWeight(QFont::Bold);
        base.setProperty(kPresentationBoldProperty, true);
      }
      insertInline(cell.runs, QString::fromStdString(cell.text), cellCursor, base);
    }
  }

  // insertTable leaves the cursor inside the frame, so the next insertBlock would land
  // in the last cell and swallow everything that follows the table. Moving to the end
  // first lets Qt place the following block outside the frame; positioning at
  // lastPosition instead grew the frame over the following blocks.
  cursor.movePosition(QTextCursor::End);
  cursor.insertBlock(bodyFormat(), QTextCharFormat());
}

void insertBlock(const typist::core::Block& block, QTextCursor& cursor, int depth,
                 BuildContext& ctx) {
  const QTextCharFormat plain;

  switch (block.type) {
    case typist::core::BlockType::Heading: {
      QTextBlockFormat headingFormat = bodyFormat();
      headingFormat.setHeadingLevel(block.level);
      headingFormat.setTopMargin(block.level == 1 ? 8 : 22);
      headingFormat.setBottomMargin(block.level == 1 ? 18 : 10);
      headingFormat.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
      cursor.insertBlock(headingFormat, QTextCharFormat());
      static const double sizes[] = {28, 22, 18, 15, 13, 12};
      QTextCharFormat base;
      base.setFontPointSize(sizes[qBound(1, block.level, 6) - 1]);
      base.setFontWeight(QFont::Bold);
      base.setProperty(kPresentationBoldProperty, true);
      insertInline(block.runs, QString::fromStdString(block.text), cursor, base);
      break;
    }
    case typist::core::BlockType::CodeBlock:
      insertCodeBlock(block.text, block.info, cursor, ctx);
      break;
    case typist::core::BlockType::Math:
    case typist::core::BlockType::Diagram: {
      const bool formula = block.type == typist::core::BlockType::Math;
      const QString source = QString::fromStdString(block.text);
      const bool dark = cursor.document()->property("typistDark").toBool();
      const auto result =
          formula ? typist::render::math(source, true, dark ? QColor("#dde3ec") : QColor("#333333"))
                  : typist::render::mermaid(source, dark);
      if (result.image.isNull() && !formula) {
        insertCodeBlock(block.text, "mermaid", cursor, ctx);
        break;
      }
      QTextBlockFormat format = bodyFormat();
      format.setProperty(kSpecialBlockProperty, formula ? "math" : "mermaid");
      format.setAlignment(Qt::AlignHCenter);
      format.setTopMargin(16);
      format.setBottomMargin(20);
      format.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
      cursor.insertBlock(format, QTextCharFormat());
      if (!result.image.isNull()) {
        const QString name = QStringLiteral("typist-special:") +
                             QString::fromLatin1(QCryptographicHash::hash(
                                                     (QString::number(formula) + source).toUtf8(),
                                                     QCryptographicHash::Sha256)
                                                     .toHex());
        cursor.document()->addResource(QTextDocument::ImageResource, QUrl(name), result.image);
        QTextImageFormat image;
        image.setName(name);
        const qreal scale = qMin(1., 760. / (result.image.width() / 2.));
        image.setWidth(result.image.width() / 2. * scale);
        image.setHeight(result.image.height() / 2. * scale);
        image.setProperty(kSpecialSourceProperty, source);
        image.setProperty(kSpecialTypeProperty, formula ? "math" : "mermaid");
        image.setToolTip(QStringLiteral("Double-click to edit source"));
        cursor.insertImage(image);
      } else
        cursor.insertText(source);
      cursor.setCharFormat(QTextCharFormat());
      break;
    }
    case typist::core::BlockType::TableOfContents: {
      QTextBlockFormat format = bodyFormat();
      format.setProperty(kSpecialBlockProperty, "toc");
      format.setLineHeight(100, QTextBlockFormat::ProportionalHeight);
      cursor.insertBlock(format, QTextCharFormat());
      QTextCharFormat object;
      object.setObjectType(typist::render::kTocObjectType);
      cursor.insertText(QString(QChar::ObjectReplacementCharacter), object);
      cursor.setCharFormat(QTextCharFormat());
      break;
    }
    case typist::core::BlockType::Footnote: {
      QTextBlockFormat format = bodyFormat();
      format.setProperty(kFootnoteDefinitionProperty, QString::fromStdString(block.info));
      format.setTopMargin(8);
      format.setLeftMargin(16);
      cursor.insertBlock(format, QTextCharFormat());
      QTextCharFormat marker;
      marker.setProperty(kFootnoteLabelProperty, true);
      marker.setFontPointSize(10);
      marker.setForeground(QColor("#4d90ce"));
      cursor.insertText(QStringLiteral("[%1] ").arg(QString::fromStdString(block.info)), marker);
      QTextCharFormat body;
      body.setFontPointSize(10);
      cursor.insertText(
          QString::fromStdString(block.text).replace(QLatin1Char('\n'), QChar::LineSeparator),
          body);
      break;
    }
    case typist::core::BlockType::ThematicBreak:
      insertTaggedBlock(cursor, QStringLiteral("\u2014"), kRuleProperty, QVariant(true), plain);
      break;
    case typist::core::BlockType::BlockQuote:
      ++ctx.groupCounter;
      for (const typist::core::Block& child : block.children) {
        if (child.type == core::BlockType::Paragraph) {
          insertTaggedInline(cursor, child.runs, QString::fromStdString(child.text), kQuoteProperty,
                             QVariant(ctx.groupCounter));
        } else {
          const QString raw = QString::fromStdString(core::serializeMarkdown({child})).trimmed();
          for (const QString& line : raw.split(QLatin1Char('\n')))
            insertTaggedInline(cursor, {}, line, kQuoteProperty, QVariant(ctx.groupCounter));
        }
      }
      break;
    case typist::core::BlockType::Table:
      insertTable(block, cursor);
      break;
    case typist::core::BlockType::List: {
      // Orderedness lives on the list node, not on its items, so the list drives
      // the marker style while each item contributes text and task state.
      // A task list draws real checkbox glyphs instead of Qt's bullet, so the marker
      // style is dropped only when every item in the list is a task item.
      bool allTasks = !block.children.empty();
      for (const typist::core::Block& item : block.children) {
        if (!item.task) allTasks = false;
      }
      QTextListFormat listFormat;
      listFormat.setStyle(
          allTasks ? QTextListFormat::ListStyleUndefined
                   : (block.ordered ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc));
      listFormat.setIndent(static_cast<uint>(depth + 1));
      for (const typist::core::Block& item : block.children) {
        cursor.setCharFormat(QTextCharFormat());
        cursor.insertList(listFormat);
        // insertList inherits the previous block format, including a heading's
        // level. Keep only the list object so items never become outline entries.
        QTextBlockFormat spacing = bodyFormat();
        spacing.setObjectIndex(cursor.blockFormat().objectIndex());
        spacing.setTopMargin(0);
        spacing.setBottomMargin(5);
        spacing.setLineHeight(150, QTextBlockFormat::ProportionalHeight);
        cursor.setBlockFormat(spacing);
        if (item.task) {
          QTextBlockFormat itemFormat = cursor.blockFormat();
          itemFormat.setProperty(kTaskProperty, QVariant(item.checked));
          cursor.setBlockFormat(itemFormat);
          QTextCharFormat markFormat = cursor.charFormat();
          markFormat.setProperty(kTaskMarkProperty, QVariant(item.checked));
          cursor.insertText(item.checked ? QString(QChar(0x2611)) : QString(QChar(0x2610)),
                            markFormat);
          cursor.insertText(QStringLiteral(" "), markFormat);
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
  bool math = false;
  bool footnote = false;
  QString specialType;
};

void pushMarkdownRun(QVector<MarkdownRun>& runs, const MarkdownRun& run) {
  if (run.text.isEmpty() && run.specialType.isEmpty()) return;
  if (!runs.empty() && runs.back().bold == run.bold && runs.back().italic == run.italic &&
      runs.back().strike == run.strike && runs.back().code == run.code &&
      runs.back().link == run.link && runs.back().image == run.image && !run.math &&
      !run.footnote && !runs.back().math && !runs.back().footnote && run.specialType.isEmpty() &&
      runs.back().specialType.isEmpty() && runs.back().url == run.url) {
    runs.back().text += run.text;
    return;
  }
  runs.push_back(run);
}

QString wrapCodeText(const QString& text) {
  int longest = 0;
  int current = 0;
  for (const QChar ch : text) {
    if (ch == QLatin1Char('`'))
      ++current;
    else
      current = 0;
    longest = std::max(longest, current);
  }
  const QString marker(longest + 1, QLatin1Char('`'));
  const bool pad =
      !text.isEmpty() && (text.front() == QLatin1Char('`') || text.back() == QLatin1Char('`'));
  if (pad) return marker + QLatin1Char(' ') + text + QLatin1Char(' ') + marker;
  return marker + text + marker;
}

QString wrapMarkdownRun(const MarkdownRun& run) {
  if (run.specialType == QLatin1String("math"))
    return QStringLiteral("\n\n$$\n") + run.text + QStringLiteral("\n$$\n\n");
  if (run.specialType == QLatin1String("mermaid"))
    return QStringLiteral("\n\n```mermaid\n") + run.text +
           (run.text.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n")) +
           QStringLiteral("```\n\n");
  if (run.math) return QStringLiteral("$") + run.text + QStringLiteral("$");
  if (run.footnote) return QStringLiteral("[^") + run.text + QStringLiteral("]");
  QString body = run.text;
  if (run.code) body = wrapCodeText(body);
  if (run.bold) body = QStringLiteral("**") + body + QStringLiteral("**");
  if (run.italic) body = QLatin1Char('*') + body + QLatin1Char('*');
  if (run.strike) body = QStringLiteral("~~") + body + QStringLiteral("~~");
  if (run.image)
    return QStringLiteral("![") + body + QStringLiteral("](") + run.url + QStringLiteral(")");
  if (run.link)
    return QStringLiteral("[") + body + QStringLiteral("](") + run.url + QStringLiteral(")");
  return body;
}

QString wrapMarkdownRuns(const QVector<MarkdownRun>& runs) {
  QString out;
  std::vector<core::InlineRun> inlineRuns;
  const auto flush = [&] {
    out += QString::fromStdString(core::serializeInline(inlineRuns));
    inlineRuns.clear();
  };
  for (const MarkdownRun& run : runs) {
    if (!run.specialType.isEmpty()) {
      flush();
      out += wrapMarkdownRun(run);
      continue;
    }
    core::InlineRun span;
    span.text = run.text.toStdString();
    span.url = run.url.toStdString();
    span.bold = run.bold;
    span.italic = run.italic;
    span.strike = run.strike;
    span.code = run.code;
    span.link = run.link;
    span.image = run.image;
    span.math = run.math;
    span.footnote = run.footnote;
    inlineRuns.push_back(std::move(span));
  }
  flush();
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
  if (text.isEmpty() || block.textFormats().isEmpty())
    return QString(text).replace(QChar::LineSeparator, QStringLiteral("  \n"));
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
    if (fmt.hasProperty(kFootnoteLabelProperty)) {
      pos = end;
      continue;
    }
    MarkdownRun run;
    run.text = text.mid(pos, end - pos);
    run.bold = fmt.fontWeight() >= QFont::Bold && !fmt.boolProperty(kPresentationBoldProperty);
    run.italic = fmt.fontItalic();
    run.strike = fmt.fontStrikeOut();
    run.code = isInlineCodeFormat(fmt);
    if (fmt.hasProperty(kSpecialSourceProperty)) {
      run.specialType = fmt.property(kSpecialTypeProperty).toString();
      if (run.specialType.isEmpty())
        run.specialType = block.blockFormat().property(kSpecialBlockProperty).toString();
      run.text = fmt.property(kSpecialSourceProperty).toString();
      run.bold = run.italic = run.strike = run.code = false;
    } else if (fmt.objectType() == typist::render::kTocObjectType) {
      run.text = QStringLiteral("[TOC]");
    } else if (fmt.hasProperty(kMathSourceProperty)) {
      run.math = true;
      run.text = fmt.property(kMathSourceProperty).toString();
      run.bold = run.italic = run.strike = run.code = false;
    } else if (fmt.hasProperty(kFootnoteReferenceProperty)) {
      run.footnote = true;
      run.text = fmt.property(kFootnoteReferenceProperty).toString();
      run.bold = run.italic = run.strike = run.code = false;
    } else if (fmt.hasProperty(kImageProperty)) {
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
  return wrapMarkdownRuns(runs).replace(QChar::LineSeparator, QStringLiteral("  \n"));
}
}  // namespace

QString inlineMarkdown(const QTextBlock& block) { return inlineFromBlock(block); }

void buildDocument(const std::vector<typist::core::Block>& blocks, QTextDocument* document) {
  document->clear();
  QFont body;
  body.setFamilies({QStringLiteral("Arial"), QStringLiteral("Microsoft YaHei"),
                    QStringLiteral("Noto Sans"), QStringLiteral("sans-serif")});
  body.setPointSizeF(12);
  document->setDefaultFont(body);
  document->setDocumentMargin(0);
  document->setIndentWidth(24);
  QTextOption option = document->defaultTextOption();
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  document->setDefaultTextOption(option);

  // A build is a bulk load. Undo commands and change notifications per block cost
  // seconds on a large document, so the whole build is one edit block with undo off;
  // loadMarkdown clears the stack afterwards anyway.
  const bool undoWasEnabled = document->isUndoRedoEnabled();
  document->setUndoRedoEnabled(false);

  QTextCursor cursor(document);
  cursor.beginEditBlock();
  BuildContext ctx;
  for (const typist::core::Block& block : blocks) insertBlock(block, cursor, 0, ctx);
  // insertBlock starts with a separator; remove its empty leading paragraph for
  // ordinary documents while retaining the first content block's semantic format.
  const QTextBlock first = document->begin();
  const QTextBlock next = first.next();
  if (first.text().isEmpty() && next.isValid() && !QTextCursor(next).currentTable()) {
    const QTextBlockFormat format = next.blockFormat();
    const QTextCharFormat characters = next.charFormat();
    QTextCursor start(first);
    start.deleteChar();
    start.setBlockFormat(format);
    start.setBlockCharFormat(characters);
  }
  cursor.endEditBlock();

  document->setUndoRedoEnabled(undoWasEnabled);
  if (std::any_of(blocks.begin(), blocks.end(),
                  [](const auto& block) { return block.type == core::BlockType::TableOfContents; }))
    typist::render::installDocumentObjects(document);
}

QString serializeDocument(const QTextDocument* document, int paragraphPosition,
                          const QString& paragraphSource) {
  QString out;
  QTextBlock block = document->firstBlock();

  // Nested content must line up with the text after the parent item's marker, so each
  // nesting level contributes its own marker width. Ordered numbering continues across
  // groups the document interleaves. Marker width assumes single-digit item numbers.
  std::vector<int> markerWidths;
  std::vector<int> orderedCounters;
  bool prevWasList = false;

  while (block.isValid()) {
    if (paragraphPosition >= 0 && block.position() == paragraphPosition) {
      if (!paragraphSource.isEmpty()) out += paragraphSource + QStringLiteral("\n\n");
      block = block.next();
      prevWasList = false;
      continue;
    }
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
            for (int col = 0; col < cols; ++col) {
              const int alignment =
                  table->cellAt(0, col).firstCursorPosition().blockFormat().intProperty(
                      kTableAlignmentProperty);
              out += alignment == 1   ? QStringLiteral(" :--- |")
                     : alignment == 2 ? QStringLiteral(" :---: |")
                     : alignment == 3 ? QStringLiteral(" ---: |")
                                      : QStringLiteral(" --- |");
            }
            out += QLatin1Char('\n');
          }
        }
        out += QLatin1Char('\n');
      }
      // lastPosition is the start of the block after the table, so landing on it is
      // already past the table. Advancing one more dropped every block that followed a
      // table, which collapsed long documents to a run of tables.
      QTextBlock current = block;
      while (current.isValid() && (current.position() < table->lastPosition() ||
                                   QTextCursor(current).currentTable() == table)) {
        current = current.next();
      }
      block = current;
      continue;
    }

    const QTextBlockFormat format = block.blockFormat();
    const QString special = format.property(kSpecialBlockProperty).toString();
    if (!special.isEmpty() && !block.text().isEmpty()) {
      QString source, tail;
      for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        if (fragment.charFormat().hasProperty(kSpecialSourceProperty))
          source += fragment.charFormat().property(kSpecialSourceProperty).toString();
        else if (fragment.charFormat().objectType() != typist::render::kTocObjectType)
          tail += fragment.text();
      }
      if (special == QLatin1String("toc"))
        out += QStringLiteral("[TOC]\n\n");
      else if (special == QLatin1String("math")) {
        if (source.isEmpty()) {
          source = tail.replace(QChar::LineSeparator, QLatin1Char('\n'));
          tail.clear();
        }
        out += QStringLiteral("$$\n") + source + QStringLiteral("\n$$\n\n");
      } else
        out += QStringLiteral("```mermaid\n") + source +
               (source.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n")) +
               QStringLiteral("```\n\n");
      if (!tail.isEmpty()) out += tail + QStringLiteral("\n\n");
      block = block.next();
      prevWasList = false;
      continue;
    }
    if (format.hasProperty(kFootnoteDefinitionProperty)) {
      const QString body =
          inlineFromBlock(block).replace(QStringLiteral("  \n"), QStringLiteral("\n    "));
      out += QStringLiteral("[^%1]: %2\n\n")
                 .arg(format.property(kFootnoteDefinitionProperty).toString(), body);
      block = block.next();
      prevWasList = false;
      continue;
    }
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
      const bool emptyFence = format.boolProperty(kEmptyCodeFenceProperty) &&
                              block.text().isEmpty() &&
                              block.next().blockFormat().intProperty(kCodeBlockProperty) != groupId;
      QString code;
      while (current.isValid() &&
             current.blockFormat().property(kCodeBlockProperty).toInt() == groupId) {
        if (!emptyFence) code += current.text() + QStringLiteral("\n");
        current = current.next();
      }
      const auto info = format.property(kCodeInfoProperty).toString();
      const auto fence =
          QString::fromStdString(core::codeFence(code.toStdString(), info.toStdString()));
      out += fence + info + '\n' + code + fence + QStringLiteral("\n\n");
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
        if (itemList == nullptr || static_cast<int>(itemList->format().indent()) - 1 != depth ||
            (itemList->format().style() == QTextListFormat::ListDecimal) != ordered) {
          break;
        }
        QString itemText = inlineFromBlock(current);
        const bool isTaskItem = itemFormat.property(kTaskProperty).isValid();
        if (isTaskItem &&
            (itemText.startsWith(QChar(0x2611)) || itemText.startsWith(QChar(0x2610)))) {
          // The checkbox glyph stands in for the Markdown marker and never goes back into the
          // source.
          itemText = itemText.mid(2);
        }
        if (itemText.isEmpty() && !isTaskItem) {
          // Qt keeps an empty block around when a list starts the document; it is
          // an artifact, not a list item.
          current = current.next();
          continue;
        }
        out += QString(indentSpaces, QLatin1Char(' '));
        out += ordered ? QString::number(index++) + QStringLiteral(". ") : QStringLiteral("- ");
        if (isTaskItem) {
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
      // Heading content keeps its inline runs; the plain block text would drop bold, emphasis,
      // code spans and links.
      out += QString(format.headingLevel(), QLatin1Char('#')) + QStringLiteral(" ") +
             inlineFromBlock(block);
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
  // A fragment inserts at the caret, preserving trailing paragraphs and frames.
  // Build in a detached document so paste is one undoable edit without clearing
  // the live document or moving table insertions to the end of the buffer.
  QTextDocument temporary;
  temporary.setBaseUrl(cursor.document()->baseUrl());
  temporary.setProperty("typistDark", cursor.document()->property("typistDark"));
  buildDocument(parsed.blocks, &temporary);
  static std::atomic<int> nextPasteGroup{2000000};
  QHash<int, int> codes, quotes;
  for (QTextBlock block = temporary.begin(); block.isValid(); block = block.next()) {
    QTextBlockFormat format = block.blockFormat();
    for (uint property : {kCodeBlockProperty, kQuoteProperty}) {
      if (!format.hasProperty(property)) continue;
      auto& groups = property == kCodeBlockProperty ? codes : quotes;
      const int original = format.intProperty(property);
      if (!groups.contains(original)) groups.insert(original, ++nextPasteGroup);
      format.setProperty(property, groups.value(original));
    }
    QTextCursor(block).setBlockFormat(format);
  }
  cursor.beginEditBlock();
  const bool blockContent =
      parsed.blocks.size() > 1 ||
      (!parsed.blocks.empty() && parsed.blocks.front().type != core::BlockType::Paragraph);
  if (blockContent) {
    cursor.removeSelectedText();
    if (!cursor.atBlockStart()) cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    if (!cursor.block().text().isEmpty()) {
      cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
      cursor.movePosition(QTextCursor::PreviousBlock);
    }
    if (!QTextCursor(temporary.begin()).currentTable())
      cursor.setBlockFormat(temporary.begin().blockFormat());
  }
  cursor.insertFragment(QTextDocumentFragment(&temporary));
  typist::render::installDocumentObjects(cursor.document());
  cursor.endEditBlock();
  return true;
}

void refreshSpecialContent(QTextDocument* document, bool dark) {
  document->setProperty("typistDark", dark);
  for (auto block = document->begin(); block.isValid(); block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      const auto format = fragment.charFormat();
      if (!format.isImageFormat()) continue;
      const bool inlineFormula = format.hasProperty(kMathSourceProperty);
      const bool special = format.hasProperty(kSpecialSourceProperty);
      if (!inlineFormula && !special) continue;
      const auto source =
          format.property(inlineFormula ? kMathSourceProperty : kSpecialSourceProperty).toString();
      const bool diagram = block.blockFormat().property(kSpecialBlockProperty).toString() ==
                           QLatin1String("mermaid");
      const auto result = diagram
                              ? typist::render::mermaid(source, dark)
                              : typist::render::math(source, !inlineFormula,
                                                     dark ? QColor("#dde3ec") : QColor("#333333"));
      if (!result.image.isNull())
        document->addResource(QTextDocument::ImageResource, QUrl(format.toImageFormat().name()),
                              result.image);
    }
  }
  document->markContentsDirty(0, document->characterCount());
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
  const QTextBlock block = document->findBlockByNumber(blockNumber);
  cursor.beginEditBlock();
  cursor.setBlockFormat(format);
  const QChar first = block.text().isEmpty() ? QChar() : block.text().at(0);
  if (first == QChar(0x2611) || first == QChar(0x2610)) {
    cursor.setPosition(block.position());
    cursor.setPosition(block.position() + 1, QTextCursor::KeepAnchor);
    QTextCharFormat markFormat = cursor.charFormat();
    markFormat.setProperty(kTaskMarkProperty, QVariant(!checked));
    cursor.insertText(QString(!checked ? QChar(0x2611) : QChar(0x2610)), markFormat);
  }
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
