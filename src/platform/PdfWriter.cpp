#include "PdfWriter.h"

#include "DocumentProperties.h"
#include "SpecialContent.h"
#include "SyntaxHighlighter.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPair>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextTable>
#include <QVector>
#include <QXmlStreamWriter>
#include <QtGui>
#include <memory>

namespace typist::platform {

QPageSize::PageSizeId pdfPaperId(const QString& key) {
  if (key.compare(QStringLiteral("custom"), Qt::CaseInsensitive) == 0) return QPageSize::Custom;
  for (auto id : kPdfPaperPresets)
    if (key.compare(QPageSize::key(id), Qt::CaseInsensitive) == 0) return id;
  return QPageSize::A4;
}

bool validPdfCustomSize(const QSizeF& size) {
  return size.width() >= kMinPdfPaperSideMm && size.width() <= kMaxPdfPaperSideMm &&
         size.height() >= kMinPdfPaperSideMm && size.height() <= kMaxPdfPaperSideMm;
}

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

QPageSize paperFor(const PdfPageOptions& options) {
  const auto id = pdfPaperId(options.paper);
  if (id != QPageSize::Custom) return QPageSize(id);
  return validPdfCustomSize(options.customSizeMm)
             ? QPageSize(options.customSizeMm, QPageSize::Millimeter, {}, QPageSize::ExactMatch)
             : QPageSize();
}

QString documentTitle(const PdfPageOptions& options) {
  const auto title = options.metadataTitle.trimmed();
  return title.isEmpty() ? options.title : title;
}

QByteArray documentMetadata(const PdfPageOptions& options, const QString& title,
                            const QString& creator) {
  QByteArray packet;
  QXmlStreamWriter xml(&packet);
  xml.writeProcessingInstruction(QStringLiteral("xpacket"),
                                 QStringLiteral("begin='' id='W5M0MpCehiHzreSzNTczkc9d'"));
  xml.writeStartElement(QStringLiteral("x:xmpmeta"));
  xml.writeNamespace(QStringLiteral("adobe:ns:meta/"), QStringLiteral("x"));
  xml.writeStartElement(QStringLiteral("rdf:RDF"));
  xml.writeNamespace(QStringLiteral("http://www.w3.org/1999/02/22-rdf-syntax-ns#"),
                     QStringLiteral("rdf"));
  xml.writeStartElement(QStringLiteral("rdf:Description"));
  xml.writeAttribute(QStringLiteral("rdf:about"), QString());
  xml.writeNamespace(QStringLiteral("http://purl.org/dc/elements/1.1/"), QStringLiteral("dc"));
  xml.writeNamespace(QStringLiteral("http://ns.adobe.com/xap/1.0/"), QStringLiteral("xmp"));
  xml.writeNamespace(QStringLiteral("http://ns.adobe.com/pdf/1.3/"), QStringLiteral("pdf"));
  const auto writeItem = [&](const QString& field, const QString& value, bool localized) {
    if (value.isEmpty()) return;
    xml.writeStartElement(field);
    xml.writeStartElement(localized ? QStringLiteral("rdf:Alt") : QStringLiteral("rdf:Seq"));
    xml.writeStartElement(QStringLiteral("rdf:li"));
    if (localized) xml.writeAttribute(QStringLiteral("xml:lang"), QStringLiteral("x-default"));
    xml.writeCharacters(value);
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
  };
  writeItem(QStringLiteral("dc:title"), title, true);
  writeItem(QStringLiteral("dc:creator"), options.author.trimmed(), false);
  writeItem(QStringLiteral("dc:description"), options.subject.trimmed(), true);
  const auto keywords = options.keywords.trimmed();
  if (!keywords.isEmpty()) xml.writeTextElement(QStringLiteral("pdf:Keywords"), keywords);
  xml.writeTextElement(QStringLiteral("xmp:CreatorTool"), creator);
  xml.writeTextElement(QStringLiteral("pdf:Producer"), QStringLiteral("Qt %1").arg(qVersion()));
  const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  xml.writeTextElement(QStringLiteral("xmp:CreateDate"), now);
  xml.writeTextElement(QStringLiteral("xmp:ModifyDate"), now);
  xml.writeEndElement();
  xml.writeEndElement();
  xml.writeEndElement();
  xml.writeProcessingInstruction(QStringLiteral("xpacket"), QStringLiteral("end='w'"));
  return xml.hasError() ? QByteArray() : packet;
}

QHash<QString, QString> bandVariables(const PdfPageOptions& options, const QString& outputPath) {
  QHash<QString, QString> values{
      {QStringLiteral("title"), documentTitle(options)},
      {QStringLiteral("author"), options.author.trimmed()},
      {QStringLiteral("today"), QDate::currentDate().toString(Qt::ISODate)},
  };
  const QFileInfo source(options.sourcePath);
  values.insert(QStringLiteral("currentPath"),
                options.sourcePath.isEmpty() ? QString() : source.absoluteFilePath());
  values.insert(QStringLiteral("currentFileName"), source.completeBaseName());
  values.insert(QStringLiteral("currentFileFullName"), source.fileName());
  values.insert(QStringLiteral("currentFolder"),
                options.sourcePath.isEmpty() ? QString() : source.absolutePath());
  if (!outputPath.isEmpty()) {
    const QFileInfo output(outputPath);
    values.insert(QStringLiteral("outputPath"), output.absoluteFilePath());
    values.insert(QStringLiteral("outputFileName"), output.completeBaseName());
    values.insert(QStringLiteral("outputFileFullName"), output.fileName());
  }
  return values;
}

QString expandBand(const QString& pattern, const QHash<QString, QString>& variables, int page,
                   int count) {
  static const QRegularExpression variable(QStringLiteral(R"(\$\{([^{}]*)\}|\{title\}|#)"));
  QString text;
  qsizetype start = 0;
  auto matches = variable.globalMatch(pattern);
  while (matches.hasNext()) {
    const auto match = matches.next();
    text += QStringView(pattern).mid(start, match.capturedStart() - start);
    const auto name = match.capturedView(1);
    const auto token = match.capturedView();
    if (name == u"pageNo" || token == u"#")
      text += QString::number(page);
    else if (name == u"pageCount" || name == u"totalPages")
      text += QString::number(count);
    else
      text += variables.value(token == u"{title}" ? QStringLiteral("title") : name.toString(),
                              token.toString());
    start = match.capturedEnd();
  }
  text += QStringView(pattern).mid(start);
  text.replace('\n', ' ');
  return text;
}

void drawBands(QPainter& painter, const QRectF& pageRect, int page, int count,
               const PdfPageOptions& options, const QHash<QString, QString>& variables) {
  painter.save();
  painter.setClipping(false);
  painter.setPen(QColor("#667085"));
  QFont font = painter.font();
  font.setPointSizeF(9);
  painter.setFont(font);
  const auto bandText = [&](const QString& pattern) {
    return painter.fontMetrics().elidedText(expandBand(pattern, variables, page, count),
                                            Qt::ElideRight, qFloor(pageRect.width()));
  };
  if (!options.header.isEmpty())
    painter.drawText(QRectF(pageRect.left(), pageRect.top(), pageRect.width(), kBandHeightPx),
                     Qt::AlignLeft | Qt::AlignTop, bandText(options.header));
  if (!options.footer.isEmpty()) {
    painter.drawText(
        QRectF(pageRect.left(), pageRect.bottom() - kBandHeightPx, pageRect.width(), kBandHeightPx),
        Qt::AlignHCenter | Qt::AlignBottom, bandText(options.footer));
  }
  painter.restore();
}

}  // namespace

bool pdfExportAvailable() { return true; }

bool exportPdf(const QString& path, const QTextDocument* document, QString* error) {
  return exportPdf(path, document, PdfPageOptions(), error);
}

namespace {
struct PreparedPages {
  std::unique_ptr<QTextDocument> document;
  QRectF pageRect;
  double headerBand = 0, contentHeight = 0;
  QVector<QRectF> backgrounds;
};
QSize physicalPage(const QPageSize& paper, bool landscape) {
  auto size = paper.sizePixels(96);
  if (landscape) size.transpose();
  return size;
}

void applyPageBreaks(QTextDocument& document, bool breakBetweenTopHeadings) {
  bool seenHeading = false, seenContent = false, inFootnotes = false;
  bool manualBreak = false;
  QTextCursor cursor(&document);
  cursor.beginEditBlock();
  for (auto it = document.rootFrame()->begin(); !it.atEnd(); ++it) {
    if (auto* frame = it.currentFrame()) {
      if (manualBreak) {
        auto format = frame->frameFormat();
        format.setPageBreakPolicy(format.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysBefore);
        frame->setFrameFormat(format);
        manualBreak = false;
      }
      seenContent = true;
      inFootnotes = false;
      continue;
    }
    auto block = it.currentBlock();
    if (!block.isValid()) continue;
    if (render::isPageBreak(block)) {
      block.setVisible(false);
      manualBreak = seenContent;
      continue;
    }
    auto format = block.blockFormat();
    if (block.text().trimmed().isEmpty() && !format.hasProperty(render::kCodeBlockProperty))
      continue;
    const bool heading = format.headingLevel() == 1 && !block.textList() &&
                         !format.hasProperty(render::kQuoteProperty);
    const bool footnote = format.hasProperty(render::kFootnoteDefinitionProperty);
    const bool sectionBreak =
        breakBetweenTopHeadings &&
        ((heading && seenHeading) || (footnote && seenContent && !inFootnotes));
    if (manualBreak || sectionBreak) {
      cursor.setPosition(block.position());
      format.setPageBreakPolicy(format.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysBefore);
      cursor.setBlockFormat(format);
      manualBreak = false;
    }
    seenHeading = seenHeading || heading;
    seenContent = true;
    inFootnotes = footnote;
  }
  cursor.endEditBlock();
}

bool preparePages(const QTextDocument* document, const PdfPageOptions& options,
                  const QSize& pageSize, QPaintDevice* device, PreparedPages& pages,
                  QString* error) {
  const auto fail = [&](const QString& reason) {
    if (error) *error = reason;
    return false;
  };
  if (!document) return fail(QStringLiteral("No document to print"));
  if (!qIsFinite(options.marginMm) || options.marginMm < 0)
    return fail(QStringLiteral("Invalid page margins"));
  const double margin = options.marginMm * 96 / 25.4;
  const QRectF pageRect = QRectF(QPointF(), pageSize).adjusted(margin, margin, -margin, -margin);
  const double headerBand = options.header.isEmpty() ? 0.0 : kBandHeightPx;
  const double footerBand = options.footer.isEmpty() ? 0.0 : kBandHeightPx;
  const double contentHeight = pageRect.height() - headerBand - footerBand;
  if (contentHeight < 1 || pageRect.width() < 1)
    return fail(QStringLiteral("No room left for page content"));
  pages.document.reset(document->clone());
  auto* copy = pages.document.get();
  typist::render::expandTocForPrint(copy);
  typist::render::installDocumentObjects(copy);
  QTextDocument& laidOut = *copy;
  laidOut.setUndoRedoEnabled(false);
  laidOut.setDocumentMargin(0);
  QFont body = laidOut.defaultFont();
  body.setPointSizeF(11);
  laidOut.setDefaultFont(body);
  laidOut.documentLayout()->setPaintDevice(device);
  auto* syntax = typist::render::installSyntaxHighlighter(&laidOut, false);
  syntax->rehighlight();
  applyPageBreaks(laidOut, options.breakBetweenTopHeadings);
  // Qt's native paged layout splits on text lines and repeats real table headers.
  constexpr auto codeProperty = render::kCodeBlockProperty;
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
    if (!block.isVisible() ||
        block.blockFormat().pageBreakPolicy().testFlag(QTextFormat::PageBreak_AlwaysBefore))
      continue;
    const QRectF firstRect = layout->blockBoundingRect(block);
    const double pageBottom = (qFloor(firstRect.top() / contentHeight) + 1) * contentHeight;
    double bottom = firstRect.bottom();
    if (block.blockFormat().headingLevel() > 0 && block.next().isValid()) {
      QTextBlock following = block.next();
      if (!following.isVisible() ||
          following.blockFormat().pageBreakPolicy().testFlag(QTextFormat::PageBreak_AlwaysBefore))
        continue;
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
      format.setPageBreakPolicy(format.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysBefore);
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
  pages.pageRect = pageRect;
  pages.headerBand = headerBand;
  pages.contentHeight = contentHeight;
  pages.backgrounds = codeBackgrounds;
  return true;
}

void paintPage(QPainter& painter, const PreparedPages& pages, int page, int count,
               const PdfPageOptions& options, const QHash<QString, QString>& variables) {
  painter.save();
  // PaintContext::clip is a layout hint; a real painter clip also protects the bands.
  painter.setClipRect(QRectF(pages.pageRect.left(), pages.pageRect.top() + pages.headerBand,
                             pages.pageRect.width(), pages.contentHeight));
  painter.translate(pages.pageRect.left(),
                    pages.pageRect.top() + pages.headerBand - page * pages.contentHeight);
  for (const auto& background : pages.backgrounds)
    if (background.intersects(
            QRectF(0, page * pages.contentHeight, pages.pageRect.width(), pages.contentHeight)))
      painter.fillRect(background, QColor("#f3f5f8"));
  QAbstractTextDocumentLayout::PaintContext context;
  context.palette.setColor(QPalette::Text, QColor("#202632"));
  context.palette.setColor(QPalette::Base, Qt::white);
  context.clip = QRectF(0, page * pages.contentHeight, pages.pageRect.width(), pages.contentHeight);
  pages.document->documentLayout()->draw(&painter, context);
  painter.restore();
  drawBands(painter, pages.pageRect, page + 1, count, options, variables);
}
}  // namespace

QImage pdfPagePreview(const QTextDocument* document, const PdfPageOptions& options, int page,
                      int* pageCount, QString* error) {
  if (pageCount) *pageCount = 0;
  const auto paper = paperFor(options);
  if (!paper.isValid()) {
    if (error) *error = QStringLiteral("Invalid paper size");
    return {};
  }
  QImage image(physicalPage(paper, options.landscape), QImage::Format_RGB32);
  if (image.isNull()) {
    if (error) *error = QStringLiteral("Cannot allocate PDF preview");
    return {};
  }
  image.setDotsPerMeterX(3780);
  image.setDotsPerMeterY(3780);
  image.fill(Qt::white);
  PreparedPages pages;
  if (!preparePages(document, options, image.size(), &image, pages, error)) return {};
  const int count = qMax(1, pages.document->pageCount());
  if (pageCount) *pageCount = count;
  QPainter painter(&image);
  paintPage(painter, pages, qBound(0, page, count - 1), count, options, bandVariables(options, {}));
  painter.end();
  return image;
}

bool exportPdf(const QString& path, const QTextDocument* document, const PdfPageOptions& options,
               QString* error, const QString& outputPath) {
  const auto paper = paperFor(options);
  if (!paper.isValid()) {
    if (error) *error = QStringLiteral("Invalid paper size");
    return false;
  }
  const auto requestedTitle = documentTitle(options);
  const auto title = requestedTitle.isEmpty() ? options.header : requestedTitle;
  const auto creator =
      options.creator.trimmed().isEmpty() ? QStringLiteral("Typist") : options.creator.trimmed();
  QByteArray metadata;
  if (!options.metadataTitle.trimmed().isEmpty() || !options.author.trimmed().isEmpty() ||
      !options.creator.trimmed().isEmpty() || !options.subject.trimmed().isEmpty() ||
      !options.keywords.trimmed().isEmpty()) {
    metadata = documentMetadata(options, title, creator);
    if (metadata.isEmpty()) {
      if (error)
        *error =
            QCoreApplication::translate("PdfWriter", "PDF metadata contains invalid characters");
      return false;
    }
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error) *error = file.errorString();
    return false;
  }
  {
    QPdfWriter writer(&file);
    writer.setResolution(96);
    writer.setTitle(title);
    writer.setCreator(creator);
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    writer.setAuthor(options.author.trimmed());
#endif
    if (!metadata.isEmpty()) writer.setDocumentXmpMetadata(metadata);
    if (!writer.setPageLayout(
            QPageLayout(paper, options.landscape ? QPageLayout::Landscape : QPageLayout::Portrait,
                        QMarginsF(), QPageLayout::Millimeter))) {
      if (error) *error = QStringLiteral("Cannot set PDF paper size");
      return false;
    }
    PreparedPages pages;
    if (!preparePages(document, options, physicalPage(paper, options.landscape), &writer, pages,
                      error))
      return false;
    QPainter painter(&writer);
    if (!painter.isActive()) {
      if (error) *error = QStringLiteral("Cannot open PDF for writing");
      return false;
    }
    const int count = qMax(1, pages.document->pageCount());
    const auto variables = bandVariables(options, outputPath.isEmpty() ? path : outputPath);
    for (int page = 0; page < count; ++page) {
      if (page > 0 && !writer.newPage()) {
        if (error) *error = QStringLiteral("Cannot create PDF page");
        return false;
      }
      paintPage(painter, pages, page, count, options, variables);
    }
    painter.end();
  }
  if (!file.commit()) {
    if (error) *error = file.errorString();
    return false;
  }
  return true;
}

}  // namespace typist::platform
