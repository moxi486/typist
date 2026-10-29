#include "SpecialContent.h"

#include "HeadingLinks.h"
#include "SyntaxHighlighter.h"
#include "advanced_syntax.h"
#include "latex.h"
#include "markdown.h"
#include "platform/qt/graphic_qt.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QCache>
#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <memory>
#include <mutex>

static void initializeMathFonts() { Q_INIT_RESOURCE(microtex_fonts); }
namespace typist::render {
RenderResult math(const QString& source, bool display, const QColor& color) {
  if (source.size() > 8192) return {{}, QStringLiteral("Formula exceeds 8192 characters")};
  static QCache<QString, QImage> cache(16 * 1024 * 1024);
  const QString key = source + QChar(0x1f) + color.name() + (display ? "D" : "I");
  if (auto* image = cache.object(key)) return {*image, {}};
  try {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
      initializeMathFonts();
      tex::LaTeX::init();
    });
    const auto text =
        (display ? QStringLiteral("\\displaystyle ") : QStringLiteral("\\textstyle ")) + source;
    std::unique_ptr<tex::TeXRender> formula(
        tex::LaTeX::parse(text.toStdWString(), 640, display ? 22.f : 17.f, 5.f, color.rgba()));
    if (!formula || formula->getWidth() > 4096 || formula->getHeight() > 2048)
      return {{}, QStringLiteral("Formula is too large")};
    const int w = qMax(8, formula->getWidth() + 8), h = qMax(8, formula->getHeight() + 8);
    QImage image(w * 2, h * 2, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(2);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    tex::Graphics2D_qt graphics(&painter);
    formula->draw(graphics, 4, 4);
    painter.end();
    cache.insert(key, new QImage(image), static_cast<int>(image.sizeInBytes()));
    return {image, {}};
  } catch (const std::exception& error) {
    return {{}, QString::fromUtf8(error.what())};
  } catch (...) {
    return {{}, QStringLiteral("Unable to render this formula")};
  }
}
QList<Heading> headings(const QTextDocument* document) {
  QList<Heading> out;
  for (auto block = document->begin(); block.isValid(); block = block.next()) {
    const int level = block.blockFormat().headingLevel();
    if (level > 0 && !block.text().isEmpty()) out.append({level, block.text(), block.position()});
  }
  return out;
}
TocObject::TocObject(QTextDocument* document) : QObject(document) {}
QSizeF TocObject::intrinsicSize(QTextDocument* document, int, const QTextFormat&) {
  return {qBound(180., document->textWidth() > 0 ? document->textWidth() : 640., 760.),
          qMax(30, headings(document).size() * 26 + 16) * 1.};
}
void TocObject::drawObject(QPainter* painter, const QRectF& rect, QTextDocument* document, int,
                           const QTextFormat&) {
  painter->save();
  painter->setPen(QColor("#4d90ce"));
  QFont font = document->defaultFont();
  font.setPointSizeF(11);
  painter->setFont(font);
  qreal y = rect.top() + 8;
  for (const auto& heading : headings(document)) {
    painter->drawText(QRectF(rect.left() + (heading.level - 1) * 18, y,
                             rect.width() - (heading.level - 1) * 18, 26),
                      Qt::AlignVCenter,
                      QFontMetricsF(font).elidedText(heading.text, Qt::ElideRight,
                                                     rect.width() - (heading.level - 1) * 18));
    y += 26;
  }
  painter->restore();
}
void installDocumentObjects(QTextDocument* document) {
  if (!document->findChild<TocObject*>(QString(), Qt::FindDirectChildrenOnly))
    document->documentLayout()->registerHandler(kTocObjectType, new TocObject(document));
}
void invalidateToc(QTextDocument* document) {
  for (auto block = document->begin(); block.isValid(); block = block.next()) {
    for (auto it = block.begin(); !it.atEnd(); ++it) {
      const auto fragment = it.fragment();
      if (fragment.charFormat().objectType() == kTocObjectType)
        document->markContentsDirty(fragment.position(), fragment.length());
    }
  }
}
void expandTocForPrint(QTextDocument* document) {
  const auto entries = headings(document);
  QList<int> positions;
  for (auto block = document->begin(); block.isValid(); block = block.next())
    for (auto it = block.begin(); !it.atEnd(); ++it)
      if (it.fragment().charFormat().objectType() == kTocObjectType)
        positions.append(it.fragment().position());
  // In print layouts TOC entries are ordinary paragraphs so a long directory
  // can span pages instead of being clipped as one indivisible inline object.
  for (auto it = positions.crbegin(); it != positions.crend(); ++it) {
    QTextCursor cursor(document);
    cursor.setPosition(*it);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.setBlockFormat(QTextBlockFormat());
    cursor.setCharFormat(QTextCharFormat());
    for (int i = 0; i < entries.size(); ++i) {
      if (i) cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
      QTextBlockFormat block;
      block.setLeftMargin((entries[i].level - 1) * 16);
      block.setBottomMargin(5);
      block.setLineHeight(125, QTextBlockFormat::ProportionalHeight);
      cursor.setBlockFormat(block);
      QTextCharFormat text;
      text.setForeground(QColor("#4d90ce"));
      cursor.insertText(entries[i].text, text);
    }
  }
}
QString html(const QString& markdown, bool dark) {
  const QString body = QString::fromStdString(core::toHtml(markdown.toStdString()));
  static const QRegularExpression pattern(QStringLiteral(
      "<(span|div) class=\"typist-(math|diagram)\" data-source=\"([0-9a-f]+)\">[\\s\\S]*?</\\1>"));
  QString out;
  qsizetype last = 0;
  auto matches = pattern.globalMatch(body);
  while (matches.hasNext()) {
    const auto match = matches.next();
    out += body.mid(last, match.capturedStart() - last);
    const QString source =
        QString::fromStdString(core::syntaxUnhex(match.captured(3).toStdString()));
    const bool formula = match.captured(2) == QLatin1String("math");
    const auto result = formula ? math(source, match.captured(1) == QLatin1String("div"),
                                       dark ? QColor("#dde3ec") : QColor("#333333"))
                                : mermaid(source, dark);
    if (result.image.isNull())
      out += match.captured();
    else {
      QByteArray bytes;
      QBuffer buffer(&bytes);
      buffer.open(QIODevice::WriteOnly);
      result.image.save(&buffer, "PNG");
      const QString image = QStringLiteral(
                                "<img alt=\"%1\" width=\"%2\" height=\"%3\" "
                                "style=\"max-width:100%;height:auto;vertical-align:middle\" "
                                "src=\"data:image/png;base64,%4\">")
                                .arg(source.toHtmlEscaped())
                                .arg(result.image.width() / 2)
                                .arg(result.image.height() / 2)
                                .arg(QString::fromLatin1(bytes.toBase64()));
      out +=
          match.captured(1) == QLatin1String("div")
              ? QStringLiteral("<div style=\"text-align:center;margin:1em 0\">%1</div>").arg(image)
              : image;
    }
    last = match.capturedEnd();
  }
  out += body.mid(last);
  static const QRegularExpression code(
      QStringLiteral("<pre><code class=\"language-([^\"]+)\">([\\s\\S]*?)</code></pre>"));
  QString highlighted;
  last = 0;
  auto blocks = code.globalMatch(out);
  while (blocks.hasNext()) {
    const auto block = blocks.next();
    highlighted += out.mid(last, block.capturedStart() - last);
    QString literal = block.captured(2);
    literal.replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
        .replace(QStringLiteral("&#39;"), QStringLiteral("'"))
        .replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    highlighted +=
        QStringLiteral("<pre><code class=\"language-%1\">%2</code></pre>")
            .arg(block.captured(1), highlightedCodeHtml(literal, block.captured(1), dark));
    last = block.capturedEnd();
  }
  return addHeadingLinks(highlighted + out.mid(last));
}
}  // namespace typist::render
