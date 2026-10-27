#include "PdfWriter.h"

#include <QByteArray>
#include <memory>
#include <QMarginsF>
#include <QPainter>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextDocument>
#include <QTextTable>
#include <QtGui>

namespace typist::platform {

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

constexpr double kBandHeightPx = 18.0;
constexpr double kTableHeaderGapPx = 4.0;

QPageSize paperFor(const QString& paper) {
  return paper == QStringLiteral("letter") ? QPageSize(QPageSize::Letter) : QPageSize(QPageSize::A4);
}

void drawBands(QPainter& painter, const QRectF& pageRect, int page, const PdfPageOptions& options,
               double headerBand, double footerBand) {
  if (!options.header.isEmpty()) {
    painter.drawText(QPointF(pageRect.left(), pageRect.top() + kBandHeightPx - 4.0), options.header);
  }
  if (!options.footer.isEmpty()) {
    QString line = options.footer;
    line.replace(QStringLiteral("#"), QString::number(page));
    painter.drawText(QPointF(pageRect.left(), pageRect.bottom() - 4.0), line);
  }
}

void emitPage(QPdfWriter& writer, QPainter& painter, QAbstractTextDocumentLayout* layout, const QRectF& pageRect,
              int page, double contentTop, double contentBottom, double headerBand, double footerBand,
              const PdfPageOptions& options) {
  if (page > 1) writer.newPage();
  painter.save();
  painter.translate(pageRect.left(), pageRect.top() + headerBand - contentTop);
  QAbstractTextDocumentLayout::PaintContext context;
  const double height = qMax(contentBottom - contentTop, 1.0);
  context.clip = QRectF(0.0, contentTop, pageRect.width(), qMin(height, pageRect.height() - headerBand - footerBand));
  layout->draw(&painter, context);
  painter.restore();
  drawBands(painter, pageRect, page, options, headerBand, footerBand);
}

}  // namespace

bool pdfExportAvailable() { return true; }

bool exportPdf(const QString& path, const QTextDocument* document, QString* error) {
  return exportPdf(path, document, PdfPageOptions(), error);
}

bool exportPdf(const QString& path, const QTextDocument* document, const PdfPageOptions& options, QString* error) {
  if (document == nullptr) {
    if (error != nullptr) *error = QStringLiteral("No document to print");
    return false;
  }

  // The writer lays the document out at 96dpi so document pixels match device pixels.
  QPdfWriter writer(path);
  writer.setResolution(96);
  if (!writer.setPageSize(paperFor(options.paper))) {
    if (error != nullptr) *error = QStringLiteral("Unsupported paper size");
    return false;
  }
  if (!writer.setPageMargins(QMarginsF(options.marginMm, options.marginMm, options.marginMm, options.marginMm),
                             QPageLayout::Millimeter)) {
    if (error != nullptr) *error = QStringLiteral("Margins do not fit the paper");
    return false;
  }

  const double headerBand = options.header.isEmpty() ? 0.0 : kBandHeightPx;
  const double footerBand = options.footer.isEmpty() ? 0.0 : kBandHeightPx;
  const QRectF pageRect(writer.pageLayout().paintRectPixels(writer.resolution()));
  const double contentHeight = pageRect.height() - headerBand - footerBand;
  if (contentHeight <= 0.0) {
    if (error != nullptr) *error = QStringLiteral("No room left for content after header and footer");
    return false;
  }

  // A clone keeps the caller document untouched: paging changes its layout page size.
  const std::unique_ptr<QTextDocument> copy(document->clone());
  QTextDocument& laidOut = *copy;
  laidOut.setPageSize(QSizeF(pageRect.width(), contentHeight));
  QAbstractTextDocumentLayout* layout = laidOut.documentLayout();
  // setPageSize alone does not relayout; documentSize() forces the pass that
  // gives real block heights and per-block line layouts.
  layout->documentSize();

  QPainter painter(&writer);
  painter.setFont(QFont(painter.font().family(), 9));

  int blockNumber = 0;
  int page = 1;
  while (blockNumber < laidOut.blockCount()) {
    const QTextBlock block = laidOut.findBlockByNumber(blockNumber);
    const QRectF blockRect = layout->blockBoundingRect(block);
    QTextLayout* lineLayout = block.layout();
    const bool tall = blockRect.height() > contentHeight && lineLayout != nullptr && lineLayout->lineCount() > 1;

    if (!tall) {
      const double base = blockRect.y();
      // A page that starts inside a table is a continuation: the first row is redrawn
      // at the top and the row it occupies is reserved out of the content height.
      const QRectF headerRect = options.repeatTableHeader ? tableHeaderRectFor(&laidOut, block) : QRectF();
      const double reserved = headerRect.isNull() ? 0.0 : headerRect.height() + kTableHeaderGapPx;
      const double room = contentHeight - reserved;
      int nextBlock = blockNumber;
      QTextBlock scan = block;
      while (scan.isValid()) {
        const QRectF rect = layout->blockBoundingRect(scan);
        if (nextBlock > blockNumber && (rect.y() - base) + rect.height() > room) break;
        ++nextBlock;
        scan = scan.next();
      }
      blockNumber = nextBlock;
      if (page > 1) writer.newPage();
      if (!headerRect.isNull()) {
        painter.save();
        painter.translate(pageRect.left(), pageRect.top() + headerBand - headerRect.y());
        QAbstractTextDocumentLayout::PaintContext headerContext;
        headerContext.clip = headerRect;
        layout->draw(&painter, headerContext);
        painter.restore();
      }
      painter.save();
      painter.translate(pageRect.left(), pageRect.top() + headerBand + reserved - base);
      QAbstractTextDocumentLayout::PaintContext context;
      context.clip = QRectF(0.0, base, pageRect.width(), room);
      layout->draw(&painter, context);
      painter.restore();
      drawBands(painter, pageRect, page, options, headerBand, footerBand);
      ++page;
      continue;
    }

    // A block taller than the page is split at line boundaries so no line is sliced.
    // QTextLine::rect() is block-relative, so page offsets are measured from the block top.
    double pageTop = 0.0;
    double pageBottom = 0.0;
    for (int line = 0; line < lineLayout->lineCount(); ++line) {
      const QRectF lineRect = lineLayout->lineAt(line).rect();
      if (pageBottom > 0.0 && lineRect.y() - pageTop >= contentHeight) {
        emitPage(writer, painter, layout, pageRect, page, blockRect.y() + pageTop, blockRect.y() + pageBottom,
                 headerBand, footerBand, options);
        ++page;
        pageTop = lineRect.y();
      }
      pageBottom = lineRect.y() + lineRect.height();
    }
    emitPage(writer, painter, layout, pageRect, page, blockRect.y() + pageTop, blockRect.y() + pageBottom,
             headerBand, footerBand, options);
    ++page;
    ++blockNumber;
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
