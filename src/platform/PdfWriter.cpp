#include "PdfWriter.h"

#include <QByteArray>
#include <memory>
#include <QMarginsF>
#include <QPainter>
#include <QPageSize>
#include <QPdfWriter>
#include <QTextBlock>
#include <QTextDocument>
#include <QtGui>

namespace typist::platform {

namespace {

constexpr double kBandHeightPx = 18.0;

QPageSize paperFor(const QString& paper) {
  return paper == QStringLiteral("letter") ? QPageSize(QPageSize::Letter) : QPageSize(QPageSize::A4);
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

  QPainter painter(&writer);
  painter.setFont(QFont(painter.font().family(), 9));

  int blockNumber = 0;
  int page = 1;
  while (blockNumber < laidOut.blockCount()) {
    const QTextBlock start = laidOut.findBlockByNumber(blockNumber);
    const double base = layout->blockBoundingRect(start).y();

    // Advance while the block still fits below the page bottom; the first block of a page
    // is always drawn even if it is taller than the page.
    int nextBlock = blockNumber;
    QTextBlock block = start;
    while (block.isValid()) {
      const QRectF rect = layout->blockBoundingRect(block);
      if (nextBlock > blockNumber && (rect.y() - base) + rect.height() > contentHeight) break;
      ++nextBlock;
      block = block.next();
    }
    blockNumber = nextBlock;

    if (page > 1) writer.newPage();

    painter.save();
    painter.translate(pageRect.left(), pageRect.top() + headerBand - base);
    QAbstractTextDocumentLayout::PaintContext context;
    context.clip = QRectF(0.0, base, pageRect.width(), contentHeight);
    layout->draw(&painter, context);
    painter.restore();

    if (!options.header.isEmpty()) {
      painter.drawText(QPointF(pageRect.left(), pageRect.top() + kBandHeightPx - 4.0), options.header);
    }
    if (!options.footer.isEmpty()) {
      QString line = options.footer;
      line.replace(QStringLiteral("#"), QString::number(page));
      painter.drawText(QPointF(pageRect.left(), pageRect.bottom() - 4.0), line);
    }

    ++page;
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
