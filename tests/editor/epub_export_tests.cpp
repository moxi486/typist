#include "EpubWriter.h"

#include <QFile>
#include <QImage>
#include <QMap>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>
#include <QXmlStreamReader>
#include <QtEndian>
#include <QtTest>

namespace {
QByteArray read(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QMap<QByteArray, QByteArray> unzip(const QByteArray& bytes) {
  QMap<QByteArray, QByteArray> out;
  qsizetype offset = 0;
  while (offset + 30 <= bytes.size() &&
         qFromLittleEndian<quint32>(bytes.constData() + offset) == 0x04034b50) {
    const auto size = qFromLittleEndian<quint32>(bytes.constData() + offset + 18);
    const auto nameSize = qFromLittleEndian<quint16>(bytes.constData() + offset + 26);
    const auto extra = qFromLittleEndian<quint16>(bytes.constData() + offset + 28);
    const auto data = offset + 30 + nameSize + extra;
    if (data + size > bytes.size()) return {};
    out.insert(bytes.mid(offset + 30, nameSize), bytes.mid(data, size));
    offset = data + size;
  }
  return out;
}
const typist::platform::EpubOptions options{
    QStringLiteral("写作 & Ideas <3>"), QStringLiteral("作者 & Author"), QStringLiteral("zh-CN")};
QString brokenReference(const QMap<QByteArray, QByteArray>& entries) {
  QMap<QString, QSet<QString>> ids;
  QMap<QString, QStringList> links;
  for (auto it = entries.begin(); it != entries.end(); ++it) {
    if (!it.key().endsWith(".xhtml")) continue;
    const auto filename = QString::fromUtf8(it.key()).mid(6);
    QXmlStreamReader reader(it.value());
    while (!reader.atEnd()) {
      reader.readNext();
      if (!reader.isStartElement()) continue;
      const auto id = reader.attributes().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        if (ids[filename].contains(id)) return "duplicate id: " + id;
        ids[filename].insert(id);
      }
      for (const auto& attr : {QStringLiteral("href"), QStringLiteral("src")}) {
        const auto target = reader.attributes().value(attr).toString();
        if (!target.isEmpty()) links[filename].append(target);
      }
    }
    if (reader.hasError()) return filename + ": " + reader.errorString();
  }
  for (auto it = links.begin(); it != links.end(); ++it)
    for (const auto& link : it.value()) {
      const QUrl target(link);
      if (!target.isRelative()) continue;
      const auto path = target.path().isEmpty() ? it.key() : target.path();
      if (!entries.contains(("OEBPS/" + path).toUtf8())) return "missing resource: " + link;
      if (!target.fragment().isEmpty() && !ids[path].contains(target.fragment(QUrl::FullyDecoded)))
        return "missing anchor: " + link;
    }
  return {};
}
void saveSpecimen(const QString& name, const QByteArray& bytes) {
  const auto output = qEnvironmentVariable("TYPIST_EPUB_OUTPUT");
  if (output.isEmpty()) return;
  QFile copy(output + '/' + name + QStringLiteral(".epub"));
  QVERIFY(copy.open(QIODevice::WriteOnly));
  QCOMPARE(copy.write(bytes), bytes.size());
}
}  // namespace
class EpubExportTests : public QObject {
  Q_OBJECT
 private slots:
  void publicationStructureAndReferences() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("book.epub"));
    QString error;
    const auto source = QStringLiteral(
        "# 中文标题\n\n[TOC]\n\n**Bold** and *italic*, ~~gone~~, `x < y`.\n\n"
        "[Jump](#中文标题) [Again](#中文标题-1) [Web](https://example.org/?a=1&b=2).[^note]\n\n"
        "- [x] Done\n  - Child\n- [ ] Next\n\n"
        "| Name | Value |\n| :--- | ---: |\n| two<br>lines | **42** |\n\n"
        "```cpp\nint value = 42; // < & >\n```\n\n> A quote\n\n---\n\n"
        "## 中文标题\n\n[^note]: A **note**.\n");
    QStringList notices;
    QVERIFY2(typist::platform::exportEpub(path, source, options, &error, {}, &notices),
             qPrintable(error));
    QVERIFY(notices.isEmpty());
    const auto bytes = read(path);
    QCOMPARE(bytes.mid(30, 8), QByteArray("mimetype"));
    QCOMPARE(bytes.mid(38, 20), QByteArray("application/epub+zip"));
    QCOMPARE(qFromLittleEndian<quint16>(bytes.constData() + 8), quint16(0));
    QCOMPARE(qFromLittleEndian<quint16>(bytes.constData() + 28), quint16(0));
    const auto entries = unzip(bytes);
    QCOMPARE(entries.size(), 6);
    for (auto it = entries.begin(); it != entries.end(); ++it)
      if (it.key().endsWith(".xml") || it.key().endsWith(".xhtml") || it.key().endsWith(".opf")) {
        QXmlStreamReader xml(it.value());
        while (!xml.atEnd()) xml.readNext();
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
      }
    const auto body = entries.value("OEBPS/content.xhtml");
    for (const auto& fragment :
         {QByteArray("<strong>Bold</strong>"), QByteArray("<em>italic</em>"),
          QByteArray("<del>gone</del>"), QByteArray("<blockquote>"), QByteArray("<thead>"),
          QByteArray("text-align:right"), QByteArray("two<br />lines"), QByteArray("<pre><code>"),
          QByteArray("epub:type=\"footnote\""), QString(QChar(0x2611)).toUtf8()})
      QVERIFY2(body.contains(fragment), fragment.constData());
    QVERIFY(body.contains("https://example.org/?a=1&amp;b=2"));
    QSet<QString> ids;
    QStringList fragments;
    QXmlStreamReader xml(body);
    while (!xml.atEnd()) {
      xml.readNext();
      if (!xml.isStartElement()) continue;
      const auto id = xml.attributes().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        QVERIFY(!ids.contains(id));
        ids.insert(id);
      }
      const auto href = xml.attributes().value(QStringLiteral("href")).toString();
      if (href.startsWith('#')) fragments.append(QUrl(href).fragment(QUrl::FullyDecoded));
    }
    for (const auto& fragment : fragments) QVERIFY2(ids.contains(fragment), qPrintable(fragment));
    QXmlStreamReader navigation(entries.value("OEBPS/nav.xhtml"));
    int links = 0;
    while (!navigation.atEnd()) {
      navigation.readNext();
      if (navigation.isStartElement() && navigation.name() == QLatin1String("a")) {
        const QUrl target(navigation.attributes().value(QStringLiteral("href")).toString());
        QCOMPARE(target.path(), QStringLiteral("content.xhtml"));
        QVERIFY(ids.contains(target.fragment(QUrl::FullyDecoded)));
        ++links;
      }
    }
    QCOMPARE(links, 2);
    const auto opf = entries.value("OEBPS/content.opf");
    QVERIFY(opf.contains(options.title.toHtmlEscaped().toUtf8()));
    QVERIFY(opf.contains(options.author.toHtmlEscaped().toUtf8()));
    QVERIFY(opf.contains("<dc:language>zh-CN</dc:language>"));
    QVERIFY(opf.contains("properties=\"nav\""));
    QVERIFY(opf.contains("<itemref idref=\"content\"/>"));
    if (!qEnvironmentVariable("TYPIST_EPUB_OUTPUT").isEmpty()) {
      QFile copy(qEnvironmentVariable("TYPIST_EPUB_OUTPUT") + QStringLiteral("/structure.epub"));
      QVERIFY(copy.open(QIODevice::WriteOnly));
      QCOMPARE(copy.write(bytes), bytes.size());
    }
  }
  void embeddedAssetsAndFallbacks() {
    QTemporaryDir directory;
    QImage pixels(60, 40, QImage::Format_ARGB32);
    pixels.fill(QColor(30, 90, 160, 100));
    QVERIFY(pixels.save(directory.filePath(QStringLiteral("图 片.png"))));
    QString error;
    QStringList notices;
    const auto path = directory.filePath(QStringLiteral("assets.epub"));
    const auto source = QStringLiteral(
        "# Assets\n\n![透明](%E5%9B%BE%20%E7%89%87.png) ![again](%E5%9B%BE%20%E7%89%87.png)\n\n"
        "$x^2$\n\n$$\nE=mc^2\n$$\n\n```mermaid\nflowchart LR\n A[写作] --> B[导出]\n```\n\n"
        "![Missing](missing.png) ![Remote](https://example.org/image.png)\n\n"
        "[File](next.md) [Broken](#absent) [Unsafe](javascript:alert)\n\n"
        "```mermaid\nunsupported graph type\n```\n");
    QVERIFY2(
        typist::platform::exportEpub(path, source, options, &error, directory.path(), &notices),
        qPrintable(error));
    const auto entries = unzip(read(path));
    int images = 0;
    for (auto it = entries.begin(); it != entries.end(); ++it)
      if (it.key().endsWith(".png")) {
        QVERIFY(!QImage::fromData(it.value()).isNull());
        ++images;
      }
    QCOMPARE(images, 4);
    const auto image = QImage::fromData(entries.value("OEBPS/images/image1.png"));
    QCOMPARE(image.size(), QSize(60, 40));
    QCOMPARE(image.pixelColor(10, 10).alpha(), 100);
    const auto body = entries.value("OEBPS/content.xhtml");
    QCOMPARE(body.count("src=\"images/image1.png\""), 2);
    QVERIFY(!body.contains("src=\"https:"));
    QVERIFY(!body.contains("href=\"javascript:"));
    QVERIFY(body.contains("unsupported graph type"));
    QVERIFY(body.contains("Missing"));
    QCOMPARE(notices.size(), 6);
    if (!qEnvironmentVariable("TYPIST_EPUB_OUTPUT").isEmpty()) {
      QFile copy(qEnvironmentVariable("TYPIST_EPUB_OUTPUT") + QStringLiteral("/assets.epub"));
      QVERIFY(copy.open(QIODevice::WriteOnly));
      const auto bytes = read(path);
      QCOMPARE(copy.write(bytes), bytes.size());
    }
  }
  void failurePreservesExistingFile() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("existing.epub"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("existing book");
    file.close();
    QString error;
    auto invalid = options;
    invalid.title = QStringLiteral(" ");
    QVERIFY(!typist::platform::exportEpub(path, QStringLiteral("Text"), invalid, &error));
    QVERIFY(!error.isEmpty());
    invalid = options;
    invalid.language = QStringLiteral("not a tag");
    QVERIFY(!typist::platform::exportEpub(path, {}, invalid, &error));
    QVERIFY(!typist::platform::exportEpub(path, QStringLiteral("<div>Raw HTML</div>\n"), options,
                                          &error));
    QVERIFY(
        !typist::platform::exportEpub(path, QStringLiteral("Text ") + QChar(1), options, &error));
    QVERIFY(!typist::platform::exportEpub(path, QString(16 * 1024 * 1024 + 1, QLatin1Char('x')),
                                          options, &error));
    QCOMPARE(read(path), QByteArray("existing book"));
    QVERIFY(!typist::platform::exportEpub(directory.filePath(QStringLiteral("absent/book.epub")),
                                          {}, options, &error));
  }
  void emptyBookAndNestedHeadings() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("empty.epub"));
    QString error;
    QVERIFY(typist::platform::exportEpub(path, {}, options, &error));
    if (!qEnvironmentVariable("TYPIST_EPUB_OUTPUT").isEmpty()) {
      QFile copy(qEnvironmentVariable("TYPIST_EPUB_OUTPUT") + QStringLiteral("/empty.epub"));
      QVERIFY(copy.open(QIODevice::WriteOnly));
      const auto bytes = read(path);
      QCOMPARE(copy.write(bytes), bytes.size());
    }
    QVERIFY(unzip(read(path)).value("OEBPS/nav.xhtml").contains("href=\"content.xhtml\""));
    QVERIFY(typist::platform::exportEpub(path, QStringLiteral("# A\n\n> # A\n\n## A\n"), options,
                                         &error));
    const auto body = unzip(read(path)).value("OEBPS/content.xhtml");
    QVERIFY(body.contains("id=\"a\""));
    QVERIFY(body.contains("id=\"a-1\""));
    QVERIFY(body.contains("id=\"_typist-heading-1\""));
    QVERIFY(typist::platform::exportEpub(path, QStringLiteral("# \n\nText.\n"), options, &error));
    QVERIFY(
        unzip(read(path)).value("OEBPS/nav.xhtml").contains(QStringLiteral("无标题章节").toUtf8()));
  }
  void installedSpecimen() {
    const auto corpus = qEnvironmentVariable("TYPIST_CORPUS_DIR");
    if (corpus.isEmpty()) QSKIP("Set TYPIST_CORPUS_DIR to validate the installed specimen.");
    const auto source = read(corpus + QStringLiteral("/epub-writing.md"));
    QVERIFY(!source.isEmpty());
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("specimen.epub"));
    QString error;
    QStringList notices;
    QVERIFY2(typist::platform::exportEpub(path, QString::fromUtf8(source), options, &error, corpus,
                                          &notices),
             qPrintable(error));
    QVERIFY(notices.isEmpty());
    const auto bytes = read(path);
    QVERIFY(unzip(bytes).contains("OEBPS/images/image1.png"));
    if (!qEnvironmentVariable("TYPIST_EPUB_OUTPUT").isEmpty()) {
      QFile copy(qEnvironmentVariable("TYPIST_EPUB_OUTPUT") + QStringLiteral("/specimen.epub"));
      QVERIFY(copy.open(QIODevice::WriteOnly));
      QCOMPARE(copy.write(bytes), bytes.size());
    }
  }
  void coverAndCrossChapterNavigation() {
    QTemporaryDir directory;
    QImage cover(800, 1200, QImage::Format_ARGB32);
    cover.fill(QColor(40, 90, 170, 180));
    const auto coverPath = directory.filePath(QStringLiteral("封 面.png"));
    QVERIFY(cover.save(coverPath));
    auto configured = options;
    configured.coverImage = coverPath;
    configured.splitChapters = true;
    const auto path = directory.filePath(QStringLiteral("chapters.epub"));
    QString error;
    QStringList notices;
    const auto source = QStringLiteral(
        "Preface [go](#重复-1).\n\n[TOC]\n\n"
        "# 重复\n\n[Next](#重复-1) and first note.[^n]\n\n![Cover "
        "again](%E5%B0%81%20%E9%9D%A2.png)\n\n"
        "## Detail\n\nA detail.\n\n> # Quoted heading\n\n"
        "# 重复\n\n[Back](#重复) and repeated note.[^n]\n\n"
        "# End\n\n[Details](#detail)\n\n[^n]: Shared note with a [link](#重复).\n");
    QVERIFY2(
        typist::platform::exportEpub(path, source, configured, &error, directory.path(), &notices),
        qPrintable(error));
    QVERIFY(notices.isEmpty());
    const auto bytes = read(path);
    const auto entries = unzip(bytes);
    const auto broken = brokenReference(entries);
    QVERIFY2(broken.isEmpty(), qPrintable(broken));
    QVERIFY(entries.contains("OEBPS/cover.xhtml"));
    QVERIFY(entries.contains("OEBPS/chapter-4.xhtml"));
    QVERIFY(!entries.contains("OEBPS/chapter-5.xhtml"));
    const auto opf = entries.value("OEBPS/content.opf");
    QCOMPARE(opf.count("properties=\"cover-image\""), 1);
    QVERIFY(opf.contains(
        "<spine><itemref idref=\"cover\"/><itemref idref=\"content\"/><itemref "
        "idref=\"content2\"/><itemref idref=\"content3\"/><itemref idref=\"content4\"/>"));
    QCOMPARE(entries.value("OEBPS/content.xhtml").count("Preface"), 1);
    QVERIFY(entries.value("OEBPS/chapter-2.xhtml")
                .contains(QStringLiteral("href=\"chapter-3.xhtml#重复-1\"").toUtf8()));
    QVERIFY(entries.value("OEBPS/chapter-3.xhtml")
                .contains(QStringLiteral("href=\"chapter-2.xhtml#重复\"").toUtf8()));
    QVERIFY(
        entries.value("OEBPS/chapter-2.xhtml").contains("href=\"chapter-4.xhtml#_typist-note-1\""));
    QVERIFY(
        entries.value("OEBPS/chapter-4.xhtml").contains("href=\"chapter-2.xhtml#_typist-ref-1\""));
    const auto nav = entries.value("OEBPS/nav.xhtml");
    QVERIFY(nav.contains("epub:type=\"landmarks\""));
    QVERIFY(nav.contains("href=\"chapter-2.xhtml#detail\""));
    int images = 0;
    for (auto it = entries.begin(); it != entries.end(); ++it)
      if (it.key().endsWith(".png")) ++images;
    QCOMPARE(images, 1);
    const auto embedded = QImage::fromData(entries.value("OEBPS/images/image1.png"));
    QCOMPARE(embedded.size(), cover.size());
    QCOMPARE(embedded.pixelColor(10, 10).alpha(), 180);
    QFile original(coverPath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(QImage::fromData(original.readAll()), cover);
    saveSpecimen(QStringLiteral("chapters-cover"), bytes);
    configured.coverImage.clear();
    QVERIFY(typist::platform::exportEpub(path, source, configured, &error, directory.path()));
    QVERIFY(!unzip(read(path)).contains("OEBPS/cover.xhtml"));
    saveSpecimen(QStringLiteral("chapters-only"), read(path));
    configured.splitChapters = false;
    configured.coverImage = coverPath;
    QVERIFY(typist::platform::exportEpub(path, source, configured, &error, directory.path()));
    QVERIFY(!unzip(read(path)).contains("OEBPS/chapter-2.xhtml"));
    QVERIFY(brokenReference(unzip(read(path))).isEmpty());
    saveSpecimen(QStringLiteral("cover-only"), read(path));
  }
  void splitBoundariesAndInvalidCoverPreserveTarget() {
    QTemporaryDir directory;
    QString error;
    auto configured = options;
    configured.splitChapters = true;
    const auto path = directory.filePath(QStringLiteral("book.epub"));
    for (const auto& source : {QStringLiteral("# One\n\n## Detail\n\nText"),
                               QStringLiteral("## Only H2\n\nText"), QString()}) {
      QVERIFY(typist::platform::exportEpub(path, source, configured, &error));
      const auto entries = unzip(read(path));
      QVERIFY(!entries.contains("OEBPS/chapter-2.xhtml"));
      QVERIFY(brokenReference(entries).isEmpty());
    }
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("existing");
    file.close();
    for (const auto& cover :
         {QStringLiteral("https://example.org/cover.png"), QStringLiteral("relative.png"),
          directory.filePath(QStringLiteral("missing.png"))}) {
      configured.coverImage = cover;
      QVERIFY(!typist::platform::exportEpub(path, QStringLiteral("# One"), configured, &error));
      QCOMPARE(read(path), QByteArray("existing"));
    }
    configured.coverImage.clear();
    QString excessive;
    for (int i = 0; i < 1001; ++i) excessive += QStringLiteral("# Heading %1\n\n").arg(i);
    QVERIFY(!typist::platform::exportEpub(path, excessive, configured, &error));
    QCOMPARE(read(path), QByteArray("existing"));
  }
  void chaptersInstalledSpecimen() {
    const auto corpus = qEnvironmentVariable("TYPIST_CORPUS_DIR");
    if (corpus.isEmpty()) QSKIP("Set TYPIST_CORPUS_DIR to check the chapter specimen.");
    const auto source = read(corpus + QStringLiteral("/epub-chapters.md"));
    QVERIFY(!source.isEmpty());
    auto configured = options;
    configured.splitChapters = true;
    QTemporaryDir directory;
    QString error;
    QStringList notices;
    const auto path = directory.filePath(QStringLiteral("example.epub"));
    QVERIFY2(typist::platform::exportEpub(path, QString::fromUtf8(source), configured, &error,
                                          corpus, &notices),
             qPrintable(error));
    QVERIFY(notices.isEmpty());
    const auto entries = unzip(read(path));
    QVERIFY(entries.contains("OEBPS/chapter-4.xhtml"));
    const auto broken = brokenReference(entries);
    QVERIFY2(broken.isEmpty(), qPrintable(broken));
    saveSpecimen(QStringLiteral("chapters-specimen"), read(path));
  }
};
QTEST_MAIN(EpubExportTests)
#include "epub_export_tests.moc"
