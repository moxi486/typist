#include "DocxWriter.h"
#include "Editor.h"
#include "EpubWriter.h"
#include "HeadingLinks.h"
#include "ImageWriter.h"
#include "PdfWriter.h"
#include "SpecialContent.h"
#include "ViewSwitch.h"
#include "markdown.h"

#include <QDialog>
#include <QDir>
#include <QFile>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextDocumentFragment>
#include <QXmlStreamReader>
#include <QtEndian>
#include <QtTest>

using namespace typist;
namespace {
QString specimen() {
  return QStringLiteral(
      "# Writing with ideas / 用公式表达想法\n\n[TOC]\n\n"
      "## Mathematics / 数学\n\nInline $E=mc^2$ and $\\alpha_i + \\frac{1}{2}$ stay "
      "editable.[^note]\n\n"
      "$$\n\\int_0^1 x^2\\,dx = \\frac{1}{3}\n$$\n\n"
      "## A clear process / 清晰的流程\n\n"
      "```mermaid\nflowchart LR\n A[写下想法] --> B{准备好?}\n B -->|是| C[继续写作]\n B -->|否| "
      "A\n```\n\n"
      "## A conversation / 交互时序\n\n"
      "```mermaid\nsequenceDiagram\n participant A as Author / 作者\n participant B as Typist\n "
      "autonumber\n A->>B: Write Markdown\n B-->>A: Render the document\n Note over A,B: Keep the "
      "original source\n```\n\n"
      "[^note]: Footnotes preserve the original text.\n    中文注释也可以跨行。\n\n");
}
bool same(const QString& a, const QString& b) {
  const auto left = core::parseMarkdown(a.toStdString()),
             right = core::parseMarkdown(b.toStdString());
  return left.ok && right.ok && core::astEqual(left.blocks, right.blocks);
}
QMap<QByteArray, QByteArray> unzip(const QByteArray& bytes) {
  QMap<QByteArray, QByteArray> out;
  qsizetype offset = 0;
  while (offset + 30 <= bytes.size() &&
         qFromLittleEndian<quint32>(bytes.constData() + offset) == 0x04034b50) {
    const auto size = qFromLittleEndian<quint32>(bytes.constData() + offset + 18);
    const auto nameSize = qFromLittleEndian<quint16>(bytes.constData() + offset + 26),
               extra = qFromLittleEndian<quint16>(bytes.constData() + offset + 28);
    const auto data = offset + 30 + nameSize + extra;
    if (data + size > bytes.size()) return {};
    out.insert(bytes.mid(offset + 30, nameSize), bytes.mid(data, size));
    offset = data + size;
  }
  return out;
}
}  // namespace
class AdvancedContentTests : public QObject {
  Q_OBJECT
 private slots:
  void htmlCommentsAreOmittedFromExports() {
    const auto source = QStringLiteral(
        "<!-- Draft block\nsecond line -->\n\n"
        "<!-- Draft on the last line --> Visible after the comment\n\n"
        "Visible <!-- Draft inline --> text and \x60<!-- literal -->\x60.");
    const auto parsed = core::parseMarkdown(source.toStdString());
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.blocks.front().type, core::BlockType::HtmlComment);
    QTextDocument saved;
    QVERIFY(editor::loadMarkdown(source, &saved, nullptr));
    QVERIFY(same(source, editor::serializeDocument(&saved)));
    QVERIFY(saved.toPlainText().contains(QStringLiteral("Draft block")));

    QTextDocument printable;
    QVERIFY(editor::loadMarkdown(source, &printable, nullptr, core::HtmlComments::Omit));
    const auto visible = printable.toPlainText();
    QVERIFY(!visible.contains(QStringLiteral("Draft")));
    QVERIFY(visible.contains(QStringLiteral("Visible after the comment")));
    QVERIFY(visible.contains(QStringLiteral("<!-- literal -->")));
    for (const auto& html :
         {core::toHtml(source.toStdString()), core::toSemanticHtml(source.toStdString())}) {
      QVERIFY(html.find("Draft") == std::string::npos);
      QVERIFY(html.find("Visible after the comment") != std::string::npos);
      QVERIFY(html.find("&lt;!-- literal --&gt;") != std::string::npos);
    }

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto docxPath = directory.filePath(QStringLiteral("notes.docx"));
    QString error;
    QVERIFY2(platform::exportDocx(docxPath, source, &error), qPrintable(error));
    QFile docx(docxPath);
    QVERIFY(docx.open(QIODevice::ReadOnly));
    const auto document = unzip(docx.readAll()).value("word/document.xml");
    QVERIFY(!document.contains("Draft") && document.contains("literal"));

    const auto epubPath = directory.filePath(QStringLiteral("notes.epub"));
    platform::EpubOptions options;
    options.title = QStringLiteral("Notes");
    QVERIFY2(platform::exportEpub(epubPath, source, options, &error), qPrintable(error));
    QFile epub(epubPath);
    QVERIFY(epub.open(QIODevice::ReadOnly));
    const auto entries = unzip(epub.readAll());
    bool hasLiteral = false;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
      if (!it.key().endsWith(".xhtml")) continue;
      QVERIFY(!it.value().contains("Draft"));
      hasLiteral |= it.value().contains("literal");
    }
    QVERIFY(hasLiteral);
  }

  void syntaxAndDocumentRoundTrip() {
    const QString source = specimen();
    const auto parsed = core::parseMarkdown(source.toStdString());
    QVERIFY(parsed.ok);
    QVERIFY(same(source, QString::fromStdString(core::serializeMarkdown(parsed.blocks))));
    QTextDocument document;
    QVERIFY(editor::loadMarkdown(source, &document, nullptr));
    const QString actual = editor::serializeDocument(&document);
    QVERIFY2(same(source, actual), qPrintable(actual));
    int imageCount = 0;
    for (auto block = document.begin(); block.isValid(); block = block.next())
      for (auto it = block.begin(); !it.atEnd(); ++it)
        if (it.fragment().charFormat().isImageFormat()) {
          ++imageCount;
          const auto format = it.fragment().charFormat().toImageFormat();
          QVERIFY(!document.resource(QTextDocument::ImageResource, QUrl(format.name()))
                       .value<QImage>()
                       .isNull());
        }
    QCOMPARE(imageCount, 5);
  }
  void codeAndCurrencyStayLiteral() {
    const QString source = QStringLiteral(
        "Literal \\$5, $5 and $10, ` $x$ [^a] ` and \\[\\^a].\n\n```markdown\n$x$ "
        "[^a]\n$$\n```\n\n    $x$ [^a]\n");
    const auto parsed = core::parseMarkdown(source.toStdString());
    QVERIFY(parsed.ok);
    for (const auto& block : parsed.blocks)
      for (const auto& run : block.runs) {
        QVERIFY(!run.math);
        QVERIFY(!run.footnote);
      }
    QCOMPARE(parsed.blocks[1].type, core::BlockType::CodeBlock);
    QCOMPARE(parsed.blocks[2].type, core::BlockType::CodeBlock);
    const QString math = QStringLiteral("TeX $\\left\\{x_1, x_2\\right\\}$ and $a < b$.");
    QVERIFY(same(math, QString::fromStdString(core::serializeMarkdown(
                           core::parseMarkdown(math.toStdString()).blocks))));
    const QString note =
        QStringLiteral("[^only]: Unreferenced **note**.\n\n    A second paragraph.\n");
    QTextDocument document;
    QVERIFY(editor::loadMarkdown(note, &document, nullptr));
    QVERIFY2(same(note, editor::serializeDocument(&document)),
             qPrintable(editor::serializeDocument(&document)));
    const QString quotedCode = QStringLiteral(
        "> ```markdown\n> $x$ [^literal]\n> ```\n\nA multiline `code\n$x$ [^literal]` span.\n");
    const auto quoted = core::parseMarkdown(quotedCode.toStdString());
    QVERIFY(quoted.ok);
    QVERIFY(quoted.blocks.front().children.front().text.find("$x$ [^literal]") !=
            std::string::npos);
    QVERIFY(editor::loadMarkdown(quotedCode, &document, nullptr));
    QVERIFY2(same(quotedCode, editor::serializeDocument(&document)),
             qPrintable(editor::serializeDocument(&document)));
  }
  void nativeMathRendersOffline() {
    for (const auto& source :
         {QStringLiteral("E=mc^2"), QStringLiteral("\\frac{-b\\pm\\sqrt{b^2-4ac}}{2a}"),
          QStringLiteral("\\sum_{i=1}^n i=\\frac{n(n+1)}{2}"),
          QStringLiteral("\\begin{pmatrix}a&b\\\\c&d\\end{pmatrix}")}) {
      const auto result = render::math(source, true);
      QVERIFY2(!result.image.isNull(), qPrintable(result.error));
      int pixels = 0;
      for (int y = 0; y < result.image.height(); ++y)
        for (int x = 0; x < result.image.width(); ++x)
          pixels += qAlpha(result.image.pixel(x, y)) > 0;
      QVERIFY(pixels > 80);
    }
    QVERIFY(render::math(QString(9000, 'x')).image.isNull());
  }
  void diagramsAndFallbackPreserveSource() {
    for (const auto& source :
         {QStringLiteral(
              "flowchart TD\n A[Start] --> B{Ready?}\n B -->|Yes| C[Done]\n B -->|No| A"),
          QStringLiteral("flowchart LR\n A-->B-->C"),
          QStringLiteral("sequenceDiagram\n participant A as Author\n A->>B: Write\n B-->>A: "
                         "Render\n Note over A,B: Offline")}) {
      const auto result = render::mermaid(source);
      QVERIFY2(!result.image.isNull(), qPrintable(result.error));
      QVERIFY(result.image.width() <= 1640);
      QVERIFY(result.image.height() <= 2800);
      QVERIFY(!render::mermaid(source, true).image.isNull());
    }
    const QString unsupported = QStringLiteral(
        "```mermaid\ngantt\n title Plan\n section Work\n Writing: 2026-10-28, 1d\n```\n");
    QTextDocument document;
    QVERIFY(editor::loadMarkdown(unsupported, &document, nullptr));
    QVERIFY(same(unsupported, editor::serializeDocument(&document)));
  }
  void pieChartsRenderProportionalSlicesAndLegends() {
    const auto data = QStringLiteral(
        "\"起草: 初稿\" : 45\n\"核对\" : 22.5; %% review\n\"修订\" : 22.5\n\"其他\" : 0\n");
    const auto light = render::mermaid(QStringLiteral("pie\n") + data);
    const auto dark = render::mermaid(QStringLiteral("pie\n") + data, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.devicePixelRatio(), 2.);
    const auto sample = [&](int x, int y) { return light.image.pixelColor(x * 2, y * 2); };
    const auto rightSlice = sample(201, 75);
    QCOMPARE(rightSlice, sample(201, 205));
    QVERIFY(rightSlice != sample(62, 205) && rightSlice != sample(62, 75));
    QVERIFY(sample(62, 205) != sample(62, 75));
    QVERIFY(rightSlice != sample(5, 5));
    QVERIFY(light.image.pixelColor(10, 10) != dark.image.pixelColor(10, 10));

    const auto titled = render::mermaid(QStringLiteral("%% Workday\r\npie showData\r\n"
                                                       "title 写作时间（分钟）\r\n") +
                                        data);
    const auto sameTitle =
        render::mermaid(QStringLiteral("pie showData title 写作时间（分钟）\n") + data);
    const auto noValues = render::mermaid(QStringLiteral("pie title 写作时间（分钟）\n") + data);
    QVERIFY2(!titled.image.isNull(), qPrintable(titled.error));
    QCOMPARE(titled.image, sameTitle.image);
    QVERIFY(titled.image.height() > light.image.height());
    QVERIFY(!noValues.image.isNull() && noValues.image != titled.image);
    const auto duplicate =
        render::mermaid(QStringLiteral("pie\n") + data + QStringLiteral("\"修订\" : 99\n"));
    QCOMPARE(duplicate.image, light.image);
    const auto longLabel =
        render::mermaid(QStringLiteral("pie\n\"") + QStringLiteral("长标签").repeated(450) +
                        QStringLiteral("\" : 1\n"));
    QVERIFY(!longLabel.image.isNull() && longLabel.image.height() <= 1440);
    for (const auto& source :
         {QStringLiteral("pie\n\"zero\" : 0"), QStringLiteral("pie\n\"negative\" : -1"),
          QStringLiteral("pie\n\"broken\" : 2.3.4"), QStringLiteral("pie\n\"missing\" 2"),
          QStringLiteral("pie showData")}) {
      const auto invalid = render::mermaid(source);
      QVERIFY(invalid.image.isNull() && !invalid.error.isEmpty());
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(titled.image.save(retained + "/pie-light.png"));
      QVERIFY(render::mermaid(QStringLiteral("pie showData title 写作时间（分钟）\n") + data, true)
                  .image.save(retained + "/pie-dark.png"));
    }
  }

  void timelineRespectsSectionsAndWrapsPeriods() {
    const auto source = QStringLiteral(
        "%% Shared milestones\r\ntimeline\r\n"
        "  title 文档发布计划\r\n  section 准备\r\n"
        "      09:00 : 收集材料 : 完成初稿\r\n"
        "      核对 : 核对术语\r\n           : 审阅示例\r\n"
        "  section 发布\r\n      2026-11-19 : 发布新版 <br> 下载包 %% finished\r\n");
    const auto equivalent = QStringLiteral(
        "timeline\n  title 文档发布计划\n  section 准备\n"
        "      09:00 : 收集材料\n            : 完成初稿\n"
        "      核对 : 核对术语 : 审阅示例\n"
        "  section 发布\n      2026-11-19 : 发布新版 <br /> 下载包\n");
    const auto light = render::mermaid(source);
    const auto dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.devicePixelRatio(), 2.);
    QCOMPARE(light.image, render::mermaid(equivalent).image);
    QCOMPARE(dark.image.size(), light.image.size());
    QVERIFY(dark.image.pixelColor(0, 0) != light.image.pixelColor(0, 0));
    const auto firstOrder = render::mermaid(QStringLiteral("timeline\nDay : Draft : Publish"));
    const auto secondOrder = render::mermaid(QStringLiteral("timeline\nDay : Publish : Draft"));
    QVERIFY(!firstOrder.image.isNull() && !secondOrder.image.isNull());
    QVERIFY(firstOrder.image != secondOrder.image);

    const auto row = QStringLiteral("timeline\nA : one\nB : two\nC : three\nD : four\n");
    const auto before = render::mermaid(row);
    const auto after = render::mermaid(row + QStringLiteral("E : five\n"));
    QVERIFY(!before.image.isNull() && !after.image.isNull());
    QCOMPARE(after.image.width(), before.image.width());
    QVERIFY(after.image.height() > before.image.height());
    const auto longEvent =
        render::mermaid(QStringLiteral("timeline\nA : ") + QStringLiteral("描述").repeated(1000));
    QVERIFY(!longEvent.image.isNull() && longEvent.image.height() <= 1440);
    auto full = QStringLiteral("timeline\n");
    for (auto period = 1; period <= 24; ++period)
      full += QStringLiteral("%1 : Event\n").arg(period);
    full += QStringLiteral(": Event\n").repeated(40);
    QVERIFY(!render::mermaid(full).image.isNull());
    QVERIFY(render::mermaid(full + QStringLiteral("Another period\n")).image.isNull());
    QVERIFY(render::mermaid(full + QStringLiteral(": Another event\n")).image.isNull());
    for (const auto& invalid :
         {QStringLiteral("timeline\n: first"), QStringLiteral("timeline\nA :"),
          QStringLiteral("timeline\nsection First\nA\nsection Second\n: misplaced"),
          QStringLiteral("timeline\ntitle Missing periods"),
          QStringLiteral("timeline\naccTitle: Milestones\n2026 : Release")}) {
      const auto result = render::mermaid(invalid);
      QVERIFY(result.image.isNull() && !result.error.isEmpty());
    }

    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/timeline-light.png"));
      QVERIFY(dark.image.save(retained + "/timeline-dark.png"));
    }
  }

  void nativeDiagramsKeepSourceAndEmbedPicturesInExports_data() {
    QTest::addColumn<QString>("diagram");
    QTest::addColumn<QString>("invalid");
    QTest::newRow("pie") << QStringLiteral(
                                "pie showData title Writing time\n  \"Draft\" : 45\n"
                                "  \"Review\" : 22.5\n  \"Revise\" : 22.5\n")
                         << QStringLiteral("pie\n  \"Bad\" : -1\n");
    QTest::newRow("timeline") << QStringLiteral(
                                     "timeline\n  title Release\n  section Preparation\n"
                                     "  Monday : Draft\n         : Review<br>examples\n"
                                     "  section Distribution\n  Tuesday : Publish\n")
                              << QStringLiteral("timeline\n  : No period\n");
  }

  void nativeDiagramsKeepSourceAndEmbedPicturesInExports() {
    QFETCH(QString, diagram);
    QFETCH(QString, invalid);
    const auto fence = [](const QString& text) {
      return QStringLiteral("```mermaid\n") + text + QStringLiteral("```\n");
    };
    const auto malformed = fence(invalid);
    const auto source = fence(diagram);
    QTextDocument document;
    QVERIFY(editor::loadMarkdown(malformed, &document, nullptr));
    QVERIFY(same(malformed, editor::serializeDocument(&document)));
    QVERIFY(!document.toPlainText().contains(QChar::ObjectReplacementCharacter));
    QVERIFY(editor::loadMarkdown(source, &document, nullptr));
    const auto serialized = editor::serializeDocument(&document);
    QVERIFY2(same(source, serialized), qPrintable(serialized));
    QVERIFY(serialized.contains(diagram));
    bool found = false;
    for (auto block = document.begin(); block.isValid(); block = block.next())
      for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto format = it.fragment().charFormat();
        if (!format.isImageFormat()) continue;
        QCOMPARE(format.property(editor::kSpecialSourceProperty).toString(), diagram);
        const auto image =
            document.resource(QTextDocument::ImageResource, QUrl(format.toImageFormat().name()))
                .value<QImage>();
        QVERIFY(!image.isNull());
        found = true;
      }
    QVERIFY(found);
    QVERIFY(render::html(source).contains(QStringLiteral("data:image/png;base64,")));

    QTemporaryDir out;
    QVERIFY(out.isValid());
    QString error;
    QFile pdf(out.filePath(QStringLiteral("chart.pdf")));
    QVERIFY2(platform::exportPdf(pdf.fileName(), &document, &error), qPrintable(error));
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.readAll().contains("/Subtype /Image"));
    QFile docx(out.filePath(QStringLiteral("chart.docx")));
    QVERIFY2(platform::exportDocx(docx.fileName(), source, &error), qPrintable(error));
    QVERIFY(docx.open(QIODevice::ReadOnly));
    const auto word = unzip(docx.readAll());
    QVERIFY(word.value("word/document.xml").contains("w:drawing"));
    QVERIFY(!QImage::fromData(word.value("word/media/image1.png")).isNull());
    QFile epub(out.filePath(QStringLiteral("chart.epub")));
    platform::EpubOptions options;
    options.title = QStringLiteral("Diagram export");
    QVERIFY2(platform::exportEpub(epub.fileName(), source, options, &error), qPrintable(error));
    QVERIFY(epub.open(QIODevice::ReadOnly));
    const auto book = unzip(epub.readAll());
    QVERIFY(!QImage::fromData(book.value("OEBPS/images/image1.png")).isNull());
  }

  void viewSwitchThemePasteAndUndo() {
    ui::ViewSwitch view;
    ui::Editor edit;
    QVERIFY(view.loadMarkdownIntoBoth(specimen(), nullptr));
    view.showActiveIn(&edit);
    const QString before = view.currentMarkdown();
    const int steps = edit.document()->availableUndoSteps();
    edit.setDarkTheme(true);
    edit.setDarkTheme(false);
    QCOMPARE(edit.document()->availableUndoSteps(), steps);
    QVERIFY(same(before, view.currentMarkdown()));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(same(before, view.currentMarkdown()));
    QTextCursor cursor(edit.document());
    cursor.movePosition(QTextCursor::End);
    cursor.insertBlock();
    QVERIFY(editor::insertMarkdownAtCursor(
        QStringLiteral("A formula $\\sqrt{x}$ and a note[^next]."), cursor, nullptr));
    QVERIFY(editor::serializeDocument(edit.document()).contains(QStringLiteral("$\\sqrt{x}$")));
    edit.document()->undo();
    QVERIFY(!editor::serializeDocument(edit.document()).contains(QStringLiteral("sqrt{x}")));
    render::invalidateToc(edit.document());
    QVERIFY(edit.document()->isUndoAvailable());
  }
  void inlineMathAndFootnoteInput() {
    ui::ViewSwitch view;
    ui::Editor edit;
    QVERIFY(view.loadMarkdownIntoBoth(QString(), nullptr));
    view.showActiveIn(&edit);
    edit.show();
    edit.setFocus();
    QTest::keyClicks(&edit, "Value $x^2$");
    QVERIFY(editor::serializeDocument(edit.document()).contains(QStringLiteral("$x^2$")));
    QVERIFY(edit.toPlainText().contains(QChar::ObjectReplacementCharacter));
    QTest::keyClicks(&edit, " next[^a]");
    QVERIFY(editor::serializeDocument(edit.document()).contains(QStringLiteral("[^a]")));
    QVERIFY(!edit.toPlainText().contains(QStringLiteral("[^a]")));
    QTest::keyClick(&edit, Qt::Key_Return);
    QTest::keyClicks(&edit, "[TOC]");
    QTest::keyClick(&edit, Qt::Key_Return);
    QVERIFY(editor::serializeDocument(edit.document()).contains(QStringLiteral("[TOC]")));
  }
  void copyingObjectsPreservesMarkdown() {
    QTextDocument document;
    QVERIFY(editor::loadMarkdown(specimen(), &document, nullptr));
    int count = 0;
    for (auto block = document.begin(); block.isValid(); block = block.next())
      for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        if (!fragment.charFormat().hasProperty(editor::kSpecialSourceProperty) &&
            fragment.charFormat().objectType() != render::kTocObjectType)
          continue;
        QTextCursor cursor(&document);
        cursor.setPosition(fragment.position());
        cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
        const QString selected = editor::selectedMarkdown(cursor);
        const auto parsed = core::parseMarkdown(selected.toStdString());
        QVERIFY(parsed.ok);
        QVERIFY(!parsed.blocks.empty());
        const auto type = parsed.blocks.front().type;
        QVERIFY(type == core::BlockType::Math || type == core::BlockType::Diagram ||
                type == core::BlockType::TableOfContents);
        ++count;
      }
    QCOMPARE(count, 4);
    const QString quote = QStringLiteral("> ```mermaid\n> flowchart LR\n> A-->B\n> ```\n");
    QVERIFY(editor::loadMarkdown(quote, &document, nullptr));
    QVERIFY2(same(quote, editor::serializeDocument(&document)),
             qPrintable(editor::serializeDocument(&document)));
  }
  void tableOfContentsDepthFiltersExportsAndKeepsHeadingTargets() {
    const auto source = QStringLiteral(
        "[TOC]\n\n# Parent\n\n#### Deep\n\n## Next\n\n###### Last\n\n[Jump](#deep)\n");
    for (const bool styled : {true, false}) {
      QList<render::HtmlHeading> outline;
      const auto html = render::html(source, {false, styled, 2}, &outline);
      const auto start = html.indexOf(QStringLiteral("<nav class=\"table-of-contents\">"));
      const auto end = html.indexOf(QStringLiteral("</nav>"), start);
      QVERIFY(start >= 0 && end > start);
      const auto toc = html.mid(start, end - start);
      QVERIFY(toc.contains(QStringLiteral("href=\"#parent\"")));
      QVERIFY(toc.contains(QStringLiteral("href=\"#next\"")));
      QVERIFY(!toc.contains(QStringLiteral("href=\"#deep\"")));
      QVERIFY(!toc.contains(QStringLiteral("href=\"#last\"")));
      QVERIFY(html.contains(QStringLiteral("<h4 id=\"deep\">Deep</h4>")));
      QVERIFY(html.contains(QStringLiteral("<a href=\"#deep\">Jump</a>")));
      QCOMPARE(outline.size(), 4);
    }

    QTextDocument document;
    QVERIFY(editor::loadMarkdown(source, &document, nullptr));
    std::unique_ptr<QTextDocument> printed(document.clone());
    render::expandTocForPrint(printed.get(), 2);
    QStringList entries;
    for (auto block = printed->begin(); block.isValid() && !block.blockFormat().headingLevel();
         block = block.next())
      entries.append(block.text());
    QCOMPARE(entries, (QStringList{QStringLiteral("Parent"), QStringLiteral("Next")}));

    QString error;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QStringLiteral("toc.docx"));
    QVERIFY2(platform::exportDocx(path, source, &error, {}, 2), qPrintable(error));
    QFile docx(path);
    QVERIFY(docx.open(QIODevice::ReadOnly));
    const auto xml = unzip(docx.readAll()).value("word/document.xml");
    const auto fieldEnd = xml.indexOf("w:fldCharType=\"end\"");
    QVERIFY(fieldEnd > 0);
    const auto toc = xml.left(fieldEnd);
    QVERIFY(toc.contains("TOC \\o &quot;1-2&quot;"));
    QVERIFY(!toc.contains("\\u"));
    QVERIFY(toc.contains("w:anchor=\"heading_1\""));
    QVERIFY(toc.contains("w:anchor=\"heading_3\""));
    QVERIFY(!toc.contains("w:anchor=\"heading_2\""));
    QVERIFY(xml.contains("w:name=\"heading_2\""));

    platform::ImageExportOptions pngOptions;
    const auto fullPath = directory.filePath(QStringLiteral("full.png"));
    const auto limitedPath = directory.filePath(QStringLiteral("limited.png"));
    QCOMPARE(platform::exportImage(fullPath, document, pngOptions, false),
             platform::ImageExportError::None);
    pngOptions.tocDepth = 2;
    QCOMPARE(platform::exportImage(limitedPath, document, pngOptions, false),
             platform::ImageExportError::None);
    const QImage full(fullPath), limited(limitedPath);
    QVERIFY(!limited.isNull() && limited.height() < full.height());

    platform::PdfPageOptions pdfOptions;
    const auto fullPage = platform::pdfPagePreview(&document, pdfOptions, 0, nullptr, &error);
    pdfOptions.tocDepth = 2;
    const auto limitedPage = platform::pdfPagePreview(&document, pdfOptions, 0, nullptr, &error);
    QVERIFY2(!fullPage.isNull() && !limitedPage.isNull(), qPrintable(error));
    QVERIFY(fullPage != limitedPage);
  }
  void longTocPrintsAsParagraphs() {
    QString source = QStringLiteral("[TOC]\n\n");
    for (int i = 1; i <= 60; ++i)
      source += QStringLiteral("## Section %1\n\nA paragraph for this section.\n\n").arg(i);
    QTextDocument document;
    QVERIFY(editor::loadMarkdown(source, &document, nullptr));
    const QString before = editor::serializeDocument(&document);
    const int undo = document.availableUndoSteps();
    std::unique_ptr<QTextDocument> copy(document.clone());
    render::expandTocForPrint(copy.get());
    int headings = 0, paragraphs = 0;
    for (auto block = copy->begin(); block.isValid(); block = block.next()) {
      headings += block.blockFormat().headingLevel() > 0;
      ++paragraphs;
    }
    QCOMPARE(headings, 60);
    QVERIFY(paragraphs >= 180);
    QCOMPARE(editor::serializeDocument(&document), before);
    QCOMPARE(document.availableUndoSteps(), undo);
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QString error;
      QVERIFY2(platform::exportPdf(retained + "/long-toc.pdf", &document, &error),
               qPrintable(error));
    }
  }
  void doubleClickEditsFormulaAndUndo() {
    ui::ViewSwitch view;
    ui::Editor edit;
    QVERIFY(view.loadMarkdownIntoBoth(QStringLiteral("A $x^2$ formula."), nullptr));
    view.showActiveIn(&edit);
    edit.resize(1000, 500);
    edit.show();
    QTest::qWait(10);
    const QString before = view.currentMarkdown();
    int object = edit.toPlainText().indexOf(QChar::ObjectReplacementCharacter);
    QVERIFY(object >= 0);
    QTextCursor cursor(edit.document());
    cursor.setPosition(object);
    const auto rect = edit.cursorRect(cursor);
    QTimer::singleShot(10, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (!dialog) return;
      auto* input = dialog->findChild<QPlainTextEdit*>(QStringLiteral("advancedSource"));
      if (input) input->setPlainText(QStringLiteral("\\sqrt{x}"));
      dialog->accept();
    });
    QTest::mouseDClick(edit.viewport(), Qt::LeftButton, Qt::NoModifier,
                       QPoint(rect.left() + 3, rect.center().y()));
    QVERIFY(view.currentMarkdown().contains(QStringLiteral("$\\sqrt{x}$")));
    edit.undo();
    QVERIFY(same(before, view.currentMarkdown()));
  }
  void htmlDocxAndPdfExport() {
    QTemporaryDir temporary;
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    const QString out = retained.isEmpty() ? temporary.path() : retained;
    QDir().mkpath(out);
    const QString source = specimen(), html = render::html(source);
    QVERIFY(html.contains(QStringLiteral("data:image/png;base64,")));
    QVERIFY(html.contains(QStringLiteral("href=\"#mathematics--数学\"")));
    QVERIFY(html.contains(QStringLiteral("id=\"mathematics--数学\"")));
    QVERIFY(html.contains(QStringLiteral("class=\"footnote\"")));
    QVERIFY(!html.contains(QStringLiteral("typist-math:")));
    QFile htmlFile(out + "/advanced-sample.html");
    QVERIFY(htmlFile.open(QIODevice::WriteOnly));
    htmlFile.write(
        QByteArray::fromStdString(core::wrapHtmlDocument(html.toStdString(), "Advanced Markdown")));
    htmlFile.close();
    QFile markdown(out + "/advanced-sample.md");
    QVERIFY(markdown.open(QIODevice::WriteOnly));
    markdown.write(source.toUtf8());
    markdown.close();
    QString error;
    QVERIFY2(platform::exportDocx(out + "/advanced-sample.docx", source, &error),
             qPrintable(error));
    QFile docx(out + "/advanced-sample.docx");
    QVERIFY(docx.open(QIODevice::ReadOnly));
    const auto entries = unzip(docx.readAll());
    QVERIFY(entries.size() >= 13);
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
      if (it.key().endsWith(".xml") || it.key().endsWith(".rels")) {
        QXmlStreamReader xml(it.value());
        while (!xml.atEnd()) xml.readNext();
        QVERIFY2(!xml.hasError(),
                 qPrintable(QString::fromUtf8(it.key()) + ": " + xml.errorString()));
      }
    QVERIFY(entries.value("word/document.xml").contains("w:footnoteReference"));
    QVERIFY(entries.value("word/footnotes.xml").contains("w:footnoteRef"));
    QVERIFY(entries.value("word/document.xml").contains(" TOC "));
    QVERIFY(entries.value("word/document.xml").contains("w:anchor=\"heading_2\""));
    QTextDocument document;
    QVERIFY(editor::loadMarkdown(source, &document, &error));
    platform::PdfPageOptions options;
    options.footer = QStringLiteral("#");
    QVERIFY2(platform::exportPdf(out + "/advanced-sample.pdf", &document, options, &error),
             qPrintable(error));
    ui::Editor edit;
    edit.setDocument(&document);
    edit.configurePresentation(true);
    edit.resize(1180, 1000);
    edit.show();
    QTest::qWait(20);
    if (!retained.isEmpty()) {
      QVERIFY(edit.grab().save(out + "/advanced-editor-light.png"));
      edit.setDarkTheme(true);
      edit.setStyleSheet(QStringLiteral("QTextEdit {background:#20252b;color:#dde3ec;}"));
      QTest::qWait(10);
      QVERIFY(edit.grab().save(out + "/advanced-editor-dark.png"));
    }
  }
};
QTEST_MAIN(AdvancedContentTests)
#include "advanced_content_tests.moc"
