#include "PdfWriter.h"

#include "SpecialContent.h"
#include "SyntaxHighlighter.h"

#include <QByteArray>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPair>
#include <QPdfWriter>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextTable>
#include <QVector>
#include <QtGui>
#include <memory>

namespace typist::platform {

QVector<QPair<int, int>> splitParagraphChunks(int lineCount, int linesPerPage) {
  QVector<QPair<int, int>> chunks;
  if (lineCount <= 0 || linesPerPage <= 0) return chunks;
  if (lineCount <= linesPerPage) {
    chunks.append(QPair<int, int>(0, lineCount));
    return chunks;
  }
  int start = 0;
  while (start < lineCount) {
    const int remaining = lineCount - start;
    if (remaining <= linesPerPage) {
      chunks.append(QPair<int, int>(start, lineCount));
      break;
    }
    int count = linesPerPage;
    // A single leftover line must not be left alone on the next page.
    if (remaining - count == 1) count = linesPerPage - 1;
    if (count < 1) count = 1;
    chunks.append(QPair<int, int>(start, start + count));
    start += count;
  }
  return chunks;
}

QRectF tableHeaderRectFor(QTextDocument* document, const QTextBlock& block) {
  if (document == nullptr || !block.isValid()) return QRectF();
  QTextCursor cursor(block);
  QTextTable* table = cursor.currentTable();
  if (table == nullptr || table->columns() == 0) return QRectF();
  const QTextTableCell own = table->cellAt(block.position());
  if (!own.isValid() || own.row() <= 0) return QRectF();
  QRectF rect;
  for (int column = 0; column < table->columns(); ++column) {
    const QTextBlock cell = table->cellAt(0, column).firstCursorPosition().block();
    if (!cell.isValid()) continue;
    const QRectF cellRect = document->documentLayout()->blockBoundingRect(cell);
    if (rect.isNull()) {
      rect = cellRect;
    } else {
      rect.setRight(qMax(rect.right(), cellRect.right()));
      rect.setBottom(qMax(rect.bottom(), cellRect.bottom()));
    }
  }
  return rect;
}

namespace {

constexpr double kBandHeightPx = 30.0;

QPageSize paperFor(const QString& paper) {
  return paper == QStringLiteral("letter") ? QPageSize(QPageSize::Letter)
                                           : QPageSize(QPageSize::A4);
}

void drawBands(QPainter& painter, const QRectF& pageRect, int page, const PdfPageOptions& options) {
  painter.save();
  painter.setClipping(false);
  painter.setPen(QColor("#667085"));
  QFont font = painter.font();
  font.setPointSizeF(9);
  painter.setFont(font);
  if (!options.header.isEmpty())
    painter.drawText(QRectF(pageRect.left(), pageRect.top(), pageRect.width(), kBandHeightPx),
                     Qt::AlignLeft | Qt::AlignTop, options.header);
  if (!options.footer.isEmpty()) {
    QString footer = options.footer;
    footer.replace(QStringLiteral("#"), QString::number(page));
    painter.drawText(
        QRectF(pageRect.left(), pageRect.bottom() - kBandHeightPx, pageRect.width(), kBandHeightPx),
        Qt::AlignHCenter | Qt::AlignBottom, footer);
  }
  painter.restore();
}

}  // namespace

bool pdfExportAvailable() { return true; }

bool exportPdf(const QString& path, const QTextDocument* document, QString* error) {
  return exportPdf(path, document, PdfPageOptions(), error);
}

bool exportPdf(const QString& path, const QTextDocument* document, const PdfPageOptions& options,
               QString* error) {
  if (document == nullptr) {
    if (error != nullptr) *error = QStringLiteral("No document to print");
    return false;
  }

  if (!qIsFinite(options.marginMm) || options.marginMm < 0) {
    if (error) *error = QStringLiteral("Invalid page margins");
    return false;
  }
  QPdfWriter writer(path);
  writer.setResolution(96);
  writer.setTitle(options.header);
  writer.setCreator(QStringLiteral("Typist"));
  writer.setPageSize(paperFor(options.paper));
  // Paint from the physical page origin, so margins are applied exactly once.
  writer.setPageMargins(QMarginsF(0, 0, 0, 0));
  const double margin = options.marginMm * writer.resolution() / 25.4;
  const QRectF pageRect = QRectF(writer.pageLayout().fullRectPixels(writer.resolution()))
                              .adjusted(margin, margin, -margin, -margin);
  const double headerBand = options.header.isEmpty() ? 0.0 : kBandHeightPx;
  const double footerBand = options.footer.isEmpty() ? 0.0 : kBandHeightPx;
  const double contentHeight = pageRect.height() - headerBand - footerBand;
  if (contentHeight < 1 || pageRect.width() < 1) {
    if (error) *error = QStringLiteral("No room left for page content");
    return false;
  }

  const std::unique_ptr<QTextDocument> copy(document->clone());
  typist::render::expandTocForPrint(copy.get());
  typist::render::installDocumentObjects(copy.get());
  QTextDocument& laidOut = *copy;
  laidOut.setUndoRedoEnabled(false);
  laidOut.setDocumentMargin(0);
  QFont body = laidOut.defaultFont();
  body.setPointSizeF(11);
  laidOut.setDefaultFont(body);
  laidOut.documentLayout()->setPaintDevice(&writer);
  auto* syntax = typist::render::installSyntaxHighlighter(&laidOut, false);
  syntax->rehighlight();
  // Qt's native paged layout splits on text lines and repeats real table headers.
  constexpr int codeProperty = 0x100001;
  QSet<QTextTable*> tables;
  for (QTextBlock block = laidOut.begin(); block.isValid(); block = block.next()) {
    if (QTextTable* table = QTextCursor(block).currentTable()) {
      if (tables.contains(table)) continue;
      tables.insert(table);
      QTextTableFormat format = table->format();
      format.setHeaderRowCount(options.repeatTableHeader ? 1 : 0);
      table->setFormat(format);
    }
  }
  laidOut.setPageSize(QSizeF(pageRect.width(), contentHeight));
  QAbstractTextDocumentLayout* layout = laidOut.documentLayout();
  layout->documentSize();
  // Apply keep rules in document order. A heading followed by a code fence is
  // one group; separate forced breaks on both would create nearly empty pages.
  for (QTextBlock block = laidOut.begin(); block.isValid(); block = block.next()) {
    const QRectF firstRect = layout->blockBoundingRect(block);
    const double pageBottom = (qFloor(firstRect.top() / contentHeight) + 1) * contentHeight;
    double bottom = firstRect.bottom();
    if (block.blockFormat().headingLevel() > 0 && block.next().isValid()) {
      QTextBlock following = block.next();
      const QVariant code = following.blockFormat().property(codeProperty);
      if (code.isValid()) {
        while (following.next().isValid() &&
               following.next().blockFormat().property(codeProperty) == code)
          following = following.next();
        bottom = layout->blockBoundingRect(following).bottom();
      } else {
        const QRectF nextRect = layout->blockBoundingRect(following);
        const QTextLayout* lines = following.layout();
        const bool shortParagraph =
            lines && lines->lineCount() <= 4 && !QTextCursor(following).currentTable();
        bottom =
            shortParagraph ? nextRect.bottom() : nextRect.top() + qMin(nextRect.height(), 48.0);
      }
    } else {
      if (QTextCursor(block).currentTable()) continue;
      if (block.previous().isValid() && block.previous().blockFormat().headingLevel() > 0) continue;
      const QVariant group = block.blockFormat().property(codeProperty);
      if (group.isValid()) {
        if (block.previous().blockFormat().property(codeProperty) == group) continue;
        QTextBlock last = block;
        while (last.next().isValid() && last.next().blockFormat().property(codeProperty) == group)
          last = last.next();
        bottom = layout->blockBoundingRect(last).bottom();
      } else {
        // Keep short paragraphs intact to avoid a single carried-over line.
        if (!block.layout() || block.layout()->lineCount() > 4) continue;
      }
    }
    const double height = bottom - firstRect.top();
    const double offset = firstRect.top() - qFloor(firstRect.top() / contentHeight) * contentHeight;
    if (height < contentHeight && bottom > pageBottom && offset > 1.0) {
      QTextCursor cursor(block);
      QTextBlockFormat format = block.blockFormat();
      format.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
      cursor.setBlockFormat(format);
      layout->documentSize();
    }
  }

  QVector<QRectF> codeBackgrounds;
  int codeGroup = 0;
  QRectF codeBounds;
  for (auto block = laidOut.begin(); block.isValid(); block = block.next()) {
    const int group = block.blockFormat().intProperty(codeProperty);
    if (group != codeGroup) {
      if (codeGroup) codeBackgrounds.append(codeBounds.adjusted(0, -4, 0, 4));
      codeBounds = {};
      codeGroup = group;
    }
    if (group) {
      const QRectF rect = layout->blockBoundingRect(block);
      const QRectF full(0, rect.top(), pageRect.width(), rect.height());
      codeBounds = codeBounds.isValid() ? codeBounds.united(full) : full;
    }
  }
  if (codeGroup) codeBackgrounds.append(codeBounds.adjusted(0, -4, 0, 4));
  QPainter painter(&writer);
  if (!painter.isActive()) {
    if (error) *error = QStringLiteral("Cannot open PDF for writing");
    return false;
  }
  const int pages = qMax(1, laidOut.pageCount());
  for (int page = 0; page < pages; ++page) {
    if (page > 0) writer.newPage();
    painter.save();
    // PaintContext::clip is a layout hint; a real painter clip also protects the bands.
    painter.setClipRect(
        QRectF(pageRect.left(), pageRect.top() + headerBand, pageRect.width(), contentHeight));
    painter.translate(pageRect.left(), pageRect.top() + headerBand - page * contentHeight);
    for (const auto& background : codeBackgrounds)
      if (background.intersects(QRectF(0, page * contentHeight, pageRect.width(), contentHeight)))
        painter.fillRect(background, QColor("#f3f5f8"));
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, QColor("#202632"));
    context.palette.setColor(QPalette::Base, Qt::white);
    context.clip = QRectF(0, page * contentHeight, pageRect.width(), contentHeight);
    layout->draw(&painter, context);
    painter.restore();
    drawBands(painter, pageRect, page + 1, options);
  }

  painter.end();

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) *error = QStringLiteral("PDF was not written");
    return false;
  }
  const QByteArray head = file.read(5);
  file.close();
  if (head != QByteArrayLiteral("%PDF-")) {
    if (error != nullptr) *error = QStringLiteral("PDF output is not a PDF file");
    QFile::remove(path);
    return false;
  }
  return true;
}

}  // namespace typist::platform
