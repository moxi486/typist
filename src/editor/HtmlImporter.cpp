#include "HtmlImporter.h"

#include "HeadingLinks.h"
#include "editor_document.h"
#include "markdown.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextTable>

namespace typist::editor {
namespace {
constexpr qsizetype kMaximumHtml = 8 * 1024 * 1024;
QString message(const char* source) { return QCoreApplication::translate("HtmlImporter", source); }
class ImportDocument : public QTextDocument {
 protected:
  QVariant loadResource(int, const QUrl&) override { return QByteArray(); }
};
QString decodedAttribute(const QString& value) {
  if (!value.contains('&')) return value;
  ImportDocument text;
  auto escaped = value;
  escaped.replace('<', QStringLiteral("&lt;")).replace('>', QStringLiteral("&gt;"));
  text.setHtml(escaped);
  return text.toPlainText();
}
QString destination(const QString& source, const QUrl& base, bool image) {
  QUrl url(source);
  if (!url.isValid() || source.isEmpty()) return {};
  if (url.isRelative()) {
    if (base.isEmpty()) return {};
    url = base.resolved(url);
  }
  const auto scheme = url.scheme().toLower();
  if (scheme == QLatin1String("http") || scheme == QLatin1String("https"))
    return url.host().isEmpty() ? QString() : url.toString(QUrl::FullyEncoded);
  if (scheme == QLatin1String("file") && QFileInfo(url.toLocalFile()).isAbsolute())
    return url.toString(QUrl::FullyEncoded);
  if (!image && scheme == QLatin1String("mailto")) return url.toString(QUrl::FullyEncoded);
  return {};
}
}  // namespace

HtmlImportResult importHtml(const QString& html, const QUrl& base) {
  HtmlImportResult result;
  if (html.toUtf8().size() > kMaximumHtml) {
    result.error = message("HTML exceeds the 8 MiB limit");
    return result;
  }
  if (html.trimmed().isEmpty()) {
    result.error = message("No HTML content to import");
    return result;
  }
  // Qt < 6.11 does not recognize del; s has identical Markdown semantics.
  auto normalized = html;
  normalized.replace(QRegularExpression(QStringLiteral("<(\\s*/?\\s*)del(?=[\\s>])"),
                                        QRegularExpression::CaseInsensitiveOption),
                     QStringLiteral("<\\1s"));
  ImportDocument document;
  document.setUndoRedoEnabled(false);
  document.setBaseUrl(base);
  document.setHtml(normalized);
  result.title = document.metaInformation(QTextDocument::DocumentTitle);
  result.notices.append(
      message("HTML appearance is simplified to Markdown; review the imported content."));
  static const QRegularExpression unsupported(
      QStringLiteral(
          "<(?:script|style|iframe|object|embed|video|audio|svg|math|form|input|canvas)\\b"),
      QRegularExpression::CaseInsensitiveOption);
  if (unsupported.match(html).hasMatch())
    result.notices.append(message("Scripts, styles and embedded content are not imported."));

  render::HeadingIds headingIds;
  QMap<QString, QString> anchors;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    if (block.blockFormat().headingLevel() <= 0 ||
        block.blockFormat().intProperty(QTextFormat::BlockQuoteLevel) > 0 ||
        QTextCursor(block).currentTable() || block.textList())
      continue;
    QString title;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto span = it.fragment();
      if (span.charFormat().isImageFormat()) {
        const auto alt =
            decodedAttribute(span.charFormat().property(QTextFormat::ImageAltText).toString());
        title += alt.isEmpty() ? message("Image") : alt;
      } else
        title += span.text();
    }
    const auto id = headingIds.next(title);
    for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment)
      for (const auto& name : fragment.fragment().charFormat().anchorNames())
        if (!anchors.contains(name)) anchors[name] = id;
    if (!anchors.contains(id)) anchors[id] = id;
  }
  struct Change {
    int position, length;
    QTextCharFormat format;
    QString replacement;
    bool replace = false;
  };
  QList<Change> changes;
  bool missingImages = false, missingLinks = false, remoteImages = false, mergedCells = false;
  int codeGroup = 0;
  bool previousCode = false;
  QSet<QTextTable*> tables;
  for (auto block = document.begin(); block.isValid(); block = block.next()) {
    auto blockFormat = block.blockFormat();
    const bool code = blockFormat.nonBreakableLines();
    if (code) {
      if (!previousCode) ++codeGroup;
      blockFormat.setProperty(kCodeBlockProperty, codeGroup);
    }
    previousCode = code;
    if (blockFormat.intProperty(QTextFormat::BlockQuoteLevel) > 0)
      blockFormat.setProperty(kQuoteProperty,
                              blockFormat.intProperty(QTextFormat::BlockQuoteLevel));
    if (blockFormat.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
      blockFormat.setProperty(kRuleProperty, true);
    const auto alignment = blockFormat.alignment();
    blockFormat.setProperty(kTableAlignmentProperty, alignment.testFlag(Qt::AlignHCenter) ? 2
                                                     : alignment.testFlag(Qt::AlignRight) ? 3
                                                                                          : 0);
    QTextCursor(block).setBlockFormat(blockFormat);
    if (auto* table = QTextCursor(block).currentTable(); table && !tables.contains(table)) {
      tables.insert(table);
      for (int row = 0; row < table->rows(); ++row)
        for (int column = 0; column < table->columns(); ++column) {
          const auto cell = table->cellAt(row, column);
          mergedCells |= cell.rowSpan() > 1 || cell.columnSpan() > 1;
        }
    }
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      auto format = fragment.charFormat();
      if (blockFormat.headingLevel() > 0) format.setProperty(kPresentationBoldProperty, true);
      for (const auto& family : format.fontFamilies().toStringList())
        if (family.contains(QStringLiteral("mono"), Qt::CaseInsensitive) ||
            family.contains(QStringLiteral("courier"), Qt::CaseInsensitive)) {
          format.setFontFamilies({QStringLiteral("Consolas")});
          break;
        }
      if (format.isImageFormat()) {
        auto image = format.toImageFormat();
        const auto target = destination(decodedAttribute(image.name()), base, true);
        if (target.isEmpty()) {
          auto alt = format.property(QTextFormat::ImageAltText).toString();
          if (alt.isEmpty()) alt = message("Image");
          changes.append({fragment.position(), fragment.length(), {}, alt, true});
          missingImages = true;
        } else {
          image.setName(target);
          image.clearProperty(QTextFormat::ImageWidth);
          image.clearProperty(QTextFormat::ImageHeight);
          image.setProperty(kImageProperty, target);
          const auto alt = decodedAttribute(format.property(QTextFormat::ImageAltText).toString());
          image.setProperty(kImageAltProperty, alt.isEmpty() ? message("Image") : alt);
          changes.append({fragment.position(), fragment.length(), image, {}, false});
          remoteImages |= target.startsWith(QLatin1String("http"));
        }
      } else if (format.isAnchor() && !format.anchorHref().isEmpty()) {
        const auto original = decodedAttribute(format.anchorHref());
        const auto fragmentId = QUrl(original).fragment(QUrl::FullyDecoded);
        auto target = original.startsWith('#') && anchors.contains(fragmentId)
                          ? '#' + anchors.value(fragmentId)
                          : destination(original, base, false);
        if (target.isEmpty()) {
          format.setAnchor(false);
          format.setAnchorHref({});
          missingLinks = true;
        } else
          format.setAnchorHref(target);
        changes.append({fragment.position(), fragment.length(), format, {}, false});
      } else {
        format.setAnchor(false);
        changes.append({fragment.position(), fragment.length(), format, {}, false});
      }
    }
  }
  for (auto it = changes.crbegin(); it != changes.crend(); ++it) {
    QTextCursor cursor(&document);
    cursor.setPosition(it->position);
    cursor.setPosition(it->position + it->length, QTextCursor::KeepAnchor);
    if (it->replace)
      cursor.insertText(it->replacement, QTextCharFormat());
    else
      cursor.setCharFormat(it->format);
  }
  if (missingImages)
    result.notices.append(message("Embedded or unresolved images are kept as alternate text."));
  if (missingLinks)
    result.notices.append(message("Unresolved or unsupported links are kept as text."));
  if (remoteImages)
    result.notices.append(message("Remote images remain links and are not downloaded."));
  if (mergedCells)
    result.notices.append(
        message("Merged table cells are flattened; only the first cell keeps the content."));
  result.markdown = serializeDocument(&document);
  if (result.markdown.trimmed().isEmpty()) {
    result.error = message("No readable content was found in this HTML");
    return result;
  }
  if (result.markdown.toUtf8().size() > kMaximumHtml) {
    result.error = message("Converted Markdown exceeds the 8 MiB limit");
    return result;
  }
  const auto parsed = core::parseMarkdown(result.markdown.toStdString());
  if (!parsed.ok) result.error = QString::fromStdString(parsed.error);
  return result;
}
HtmlImportResult importHtmlFile(const QString& path) {
  HtmlImportResult result;
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    result.error = input.errorString();
    return result;
  }
  if (input.size() > kMaximumHtml) {
    result.error = message("HTML exceeds the 8 MiB limit");
    return result;
  }
  const auto bytes = input.read(kMaximumHtml + 1);
  if (input.error() != QFileDevice::NoError) {
    result.error = input.errorString();
    return result;
  }
  if (bytes.size() > kMaximumHtml) {
    result.error = message("HTML exceeds the 8 MiB limit");
    return result;
  }
  auto decoder = QStringDecoder::decoderForHtml(bytes);
  const QString html = decoder(bytes);
  if (decoder.hasError()) {
    result.error = message("The HTML text encoding could not be decoded");
    return result;
  }
  return importHtml(html, QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
}
}  // namespace typist::editor
