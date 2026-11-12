#include "ImageWriter.h"

#include "DocumentProperties.h"
#include "SpecialContent.h"
#include "SyntaxHighlighter.h"

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QPalette>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QTextTable>
#include <QtMath>
#include <cmath>
#include <memory>

namespace typist::platform {
namespace {
constexpr int kPadding = 24;
constexpr int kMaxImageSide = 32767;  // Qt's raster paint engine coordinate range
constexpr qint64 kMaxImagePixels = 16 * 1024 * 1024;

struct FormatChange {
  int position;
  int length;
  QTextCharFormat format;
};

void prepareDocument(QTextDocument& document, const ImageExportOptions& options, bool dark) {
  document.setUndoRedoEnabled(false);
  document.setLayoutEnabled(false);
  render::expandTocForPrint(&document);
  const qreal originalPointSize = document.defaultFont().pointSizeF();
  QFont font = document.defaultFont();
  font.setPixelSize(options.fontPixels);
  document.setDefaultFont(font);
  document.setDocumentMargin(kPadding);
  document.setTextWidth(options.width);
  QTextCursor cursor(&document);
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    if (render::isPageBreak(block)) {
      block.setVisible(false);
      continue;
    }
    cursor.setPosition(block.position());
    auto blockFormat = block.blockFormat();
    if (blockFormat.hasProperty(render::kCodeBlockProperty)) {
      blockFormat.setBackground(QColor(dark ? "#2d333c" : "#f2f5f9"));
      cursor.setBlockFormat(blockFormat);
    }
    qreal availableWidth = options.width - 2 * kPadding;
    if (auto* table = cursor.currentTable()) {
      availableWidth = availableWidth / table->columns() - 2 * table->format().cellPadding() - 2;
    }
    availableWidth -=
        blockFormat.leftMargin() + blockFormat.rightMargin() +
        (block.textList() ? block.textList()->format().indent() : blockFormat.indent()) *
            document.indentWidth();
    QList<FormatChange> changes;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      auto format = fragment.charFormat();
      if (format.fontPointSize() > 0 && originalPointSize > 0) {
        const int size =
            qMax(1, qRound(options.fontPixels * format.fontPointSize() / originalPointSize));
        format.clearProperty(QTextFormat::FontPointSize);
        format.setProperty(QTextFormat::FontPixelSize, size);
      }
      if (format.isImageFormat()) {
        auto image = format.toImageFormat();
        const qreal width = qMax(1., availableWidth);
        if (image.width() > width) {
          image.setHeight(image.height() * width / image.width());
          image.setWidth(width);
        }
        format = image;
      }
      if (format.isAnchor()) format.setForeground(QColor(dark ? "#91c5fa" : "#2866a2"));
      if (format != fragment.charFormat())
        changes.append({fragment.position(), fragment.length(), format});
    }
    for (const auto& change : changes) {
      cursor.setPosition(change.position);
      cursor.setPosition(change.position + change.length, QTextCursor::KeepAnchor);
      cursor.setCharFormat(change.format);
    }
  }
  document.setLayoutEnabled(true);
  render::installSyntaxHighlighter(&document, false, dark)->rehighlight();
}
}  // namespace

ImageExportError exportImage(const QString& path, const QTextDocument& document,
                             const ImageExportOptions& options, bool dark) {
  if (options.width < kMinImageWidth || options.width > kMaxImageWidth ||
      options.fontPixels < kMinImageFont || options.fontPixels > kMaxImageFont ||
      options.scale < 1 || options.scale > kMaxImageScale)
    return ImageExportError::InvalidOptions;

  QImage metrics(1, 1, QImage::Format_RGB32);
  metrics.setDotsPerMeterX(3780);
  metrics.setDotsPerMeterY(3780);
  std::unique_ptr<QTextDocument> prepared(document.clone());
  prepared->documentLayout()->setPaintDevice(&metrics);
  prepareDocument(*prepared, options, dark);
  auto* layout = prepared->documentLayout();
  const QSizeF size = layout->documentSize();
  if (size.width() > options.width + 0.5) return ImageExportError::ContentTooWide;
  const qreal height = std::ceil(size.height()) * options.scale;
  const int width = options.width * options.scale;
  if (!qIsFinite(height) || height > kMaxImageSide || width * height > kMaxImagePixels)
    return ImageExportError::ImageTooLarge;
  if (height <= 0) return ImageExportError::Render;

  QImage image(width, int(height), QImage::Format_RGB32);
  if (image.isNull()) return ImageExportError::Render;
  image.setDotsPerMeterX(3780 * options.scale);
  image.setDotsPerMeterY(3780 * options.scale);
  const QColor background(dark ? "#1f2328" : "#ffffff");
  image.fill(background);
  QPainter painter(&image);
  if (!painter.isActive()) return ImageExportError::Render;
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                         QPainter::SmoothPixmapTransform);
  painter.scale(options.scale, options.scale);
  QAbstractTextDocumentLayout::PaintContext context;
  context.palette.setColor(QPalette::Base, background);
  context.palette.setColor(QPalette::Text, QColor(dark ? "#e6e6e6" : "#202632"));
  layout->draw(&painter, context);
  painter.end();

  QSaveFile target(path);
  if (!target.open(QIODevice::WriteOnly)) return ImageExportError::Write;
  QImageWriter writer(&target, "png");
  if (!writer.write(image) || !target.commit()) return ImageExportError::Write;
  return ImageExportError::None;
}

}  // namespace typist::platform
