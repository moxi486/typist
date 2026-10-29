#include "EpubWriter.h"

#include "HeadingLinks.h"
#include "ImageResources.h"
#include "SpecialContent.h"
#include "SyntaxHighlighter.h"
#include "ZipArchive.h"
#include "markdown.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>
#include <algorithm>

namespace typist::platform {
namespace {
using core::Block;
using core::BlockType;
QString text(const char* source) { return QCoreApplication::translate("EpubWriter", source); }
QString xml(const QString& value) { return value.toHtmlEscaped(); }
QString string(const std::string& value) { return QString::fromStdString(value); }
struct Heading {
  QString id, title;
  int level;
};
struct Note {
  QString id, body, back;
};
struct Context {
  EpubOptions options;
  QString base;
  QVector<ZipEntry> images;
  QMap<QString, QString> imageNames;
  QMap<QString, QString> targets;
  QList<Heading> headings;
  QMap<QString, Note> notes;
  QStringList noteOrder, notices;
  QSet<QString> usedIds;
  QString error;
  int headingIndex = 0, referenceIndex = 0, quotedHeadingIndex = 0;
  qint64 imageBytes = 0;
  bool chinese() const { return options.language.startsWith(QLatin1String("zh")); }
  QString uniqueId(QString id) {
    while (usedIds.contains(id)) id += '-';
    usedIds.insert(id);
    return id;
  }
  void notice(const QString& message) {
    if (!notices.contains(message)) notices.append(message);
  }
  QString image(const QString& key, const QImage& pixels, const QString& alt, bool math = false) {
    if (pixels.isNull()) return {};
    auto name = imageNames.value(key);
    if (name.isEmpty()) {
      if (images.size() >= 10000) {
        error = text("Too many EPUB images");
        return {};
      }
      QByteArray bytes;
      QBuffer buffer(&bytes);
      buffer.open(QIODevice::WriteOnly);
      if (!pixels.save(&buffer, "PNG")) {
        error = text("Could not encode EPUB image");
        return {};
      }
      imageBytes += bytes.size();
      if (imageBytes > 128 * 1024 * 1024) {
        error = text("EPUB images exceed the 128 MiB limit");
        return {};
      }
      name = QStringLiteral("images/image%1.png").arg(images.size() + 1);
      imageNames[key] = name;
      images.append({("OEBPS/" + name).toUtf8(), bytes});
    }
    return QStringLiteral("<img src=\"%1\" alt=\"%2\"%3 />")
        .arg(xml(name), xml(alt), math ? QStringLiteral(" class=\"math\"") : QString());
  }
};
QString blocks(const std::vector<Block>& nodes, Context& context, bool root = false);
QString href(const QString& target, Context& context) {
  const QUrl url(target);
  if (target.startsWith('#')) {
    const auto fragment = url.fragment(QUrl::FullyDecoded);
    auto id = context.targets.value(fragment);
    if (id.isEmpty()) id = context.targets.value(render::headingSlug(fragment));
    if (!id.isEmpty()) return '#' + id;
  } else if ((url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")) &&
             !url.host().isEmpty())
    return url.toString(QUrl::FullyEncoded);
  else if (url.scheme() == QLatin1String("mailto"))
    return url.toString(QUrl::FullyEncoded);
  context.notice(text("Link kept as text: %1").arg(target));
  return {};
}
QString runs(const std::vector<core::InlineRun>& spans, const QString& fallback, Context& context) {
  if (spans.empty()) return xml(fallback).replace('\n', QStringLiteral("<br />"));
  QString out;
  for (const auto& run : spans) {
    const auto value = string(run.text);
    if (run.footnote) {
      auto note = context.notes.find(value);
      if (note == context.notes.end()) {
        out += xml("[^" + value + ']');
        continue;
      }
      const auto reference =
          context.uniqueId(QStringLiteral("_typist-ref-%1").arg(++context.referenceIndex));
      if (note->back.isEmpty()) note->back = reference;
      out += QStringLiteral("<a epub:type=\"noteref\" id=\"%1\" href=\"#%2\"><sup>[%3]</sup></a>")
                 .arg(reference, note->id, xml(value));
      continue;
    }
    if (run.image) {
      const auto target = string(run.url);
      const QUrl base = context.base.isEmpty() ? QUrl() : QUrl::fromLocalFile(context.base + '/');
      auto path = render::localImagePath(target, base);
      if (context.base.isEmpty() && !QFileInfo(path).isAbsolute()) path.clear();
      const auto rendered =
          path.isEmpty() ? QImage() : render::imagePreview(path, QSize(1600, 1600));
      auto embedded =
          context.image("local:" + QFileInfo(path).canonicalFilePath(), rendered, value);
      if (embedded.isEmpty()) {
        context.notice(text("Image kept as text: %1").arg(target));
        const auto label = value.isEmpty() ? target : value;
        embedded = "<span class=\"resource-fallback\">" + xml(label) + "</span>";
      }
      out += embedded;
      continue;
    }
    if (run.math) {
      auto embedded = context.image("math:" + value, render::math(value).image, value, true);
      if (embedded.isEmpty()) {
        context.notice(text("Formula kept as text: %1").arg(value));
        embedded = "<code>" + xml(value) + "</code>";
      }
      out += embedded;
      continue;
    }
    static const QRegularExpression lineBreak(QStringLiteral("^<br\\s*/?>$"),
                                              QRegularExpression::CaseInsensitiveOption);
    if (run.html && lineBreak.match(value).hasMatch()) {
      out += "<br />";
      continue;
    }
    auto body = xml(value).replace('\n', QStringLiteral("<br />"));
    if (run.code) body = "<code>" + body + "</code>";
    if (run.bold) body = "<strong>" + body + "</strong>";
    if (run.italic) body = "<em>" + body + "</em>";
    if (run.strike) body = "<del>" + body + "</del>";
    if (run.link) {
      const auto target = href(string(run.url), context);
      if (!target.isEmpty()) body = "<a href=\"" + xml(target) + "\">" + body + "</a>";
    }
    out += body;
  }
  return out;
}
QString toc(Context& context, bool navigation) {
  QString out =
      navigation ? QStringLiteral("<nav epub:type=\"toc\" id=\"toc\">") : QStringLiteral("<nav>");
  out += "<h1>" + QString(context.chinese() ? QStringLiteral("目录") : QStringLiteral("Contents")) +
         "</h1><ol>";
  if (context.headings.isEmpty())
    out += "<li><a href=\"content.xhtml\">" + xml(context.options.title) + "</a></li>";
  for (const auto& heading : context.headings)
    out += QStringLiteral("<li style=\"margin-left:%1em\"><a href=\"%2#%3\">%4</a></li>")
               .arg(heading.level - 1)
               .arg(navigation ? QStringLiteral("content.xhtml") : QString(), xml(heading.id),
                    xml(heading.title.trimmed().isEmpty()
                            ? (context.chinese() ? QStringLiteral("无标题章节")
                                                 : QStringLiteral("Untitled section"))
                            : heading.title));
  return out + "</ol></nav>";
}
QString blocks(const std::vector<Block>& nodes, Context& context, bool root) {
  QString out;
  for (const auto& block : nodes) {
    const auto value = string(block.text);
    switch (block.type) {
      case BlockType::Heading: {
        const auto id =
            root ? context.headings[context.headingIndex++].id
                 : context.uniqueId(
                       QStringLiteral("_typist-heading-%1").arg(++context.quotedHeadingIndex));
        out += QStringLiteral("<h%1 id=\"%2\">%3</h%1>")
                   .arg(block.level)
                   .arg(xml(id), runs(block.runs, value, context));
        break;
      }
      case BlockType::Paragraph:
        out += "<p>" + runs(block.runs, value, context) + "</p>";
        break;
      case BlockType::List: {
        const auto tag = block.ordered ? QStringLiteral("ol") : QStringLiteral("ul");
        out += '<' + tag + '>';
        for (const auto& item : block.children) {
          out += "<li>";
          if (item.task)
            out += item.checked ? QString(QChar(0x2611)) + ' ' : QString(QChar(0x2610)) + ' ';
          out += runs(item.runs, string(item.text), context) + blocks(item.children, context) +
                 "</li>";
        }
        out += "</" + tag + '>';
        break;
      }
      case BlockType::CodeBlock:
        out += "<pre><code>" + render::highlightedCodeHtml(value, string(block.info)) +
               "</code></pre>";
        break;
      case BlockType::BlockQuote:
        out += "<blockquote>" + blocks(block.children, context) + "</blockquote>";
        break;
      case BlockType::ThematicBreak:
        out += "<hr />";
        break;
      case BlockType::Table: {
        out += "<table>";
        for (std::size_t row = 0; row < block.children.size(); ++row) {
          out += row == 0 ? "<thead><tr>" : row == 1 ? "<tbody><tr>" : "<tr>";
          const auto& cells = block.children[row].children;
          for (std::size_t column = 0; column < cells.size(); ++column) {
            const auto tag = row == 0 ? QStringLiteral("th") : QStringLiteral("td");
            const auto alignment = column < block.alignments.size() ? block.alignments[column] : 0;
            out += '<' + tag + " style=\"text-align:" +
                   (alignment == 2   ? "center"
                    : alignment == 3 ? "right"
                                     : "left") +
                   "\">";
            out += runs(cells[column].runs, string(cells[column].text), context) + "</" + tag + '>';
          }
          out += row == 0 ? "</tr></thead>" : "</tr>";
        }
        if (block.children.size() > 1) out += "</tbody>";
        out += "</table>";
        break;
      }
      case BlockType::TableOfContents:
        out += toc(context, false);
        break;
      case BlockType::Math:
      case BlockType::Diagram: {
        const auto rendered =
            block.type == BlockType::Math ? render::math(value, true) : render::mermaid(value);
        auto image =
            context.image((block.type == BlockType::Math ? "math-block:" : "diagram:") + value,
                          rendered.image, value);
        if (image.isEmpty()) {
          context.notice(text("Block source kept as text: %1").arg(value.left(80)));
          image = "<pre><code>" + xml(value) + "</code></pre>";
        }
        out += "<div class=\"figure\">" + image + "</div>";
        break;
      }
      case BlockType::Footnote:
        break;
      case BlockType::Unsupported:
        context.error = text("Unsupported Markdown block: %1").arg(value);
        break;
      default:
        break;
    }
  }
  return out;
}
QString xhtml(const QString& title, const QString& body, const EpubOptions& options) {
  return QStringLiteral(
             "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE html>\n"
             "<html xmlns=\"http://www.w3.org/1999/xhtml\" "
             "xmlns:epub=\"http://www.idpf.org/2007/ops\" "
             "xml:lang=\"%1\" lang=\"%1\"><head><meta charset=\"utf-8\"/><title>%2</title>"
             "<link rel=\"stylesheet\" type=\"text/css\" "
             "href=\"style.css\"/></head><body>%3</body></html>")
      .arg(xml(options.language), xml(title), body);
}
bool validXml(const QString& source) {
  QXmlStreamReader reader(source);
  while (!reader.atEnd()) reader.readNext();
  return !reader.hasError();
}
struct Chapter {
  QString filename, title, body;
};
QList<Chapter> chapters(const QString& body, const Context& context) {
  QList<Chapter> result;
  qsizetype start = 0;
  QString title = context.options.title;
  if (context.options.splitChapters) {
    for (const auto& heading : context.headings) {
      if (heading.level != 1) continue;
      const auto marker = QStringLiteral("<h1 id=\"%1\">").arg(xml(heading.id));
      const auto position = body.indexOf(marker, start);
      if (position < 0) continue;
      if (position > start) result.append({{}, title, body.mid(start, position - start)});
      title = heading.title.trimmed().isEmpty() ? context.options.title : heading.title;
      start = position;
    }
  }
  result.append({{}, title, body.mid(start)});
  for (int index = 0; index < result.size(); ++index)
    result[index].filename = index == 0 ? QStringLiteral("content.xhtml")
                                        : QStringLiteral("chapter-%1.xhtml").arg(index + 1);
  return result;
}
QMap<QString, QString> locations(const QList<Chapter>& chapters) {
  QMap<QString, QString> result;
  for (const auto& chapter : chapters) {
    QXmlStreamReader reader;
    // Inline epub:type attributes are present in footnotes.
    reader.addData(QStringLiteral("<body xmlns:epub=\"http://www.idpf.org/2007/ops\">") +
                   chapter.body + QStringLiteral("</body>"));
    while (!reader.atEnd()) {
      reader.readNext();
      if (!reader.isStartElement()) continue;
      const auto id = reader.attributes().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) result[id] = chapter.filename;
    }
  }
  return result;
}
QString relocateLinks(const QString& body, const QString& current,
                      const QMap<QString, QString>& targets) {
  // Operates only on markup produced above, never on author-supplied raw HTML.
  static const QRegularExpression link(QStringLiteral("href=\"(?:content\\.xhtml)?#([^\"]*)\""));
  QString result;
  qsizetype end = 0;
  auto matches = link.globalMatch(body);
  while (matches.hasNext()) {
    const auto match = matches.next();
    result += body.mid(end, match.capturedStart() - end);
    // Heading IDs and generated note IDs contain no XML-special characters.
    const auto id = match.captured(1);
    const auto destination = targets.value(id);
    result += destination.isEmpty()
                  ? match.captured()
                  : QStringLiteral("href=\"%1#%2\"")
                        .arg(destination == current ? QString() : destination, id);
    end = match.capturedEnd();
  }
  return result + body.mid(end);
}
}  // namespace

bool exportEpub(const QString& path, const QString& markdown, const EpubOptions& options,
                QString* error, const QString& baseDirectory, QStringList* notices) {
  if (notices) notices->clear();
  const auto fail = [&](const QString& message) {
    if (error) *error = message;
    return false;
  };
  static const QRegularExpression language(QStringLiteral("^[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})*$"));
  if (options.title.trimmed().isEmpty() || !language.match(options.language).hasMatch())
    return fail(text("Enter a book title and a valid language tag"));
  if (markdown.toUtf8().size() > 16 * 1024 * 1024)
    return fail(text("EPUB source exceeds the 16 MiB limit"));
  const auto parsed = core::parseMarkdown(markdown.toStdString());
  if (!parsed.ok) return fail(string(parsed.error));
  Context context;
  context.options = options;
  context.base = baseDirectory;
  QString coverMarkup, coverResource;
  if (!options.coverImage.isEmpty()) {
    const QUrl base = baseDirectory.isEmpty() ? QUrl() : QUrl::fromLocalFile(baseDirectory + '/');
    const auto path = render::localImagePath(options.coverImage, base);
    if (path.isEmpty() || !QFileInfo(path).isAbsolute())
      return fail(text("Choose a readable local cover image"));
    const auto pixels = render::imagePreview(path, QSize(1600, 1600));
    coverMarkup =
        context.image("local:" + QFileInfo(path).canonicalFilePath(), pixels,
                      context.chinese() ? QStringLiteral("封面") : QStringLiteral("Cover"));
    if (coverMarkup.isEmpty())
      return fail(context.error.isEmpty() ? text("Choose a readable local cover image")
                                          : context.error);
    coverResource = context.imageNames.value("local:" + QFileInfo(path).canonicalFilePath());
  }
  render::HeadingIds ids;
  for (const auto& block : parsed.blocks)
    if (block.type == BlockType::Heading) {
      const auto title = string(block.text), id = ids.next(title);
      context.headings.append({id, title, block.level});
      context.usedIds.insert(id);
      context.targets[id] = id;
      if (!context.targets.contains(title)) context.targets[title] = id;
    }
  if (options.splitChapters &&
      std::count_if(context.headings.begin(), context.headings.end(),
                    [](const Heading& heading) { return heading.level == 1; }) > 1000)
    return fail(text("EPUB supports at most 1000 chapters"));
  for (const auto& block : parsed.blocks)
    if (block.type == BlockType::Footnote) {
      const auto label = string(block.info);
      if (!context.notes.contains(label)) {
        context.noteOrder.append(label);
        context.notes[label] = {
            context.uniqueId(QStringLiteral("_typist-note-%1").arg(context.notes.size() + 1)),
            string(block.text),
            {}};
      }
    }
  auto body = blocks(parsed.blocks, context, true);
  if (!context.notes.isEmpty()) {
    body += "<section epub:type=\"footnotes\"><h2>" +
            QString(context.chinese() ? QStringLiteral("注释") : QStringLiteral("Notes")) + "</h2>";
    for (const auto& label : context.noteOrder) {
      const auto noteSource = context.notes[label].body;
      const auto content = blocks(core::parseMarkdown(noteSource.toStdString()).blocks, context);
      const auto& note = context.notes[label];
      body += "<aside epub:type=\"footnote\" id=\"" + note.id + "\"><p>" + xml(label) + "</p>" +
              content;
      if (!note.back.isEmpty())
        body += "<p><a href=\"#" + note.back + "\">" + QString(QChar(0x21a9)) + "</a></p>";
      body += "</aside>";
    }
    body += "</section>";
  }
  if (!context.error.isEmpty()) return fail(context.error);
  auto sections = chapters(body, context);
  const auto targets = locations(sections);
  QString navigationBody = relocateLinks(toc(context, true), QStringLiteral("nav.xhtml"), targets);
  QString cover;
  if (!coverMarkup.isEmpty()) {
    cover = xhtml(options.title,
                  QStringLiteral("<div class=\"cover\">") + coverMarkup + QStringLiteral("</div>"),
                  options);
    navigationBody +=
        QStringLiteral(
            "<nav epub:type=\"landmarks\" hidden=\"hidden\"><h2>%1</h2><ol><li><a "
            "epub:type=\"cover\" href=\"cover.xhtml\">%1</a></li></ol></nav>")
            .arg(context.chinese() ? QStringLiteral("封面") : QStringLiteral("Cover"));
  }
  const auto navigation =
      xhtml(context.chinese() ? QStringLiteral("目录") : QStringLiteral("Contents"), navigationBody,
            options);
  const auto identifier = "urn:uuid:" + QUuid::createUuid().toString(QUuid::WithoutBraces);
  QString package =
      QStringLiteral(
          "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
          "<package xmlns=\"http://www.idpf.org/2007/opf\" version=\"3.0\" "
          "unique-identifier=\"book-id\" "
          "xml:lang=\"%1\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
          "<dc:identifier "
          "id=\"book-id\">%2</dc:identifier><dc:title>%3</dc:title><dc:language>%1</dc:language>")
          .arg(xml(options.language), identifier, xml(options.title.trimmed()));
  if (!options.author.trimmed().isEmpty())
    package += "<dc:creator>" + xml(options.author.trimmed()) + "</dc:creator>";
  package += "<meta property=\"dcterms:modified\">" +
             QDateTime::currentDateTimeUtc().toString("yyyy-MM-dd'T'HH:mm:ss'Z'") +
             "</meta></metadata><manifest>"
             "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" "
             "properties=\"nav\"/>"
             "<item id=\"style\" href=\"style.css\" media-type=\"text/css\"/>";
  for (int index = 0; index < sections.size(); ++index)
    package +=
        QStringLiteral("<item id=\"content%1\" href=\"%2\" media-type=\"application/xhtml+xml\"/>")
            .arg(index == 0 ? QString() : QString::number(index + 1), sections[index].filename);
  if (!cover.isEmpty())
    package += "<item id=\"cover\" href=\"cover.xhtml\" media-type=\"application/xhtml+xml\"/>";
  for (int index = 0; index < context.images.size(); ++index)
    package += QStringLiteral(
                   "<item id=\"image%1\" href=\"images/image%1.png\" media-type=\"image/png\"%2/>")
                   .arg(index + 1)
                   .arg(coverResource == QStringLiteral("images/image%1.png").arg(index + 1)
                            ? QStringLiteral(" properties=\"cover-image\"")
                            : QString());
  package += "</manifest><spine>";
  if (!cover.isEmpty()) package += "<itemref idref=\"cover\"/>";
  for (int index = 0; index < sections.size(); ++index)
    package += QStringLiteral("<itemref idref=\"content%1\"/>")
                   .arg(index == 0 ? QString() : QString::number(index + 1));
  package += "</spine></package>";
  if (!validXml(navigation) || !validXml(package) || (!cover.isEmpty() && !validXml(cover)))
    return fail(text("EPUB content contains invalid XML characters"));
  const QByteArray container =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?><container "
      "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\" "
      "version=\"1.0\"><rootfiles><rootfile full-path=\"OEBPS/content.opf\" "
      "media-type=\"application/oebps-package+xml\"/></rootfiles></container>";
  const QByteArray css =
      "body{font-family:serif;line-height:1.65;margin:1.5em 5%;color:#252a34;}"
      "h1,h2,h3,h4,h5,h6{line-height:1.25;break-after:avoid;}h1{font-size:1.8em;}h2{font-size:1."
      "4em;}"
      "p{margin:.8em "
      "0;}a{color:#2f6fb3;}pre,code{font-family:monospace;}pre{white-space:pre-wrap;padding:1em;"
      "background:#f6f8fa;}"
      "blockquote{border-left:.2em solid "
      "#c9cfd8;margin-left:0;padding-left:1em;}img{max-width:100%;height:auto;}"
      ".math{vertical-align:middle;max-height:3em;}.figure{text-align:center;margin:1em 0;}"
      "table{border-collapse:collapse;width:100%;}th,td{border:1px solid "
      "#a8b0bd;padding:.4em;vertical-align:top;}"
      "th{background:#f4f6f8;}aside{font-size:.9em;}nav ol{padding-left:1.5em;}"
      ".cover{text-align:center;}.cover img{max-height:90vh;width:auto;max-width:100%;}";
  QVector<ZipEntry> entries = {{"mimetype", "application/epub+zip"},
                               {"META-INF/container.xml", container},
                               {"OEBPS/content.opf", package.toUtf8()},
                               {"OEBPS/nav.xhtml", navigation.toUtf8()},
                               {"OEBPS/style.css", css}};
  for (const auto& section : sections) {
    const auto content =
        xhtml(section.title, relocateLinks(section.body, section.filename, targets), options);
    if (!validXml(content)) return fail(text("EPUB content contains invalid XML characters"));
    entries.append({("OEBPS/" + section.filename).toUtf8(), content.toUtf8()});
  }
  if (!cover.isEmpty()) entries.append({"OEBPS/cover.xhtml", cover.toUtf8()});
  entries += context.images;
  const auto archive = buildZip(entries);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(archive) != archive.size() || !file.commit())
    return fail(file.errorString());
  if (notices) *notices = context.notices;
  return true;
}
}  // namespace typist::platform
