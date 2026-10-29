#include "HtmlImporter.h"
#include "editor_document.h"
#include "markdown.h"

#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QtTest>

class HtmlImportTests : public QObject {
  Q_OBJECT
 private slots:
  void nativeFormatting() {
    const auto imported = typist::editor::importHtml(QStringLiteral(
        "<!DOCTYPE html><html><head><title>导入 &amp; Import</title></head><body>"
        "<h1 id='intro'>Hello / 你好</h1><p><strong>Bold</strong> <em>italic</em> "
        "<del>gone</del> <code>x &lt; y</code> <a href='#intro'>Jump</a> "
        "<a href='https://example.org?a=1&amp;b=2'>Web</a></p>"
        "<blockquote><p>A quotation</p></blockquote><ul><li>Parent<ul><li>Child</li></ul></li>"
        "<li>Next</li></ul><pre>int x = 1;\n  return x &lt; 2;\n</pre>"
        "<table><tr><th>Name</th><th>Value</th></tr><tr><td>Two<br>lines</td><td>42</td></tr></"
        "table>"
        "<p>Literal *stars* and [brackets].</p><hr></body></html>"));
    QVERIFY2(imported.ok(), qPrintable(imported.error));
    QCOMPARE(imported.title, QStringLiteral("导入 & Import"));
    const auto parsed = typist::core::parseMarkdown(imported.markdown.toStdString());
    QVERIFY(parsed.ok);
    QVERIFY(imported.markdown.contains(QStringLiteral("# Hello / 你好")));
    QVERIFY(imported.markdown.contains(QStringLiteral("**Bold**")));
    QVERIFY(imported.markdown.contains(QStringLiteral("*italic*")));
    QVERIFY(imported.markdown.contains(QStringLiteral("~~gone~~")));
    QVERIFY(imported.markdown.contains(QStringLiteral("`x < y`")));
    QVERIFY(imported.markdown.contains(QStringLiteral("[Jump](#hello--你好)")));
    QVERIFY(imported.markdown.contains(QStringLiteral("[Web](https://example.org?a=1&b=2)")));
    bool code = false, table = false, quote = false, list = false;
    for (const auto& block : parsed.blocks) {
      code |= block.type == typist::core::BlockType::CodeBlock;
      table |= block.type == typist::core::BlockType::Table;
      quote |= block.type == typist::core::BlockType::BlockQuote;
      list |= block.type == typist::core::BlockType::List;
    }
    QVERIFY(code && table && quote && list);
    QTextDocument document;
    QVERIFY(typist::editor::loadMarkdown(imported.markdown, &document, nullptr));
    const auto saved = typist::editor::serializeDocument(&document);
    QVERIFY2(typist::core::astEqual(parsed.blocks,
                                    typist::core::parseMarkdown(saved.toStdString()).blocks),
             qPrintable(saved));
  }
  void resourcesAndMergedTables() {
    QTemporaryDir directory;
    QImage image(30, 20, QImage::Format_ARGB32);
    image.fill(Qt::red);
    const auto imagePath = directory.filePath(QStringLiteral("图 片.png"));
    QVERIFY(image.save(imagePath));
    int resourceRequests = 0;
    const auto previousProvider = QTextDocument::defaultResourceProvider();
    QTextDocument::setDefaultResourceProvider([&](const QUrl&) -> QVariant {
      ++resourceRequests;
      return QByteArray("must not be loaded");
    });
    const auto result = typist::editor::importHtml(
        QStringLiteral(
            "<html><head><link rel='stylesheet' href='https://example.org/style.css'></head><body>"
            "<h1 id='photo'><img src='%E5%9B%BE%20%E7%89%87.png' alt='Photo'></h1>"
            "<p><a href='#photo'>Photo link</a> <a href='next.md#part'>Next</a> "
            "<a href='javascript:alert(1)'>Unsafe</a>"
            "<img src='https://example.org/image.png' alt='Remote'>"
            "<img src='data:image/png;base64,AAAA' alt='Embedded'></p>"
            "<table><tr><th "
            "colspan='2'>Merged</th></tr><tr><td>Left</td><td>Right</td></tr></table>"
            "<script>doNotRun()</script></body></html>"),
        QUrl::fromLocalFile(directory.filePath(QStringLiteral("source.html"))));
    QTextDocument::setDefaultResourceProvider(previousProvider);
    QCOMPARE(resourceRequests, 0);
    QVERIFY2(result.ok(), qPrintable(result.error));
    QVERIFY(result.markdown.contains(QUrl::fromLocalFile(imagePath).toString(QUrl::FullyEncoded)));
    QVERIFY(result.markdown.contains(QStringLiteral("[Photo link](#photo)")));
    QVERIFY(result.markdown.contains(
        QUrl::fromLocalFile(directory.filePath(QStringLiteral("next.md"))).toString()));
    QVERIFY(!result.markdown.contains(QStringLiteral("javascript:")));
    QVERIFY(!result.markdown.contains(QStringLiteral("data:image")));
    QVERIFY(!result.markdown.contains(QStringLiteral("doNotRun")));
    QVERIFY(result.markdown.contains(QStringLiteral("Embedded")));
    QVERIFY(result.notices.size() >= 5);
    const auto clipboard = typist::editor::importHtml(QStringLiteral(
        "<p><a href='relative.md'>Relative</a><img src='relative.png' alt='Alt'></p>"));
    QVERIFY(clipboard.ok());
    QVERIFY(!clipboard.markdown.contains(QStringLiteral("](")));
    QVERIFY(clipboard.markdown.contains(QStringLiteral("Alt")));
    const auto localClipboard = typist::editor::importHtml(
        QStringLiteral("<img src=\"%1\" alt=\"Local\">")
            .arg(QUrl::fromLocalFile(imagePath).toString(QUrl::FullyEncoded)));
    QVERIFY(localClipboard.ok());
    QVERIFY(localClipboard.markdown.contains(QStringLiteral("![Local]")));
  }
  void fileEncodingAndFailures() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("source.html"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<meta charset='iso-8859-1'><p>Caf\xe9</p>");
    file.close();
    const auto imported = typist::editor::importHtmlFile(path);
    QVERIFY2(imported.ok(), qPrintable(imported.error));
    QVERIFY(imported.markdown.contains(QStringLiteral("Café")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("<meta charset='iso-8859-1'><p>Caf\xe9</p>"));
    file.close();
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<meta charset='utf-8'><p>\xff</p>");
    file.close();
    QVERIFY(!typist::editor::importHtmlFile(path).ok());
    QVERIFY(
        !typist::editor::importHtmlFile(directory.filePath(QStringLiteral("missing.html"))).ok());
    QVERIFY(!typist::editor::importHtml(QString()).ok());
    QVERIFY(!typist::editor::importHtml(QStringLiteral("<script>nothing()</script>")).ok());
    QVERIFY(!typist::editor::importHtml(QString(8 * 1024 * 1024 + 1, QLatin1Char('x'))).ok());
  }
  void codeBeforeTableFixture() {
    const auto corpus = qEnvironmentVariable("TYPIST_CORPUS_DIR");
    if (corpus.isEmpty()) QSKIP("Set TYPIST_CORPUS_DIR to check the round-trip corpus.");
    QFile file(corpus + QStringLiteral("/html_code_table.md"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto source = file.readAll().toStdString();
    const auto parsed = typist::core::parseMarkdown(source);
    QVERIFY(parsed.ok);
    QVERIFY(typist::core::astEqual(
        parsed.blocks,
        typist::core::parseMarkdown(typist::core::serializeMarkdown(parsed.blocks)).blocks));
    QTextDocument document;
    QVERIFY(typist::editor::loadMarkdown(QString::fromStdString(source), &document, nullptr));
    const auto saved = typist::editor::serializeDocument(&document);
    QVERIFY2(typist::core::astEqual(parsed.blocks,
                                    typist::core::parseMarkdown(saved.toStdString()).blocks),
             qPrintable(saved));
  }
  void installedSpecimen() {
    const auto corpus = qEnvironmentVariable("TYPIST_CORPUS_DIR");
    if (corpus.isEmpty()) QSKIP("Set TYPIST_CORPUS_DIR to check the installed HTML specimen.");
    const auto imported =
        typist::editor::importHtmlFile(corpus + QStringLiteral("/import-writing.html"));
    QVERIFY2(imported.ok(), qPrintable(imported.error));
    const auto parsed = typist::core::parseMarkdown(imported.markdown.toStdString());
    QVERIFY(parsed.ok);
    QTextDocument document;
    QVERIFY(typist::editor::loadMarkdown(imported.markdown, &document, nullptr));
    const auto saved = typist::editor::serializeDocument(&document);
    QVERIFY2(typist::core::astEqual(parsed.blocks,
                                    typist::core::parseMarkdown(saved.toStdString()).blocks),
             qPrintable(saved));
    QVERIFY(saved.contains(QStringLiteral("[跳到步骤](#steps--步骤)")));
  }
};
QTEST_MAIN(HtmlImportTests)
#include "html_import_tests.moc"
