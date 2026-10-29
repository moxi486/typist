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
};
QTEST_MAIN(EpubExportTests)
#include "epub_export_tests.moc"
