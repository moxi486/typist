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
        "```mermaid\ngantt\n dateFormat DD-MM-YYYY\n Writing: 28-10-2026, 1d\n```\n");
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

  void xyChartsRenderBothOrientationsAndCombineSeries() {
    const auto definition = QStringLiteral("x-axis [A, B]\ny-axis -4 --> 4\nbar [3, -3]\n");
    const auto portrait = render::mermaid(QStringLiteral("xychart\n") + definition);
    const auto landscape =
        render::mermaid(QStringLiteral("xychart-beta horizontal\n") + definition);
    QVERIFY2(!portrait.image.isNull(), qPrintable(portrait.error));
    QVERIFY2(!landscape.image.isNull(), qPrintable(landscape.error));
    const auto bounds = [](const QImage& image, const QRect& region, const QColor& color) {
      QRect found;
      for (int y = region.top(); y <= region.bottom(); y += 2)
        for (int x = region.left(); x <= region.right(); x += 2)
          if (image.pixel(x, y) == color.rgba()) found |= QRect(x, y, 1, 1);
      return found;
    };
    const QColor blue("#327fc2"), orange("#bf521c");
    const int px = portrait.image.width() / 2, py = portrait.image.height();
    const auto verticalPositive = bounds(portrait.image, QRect(0, 0, px, py), blue);
    const auto verticalNegative = bounds(portrait.image, QRect(px, 0, px, py), blue);
    QVERIFY(verticalPositive.isValid() && verticalNegative.isValid());
    QVERIFY(verticalPositive.bottom() < verticalNegative.top());
    const int hx = landscape.image.width(), hy = landscape.image.height() / 2;
    const auto horizontalPositive = bounds(landscape.image, QRect(0, 0, hx, hy), blue);
    const auto horizontalNegative = bounds(landscape.image, QRect(0, hy, hx, hy), blue);
    QVERIFY(horizontalPositive.isValid() && horizontalNegative.isValid());
    QVERIFY(horizontalNegative.right() < horizontalPositive.left());

    const auto monthly = QStringLiteral(
        "xychart-beta\ntitle \"每月产量（件）\"\nx-axis \"月份\" [一月, 二月, \"三月, 试产\", "
        "四月]\n"
        "y-axis \"数量\" 0 --> 100\nbar \"实际\" [40, 65, 72, 83]\n"
        "line \"目标 %% 计划\" [50, 60, 75, 80] %% planned\n");
    const auto light = render::mermaid(monthly);
    const auto dark = render::mermaid(monthly, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.devicePixelRatio(), 2.);
    QVERIFY(dark.image.pixelColor(0, 0) != light.image.pixelColor(0, 0));
    QVERIFY(bounds(light.image, light.image.rect().adjusted(0, 0, 0, -128), blue).isValid());
    QVERIFY(bounds(light.image, light.image.rect().adjusted(0, 0, 0, -128), orange).isValid());
    auto alias = monthly;
    alias.replace("xychart-beta\n", "xychart vertical\n");
    QCOMPARE(light.image, render::mermaid(alias).image);
    const auto numeric =
        render::mermaid(QStringLiteral("xychart\nx-axis Time -2 --> 2\ny-axis Level\nline [5, 5]"));
    QVERIFY2(!numeric.image.isNull(), qPrintable(numeric.error));
    QVERIFY(!render::mermaid(QStringLiteral("xychart\nline [+1.3, .6, -.34]")).image.isNull());
    const auto clipped =
        render::mermaid(QStringLiteral("xychart\nx-axis [A, B]\nbar [4, -3, 999]"));
    const auto paired = render::mermaid(QStringLiteral("xychart\nx-axis [A, B]\nbar [4, -3]"));
    QVERIFY(!clipped.image.isNull());
    QCOMPARE(clipped.image, paired.image);
    for (const auto& invalid :
         {QStringLiteral("xychart\ny-axis 5 --> 1\nline [2]"),
          QStringLiteral("xychart\nx-axis [A, \"unfinished]\nbar [2]"),
          QStringLiteral("xychart\nbar [1, NaN]"), QStringLiteral("xychart\nline [1,]"),
          QStringLiteral("xychart\n") + QStringLiteral("line [1]\n").repeated(9)}) {
      const auto result = render::mermaid(invalid);
      QVERIFY(result.image.isNull() && !result.error.isEmpty());
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/xychart-light.png"));
      QVERIFY(dark.image.save(retained + "/xychart-dark.png"));
    }
  }

  void stateDiagramsRenderDirectionsLoopsAndChoice() {
    const auto source = QStringLiteral(
        "stateDiagram-v2\n"
        "    state \"等待编辑\" as Idle\n    Active: 编辑中\n"
        "    [*] --> Idle\n    Idle --> Active : 打开文档 %% enter\n"
        "    Active --> Idle : 关闭文档\n    Idle --> [*] : 取消\n"
        "    Active --> Active : 自动保存\n    Active --> [*] : 退出应用\n");
    const auto light = render::mermaid(source);
    const auto dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.devicePixelRatio(), 2.);
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image.pixelColor(0, 0) != dark.image.pixelColor(0, 0));

    auto equivalent = source;
    equivalent.replace(QStringLiteral("stateDiagram-v2"), QStringLiteral("stateDiagram"));
    equivalent.replace(QStringLiteral("state \"等待编辑\" as Idle"),
                       QStringLiteral("Idle: 等待编辑"));
    QCOMPARE(light.image, render::mermaid(equivalent).image);

    auto horizontal = source;
    horizontal.replace(QStringLiteral("stateDiagram-v2\n"),
                       QStringLiteral("stateDiagram-v2\r\n    direction LR\r\n"));
    const auto wide = render::mermaid(horizontal);
    QVERIFY2(!wide.image.isNull(), qPrintable(wide.error));
    QVERIFY(wide.image.width() > wide.image.height());
    QVERIFY(light.image.height() > light.image.width());
    auto noLoop = source;
    noLoop.remove(QStringLiteral("    Active --> Active : 自动保存\n"));
    const auto smaller = render::mermaid(noLoop);
    QVERIFY(!smaller.image.isNull());
    QVERIFY(smaller.image.width() < light.image.width());

    const auto branching = render::mermaid(
        QStringLiteral("stateDiagram-v2\nstate Decision <<choice>>; [*] --> Decision\n"
                       "Decision --> Approved : yes\nDecision --> Denied : no\n"
                       "Approved --> [*]\nDenied --> [*]"));
    QVERIFY2(!branching.image.isNull(), qPrintable(branching.error));
    for (const auto& invalid : {QStringLiteral("stateDiagram-v2\nstate Active {\n[*] --> Idle\n}"),
                                QStringLiteral("stateDiagram\nA --> B:::warning"),
                                QStringLiteral("stateDiagram\naccTitle: Hidden metadata\nA --> B"),
                                QStringLiteral("stateDiagram\nUnfinished -->"),
                                QStringLiteral("stateDiagram-v2\ndirection unknown\nA --> B")}) {
      const auto result = render::mermaid(invalid);
      QVERIFY(result.image.isNull() && !result.error.isEmpty());
    }

    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/state-light.png"));
      QVERIFY(dark.image.save(retained + "/state-dark.png"));
    }
  }

  void ganttSchedulesResolveGroupsDependenciesAndExcludedDates() {
    const QString header = QStringLiteral(
        "gantt\n title 文档发布计划\n dateFormat YYYY-MM-DD\n excludes weekends\n"
        " excludes 2026-11-25 %% 休息日\n section 准备\n");
    const QString source =
        header + QStringLiteral(
                     " 整理资料 :done, draft, 2026-11-19, 2d\n"
                     " 技术复核 :active, review, after draft, 2d\n"
                     " section 发布\n 校对样例 :crit, check, after draft review, 24h\n"
                     " 发布新版 :milestone, after check, 0d\n"
                     " 小范围验收 :12h\n");
    const auto light = render::mermaid(source);
    const auto dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);

    const QString fixed = header + QStringLiteral(
                                       " 整理资料 :done, 2026-11-19, 2026-11-21\n"
                                       " 技术复核 :active, 2026-11-23, 2026-11-25\n"
                                       " section 发布\n 校对样例 :crit, 2026-11-26, 2026-11-27\n"
                                       " 发布新版 :milestone, 2026-11-27, 2026-11-27\n"
                                       " 小范围验收 :2026-11-27, 0.5d\n");
    QCOMPARE(light.image, render::mermaid(fixed).image);

    QString noExclusions = source;
    noExclusions.remove("excludes weekends\n ").remove("excludes 2026-11-25 %% 休息日\n ");
    const auto continuous = render::mermaid(noExclusions);
    QVERIFY(!continuous.image.isNull());
    QVERIFY(light.image != continuous.image);
    const auto forward = render::mermaid(
        QStringLiteral("gantt\n Second :after one, 30m\n First :one, 2026-11-19, 3h\n"));
    QCOMPARE(forward.image,
             render::mermaid(
                 QStringLiteral("gantt\n Second :after one, 30m\n First :one, 2026-11-19, 180m\n"))
                 .image);
    QVERIFY(!forward.image.isNull());
    QVERIFY(!render::mermaid(QStringLiteral("gantt\n Week :2026-11-19, 1w")).image.isNull());

    const auto matchingPixels = [](const QImage& diagram, QColor color) {
      int count = 0;
      for (int y = 0; y < diagram.height(); ++y)
        for (int x = 0; x < diagram.width(); ++x) count += diagram.pixelColor(x, y) == color;
      return count;
    };
    for (const char* color : {"#397458", "#167d86", "#b34542", "#4576bc", "#f1e7e9"}) {
      QVERIFY(matchingPixels(light.image, QColor(color)) > 100);
    }
    QCOMPARE(matchingPixels(continuous.image, QColor("#f1e7e9")), 0);

    const QString row = QStringLiteral(" Work :2026-11-19, 1d\n");
    QVERIFY(!render::mermaid(QStringLiteral("gantt\n") + row.repeated(24)).image.isNull());
    QVERIFY(render::mermaid(QStringLiteral("gantt\n") + row.repeated(25)).image.isNull());
    for (const QString& invalid : {
             QStringLiteral("gantt\n title No tasks"),
             QStringLiteral("gantt\n Work :2026-02-29, 1d"),
             QStringLiteral("gantt\n Work :2026-11-19, 2026-11-18"),
             QStringLiteral("gantt\n Work :2026-11-19, 9999999w"),
             QStringLiteral("gantt\n Work :1d"),
             QStringLiteral("gantt\n Work :after missing, 1d"),
             QStringLiteral("gantt\n A :a, after b, 1d\n B :b, after a, 1d"),
             QStringLiteral("gantt\n A :a, 2026-11-19, 1d\n B :a, after a, 1d"),
             QStringLiteral("gantt\n excludes 2026-02-31\n Work :2026-11-19, 1d"),
             QStringLiteral("gantt\n axisFormat %m-%d\n Work :2026-11-19, 1d"),
             QStringLiteral("%%{init: {}}%%\ngantt\n Work :2026-11-19, 1d"),
         }) {
      const auto result = render::mermaid(invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/gantt-light.png"));
      QVERIFY(dark.image.save(retained + "/gantt-dark.png"));
    }
  }

  void classDiagramsRenderMembersAndUmlRelations() {
    const QString plot = QStringLiteral(
        "classDiagram\n"
        " class Document[\"文档\"] {\n"
        "  <<abstract>>\n  -String title\n  +int count$\n  +save() bool*\n }\n"
        " class MarkdownDocument[\"Markdown 文档\"] {\n  +toMarkdown() String\n }\n"
        " class Section[\"章节\"]\n"
        " Document <|-- MarkdownDocument\n"
        " Document \"1\" *-- \"1..*\" Section : 包含\n"
        " Section --> Section : 下一节\n");
    const auto light = render::mermaid(plot);
    const auto dark = render::mermaid(plot, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);

    const auto alternate = render::mermaid(QStringLiteral(
        "classDiagram\n"
        " class Document[\"文档\"] <<abstract>>\n"
        " Document : +save() bool*\n Document : -String title\n Document : +int count$\n"
        " class MarkdownDocument[\"Markdown 文档\"]\n"
        " MarkdownDocument : +toMarkdown() String\n"
        " class Section[\"章节\"]\n"
        " Document<|--MarkdownDocument\n"
        " Document \"1\" *-- \"1..*\" Section : 包含\n"
        " Section --> Section : 下一节\n"));
    QCOMPARE(light.image, alternate.image);

    const auto base = QStringLiteral("classDiagram\nclass Whole\nclass Part\n");
    const auto composition = render::mermaid(base + QStringLiteral("Whole *-- Part"));
    const auto aggregation = render::mermaid(base + QStringLiteral("Whole o-- Part"));
    const auto reversed = render::mermaid(base + QStringLiteral("Part --* Whole"));
    QVERIFY(!composition.image.isNull());
    QCOMPARE(composition.image.size(), aggregation.image.size());
    QVERIFY(composition.image != aggregation.image);
    QCOMPARE(composition.image, reversed.image);
    const auto dependency = render::mermaid(base + QStringLiteral("Whole ..> Part"));
    const auto association = render::mermaid(base + QStringLiteral("Whole --> Part"));
    QCOMPARE(dependency.image.size(), association.image.size());
    QVERIFY(dependency.image != association.image);
    const auto realization = render::mermaid(base + QStringLiteral("Whole <|.. Part"));
    const auto inheritance = render::mermaid(base + QStringLiteral("Whole <|-- Part"));
    QCOMPARE(realization.image.size(), inheritance.image.size());
    QVERIFY(realization.image != inheritance.image);
    const auto horizontal = render::mermaid(base + QStringLiteral("direction LR\nWhole *-- Part"));
    const auto rtl = render::mermaid(base + QStringLiteral("direction RL\nWhole *-- Part"));
    const auto upwards = render::mermaid(base + QStringLiteral("direction BT\nWhole *-- Part"));
    QVERIFY(horizontal.image.width() > composition.image.width());
    QVERIFY(horizontal.image.height() < composition.image.height());
    QCOMPARE(horizontal.image.size(), rtl.image.size());
    QVERIFY(horizontal.image != rtl.image);
    QCOMPARE(upwards.image.size(), composition.image.size());
    QVERIFY(upwards.image != composition.image);
    QVERIFY(!render::mermaid(
                 QStringLiteral("classDiagram\n A --> B\n B --> C\n A \"1\" --> \"many\" C\n"
                                " C --> A\n"))
                 .image.isNull());
    const auto standalone = render::mermaid(QStringLiteral("classDiagram\nclass 文档"));
    const auto byReference = render::mermaid(
        QStringLiteral("classDiagram\nclass 文档\n<<interface>> 文档\n文档 : +打开()"));
    QVERIFY(byReference.image.height() > standalone.image.height());

    for (const auto& invalid : {
             QStringLiteral("classDiagram"),
             QStringLiteral("classDiagram\nclass A{\n-int field"),
             QStringLiteral("classDiagram\nclass A~int~"),
             QStringLiteral("classDiagram\nclass A\nA : +List~int~ items"),
             QStringLiteral("classDiagram\nnamespace Core {\n class A\n}"),
             QStringLiteral("classDiagram\nclass A\nnote for A \"text\""),
             QStringLiteral("classDiagram\nclass A\nA ?-- B"),
             QStringLiteral("classDiagram\nclass A\nclassDef selected fill:#ccc"),
             QStringLiteral("%%{init: {}}%%\nclassDiagram\nclass A"),
         }) {
      const auto result = render::mermaid(invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/class-light.png"));
      QVERIFY(dark.image.save(retained + "/class-dark.png"));
    }
  }

  void quadrantChartsPlotNormalizedCoordinatesAndKeepDenseLabels() {
    const QString header = QStringLiteral(
        "quadrantChart\n"
        " title 写作改进优先级\n x-axis 低投入 --> 高投入\n y-axis 低收益 --> 高收益\n"
        " quadrant-1 规划后实施\n quadrant-2 优先实施\n quadrant-3 维持现状\n"
        " quadrant-4 暂缓\n");
    const QString points = QStringLiteral(
        " 模板整理: [0.25, 0.75]\n \"移动端: 适配\": [0.75, 0.75]\n"
        " 快捷键微调: [0.25, 0.25]\n 主题重绘: [.75, .25]\n"
        " 起点: [0, 0]\n 终点: [1, 1]\n");
    const auto light = render::mermaid(header + points);
    const auto dark = render::mermaid(header + points, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);

    const auto chartPixel = [&](qreal x, qreal y) {
      return light.image.pixelColor(qRound((78 + x * 440) * light.image.devicePixelRatio()),
                                    qRound((59 + (1 - y) * 440) * light.image.devicePixelRatio()));
    };
    const QColor marker("#245c93");
    for (const qreal x : {0.25, 0.75})
      for (const qreal y : {0.25, 0.75}) QCOMPARE(chartPixel(x, y), marker);
    QCOMPARE(chartPixel(0, 0), marker);
    QCOMPARE(chartPixel(1, 1), marker);
    QCOMPARE(chartPixel(0.9, 0.9), QColor("#e7f3eb"));
    QCOMPARE(chartPixel(0.1, 0.9), QColor("#e8f1fc"));
    QCOMPARE(chartPixel(0.1, 0.1), QColor("#f0edf7"));
    QCOMPARE(chartPixel(0.9, 0.1), QColor("#f7eee6"));
    const QString orientation = QStringLiteral("quadrantChart\n Point: [0.2, 0.8]\n");
    const auto plotted = render::mermaid(orientation);
    const QColor located =
        plotted.image.pixelColor(qRound((78 + 0.2 * 440) * plotted.image.devicePixelRatio()),
                                 qRound((22 + 0.2 * 440) * plotted.image.devicePixelRatio()));
    QCOMPARE(located, marker);

    const auto blank = render::mermaid(header);
    QVERIFY(!blank.image.isNull());
    QVERIFY(blank.image != light.image);
    const auto singleAxis =
        render::mermaid(QStringLiteral("quadrantChart\n x-axis \"low --> target\"\n"));
    QVERIFY(!singleAxis.image.isNull());
    QCOMPARE(singleAxis.image.size(),
             render::mermaid(QStringLiteral("quadrantChart\n x-axis low\n")).image.size());
    auto quoted = header;
    quoted.replace("低投入 --> 高投入", "\"低投入\" --> \"高投入\"");
    quoted.replace("低收益 --> 高收益", "\"低收益\" --> \"高收益\"");
    QCOMPARE(light.image, render::mermaid(quoted + points).image);
    QCOMPARE(light.image,
             render::mermaid(QStringLiteral("%% Comment\r\n") + header + points).image);

    QString dense = header;
    for (int i = 1; i <= 32; ++i)
      dense += QStringLiteral("Repeated observation %1: [0.5, 0.5]\n").arg(i);
    const auto crowded = render::mermaid(dense), crowdedDark = render::mermaid(dense, true);
    QVERIFY(crowded.image.height() > light.image.height());
    QCOMPARE(crowded.image.size(), crowdedDark.image.size());
    QVERIFY(render::mermaid(dense + QStringLiteral("One more: [0.5, 0.5]\n")).image.isNull());

    for (const auto& invalid : {
             QStringLiteral("quadrantChart\n Point: [1.2, 0.2]"),
             QStringLiteral("quadrantChart\n Point: [-0.2, 0.2]"),
             QStringLiteral("quadrantChart\n Point: [0.2, NaN]"),
             QStringLiteral("quadrantChart\n Point: [0.2, 0.2] radius: 8"),
             QStringLiteral("quadrantChart\n Point:::style: [0.2, 0.2]"),
             QStringLiteral("quadrantChart\n x-axis baseline -->"),
             QStringLiteral("quadrantChart\n x-axis a --> b --> c"),
             QStringLiteral("quadrantChart\n quadrant-5 Somewhere"),
             QStringLiteral("quadrantChart\n quadrant-1 One\n quadrant-1 Two"),
             QStringLiteral("%%{init: {}}%%\nquadrantChart\n Point: [0.2, 0.2]"),
         }) {
      const auto result = render::mermaid(invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/quadrant-light.png"));
      QVERIFY(dark.image.save(retained + "/quadrant-dark.png"));
      QVERIFY(blank.image.save(retained + "/quadrant-blank.png"));
      QVERIFY(crowded.image.save(retained + "/quadrant-dense.png"));
    }
  }

  void mindmapsUseIndentationShapesAndBranchColors() {
    const QString source = QStringLiteral(
        "mindmap\n"
        "  root((写作计划))\n"
        "    research[资料整理]\n"
        "    ::icon(fa fa-book)\n"
        "      Sources\n"
        "      整理术语<br/>与例子\n"
        "    drafting(撰写初稿)\n"
        "      goals{{大纲}}\n"
        "      idea)记录想法(\n"
        "    review))检查与发布((\n"
        "      ::icon(fas fa-check)\n"
        "      发布\n");
    const auto light = render::mermaid(source);
    const auto dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    const auto colored = [&](const QColor& color) {
      int pixels = 0;
      for (int y = 0; y < light.image.height(); ++y)
        for (int x = 0; x < light.image.width(); ++x)
          pixels += light.image.pixelColor(x, y) == color;
      return pixels;
    };
    for (const char* fill : {"#e8f3fa", "#f0ebfa", "#e6f4eb"}) {
      QVERIFY(colored(QColor(fill)) > 150);
    }

    auto uneven = source;
    uneven.replace("      Sources\n", "         Sources\n    %% note\n");
    uneven.replace("      整理", "       整理");
    QCOMPARE(light.image, render::mermaid(uneven).image);
    auto shifted = source;
    shifted.replace("\n ", "\n    ");
    QCOMPARE(light.image, render::mermaid(shifted).image);
    auto chain = source;
    chain.replace("      整理", "       整理");
    QVERIFY(light.image != render::mermaid(chain).image);

    auto square = source;
    square.replace("research[资料整理]", "research(资料整理)");
    QVERIFY(light.image != render::mermaid(square).image);
    auto noIcon = source;
    noIcon.remove("    ::icon(fa fa-book)\n");
    QVERIFY(light.image != render::mermaid(noIcon).image);
    const auto oneLine = render::mermaid(QStringLiteral("mindmap\nroot(I)"));
    const auto lineBreak = render::mermaid(QStringLiteral("mindmap\nroot(I<br>I)"));
    QVERIFY(!oneLine.image.isNull());
    QVERIFY(lineBreak.image.height() > oneLine.image.height());
    QVERIFY(!render::mermaid(QStringLiteral("mindmap\n[\"Heading (details)\"]")).image.isNull());
    const auto longNode =
        render::mermaid(QStringLiteral("mindmap\n  root[") +
                        QStringLiteral("长标签").repeated(200) + QStringLiteral("]\n    Short"));
    const auto fourLines =
        render::mermaid(QStringLiteral("mindmap\n  root[I<br>I<br>I<br>I]\n    Short"));
    QVERIFY(!longNode.image.isNull());
    QVERIFY(!fourLines.image.isNull());
    QVERIFY2(longNode.image.height() <= fourLines.image.height(),
             qPrintable(QStringLiteral("Long label: %1 px; four explicit lines: %2 px")
                            .arg(longNode.image.height())
                            .arg(fourLines.image.height())));

    const QString many =
        QStringLiteral("mindmap\nRoot\n") + QStringLiteral("  Child\n").repeated(47);
    QVERIFY(!render::mermaid(many).image.isNull());
    QVERIFY(render::mermaid(many + QStringLiteral("  Child\n")).image.isNull());
    QString deep = QStringLiteral("mindmap\n");
    for (int i = 0; i < 8; ++i) deep += QString(i, ' ') + QStringLiteral("Node\n");
    QVERIFY(!render::mermaid(deep).image.isNull());
    QVERIFY(render::mermaid(deep + QStringLiteral("        Too deep")).image.isNull());

    for (const auto& invalid : {
             QStringLiteral("mindmap"),
             QStringLiteral("mindmap\n  Root\n  Another root"),
             QStringLiteral("mindmap\n  Root\nAnother root"),
             QStringLiteral("mindmap\n  root((Missing delimiter)"),
             QStringLiteral("mindmap\n  root(( ))"),
             QStringLiteral("mindmap\n  Root\n  ::icon(fa fa-incomplete)"),
             QStringLiteral("mindmap\n  ::icon(fa fa-book)\n  Root"),
             QStringLiteral("mindmap\n  Root\n    Child\n    :::custom"),
             QStringLiteral("mindmap\n  root[\"%1Bold%1\"]").arg(QChar(0x60)),
             QStringLiteral("%%{init: {}}%%\nmindmap\n  Root"),
         }) {
      const auto result = render::mermaid(invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/mindmap-light.png"));
      QVERIFY(dark.image.save(retained + "/mindmap-dark.png"));
    }
  }

  void sankeyDiagramsKeepProportionsAcrossSplitsAndMerges() {
    const QString source = QStringLiteral(
        "sankey\n"
        "%% 来源、去向和数值\n"
        "写作计划,整理素材,12\n"
        "写作计划,直接起草,8\n"
        "\n"
        "整理素材,撰写正文,9\n"
        "整理素材,参考归档,3\n"
        "直接起草,撰写正文,8\n"
        "撰写正文,\"复核, \"\"校对\"\"\",17\n");
    const auto light = render::mermaid(source);
    const auto dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);

    auto alias = source;
    alias.replace("sankey\n", "sankey-beta\r\n");
    alias.replace("写作计划,整理素材,12\n",
                  "  写作计划, \"整理素材\", \"5\"\r\n  写作计划, 整理素材, 7\r\n");
    alias += QStringLiteral("Ignored source,Ignored target,0\n");
    QCOMPARE(light.image, render::mermaid(alias).image);

    const auto multiline = render::mermaid(QStringLiteral("sankey\nIn,\"First\r\nSecond\",0.25"));
    QVERIFY(!multiline.image.isNull());
    QCOMPARE(multiline.image, render::mermaid(QStringLiteral("sankey\nIn,First Second,.25")).image);

    const auto matchingPixels = [](const QImage& diagram, QColor color) {
      int count = 0;
      for (int y = 0; y < diagram.height(); ++y)
        for (int x = 0; x < diagram.width(); ++x) count += diagram.pixelColor(x, y) == color;
      return count;
    };
    const auto split = render::mermaid(QStringLiteral("sankey\nInput,Heavy,3\nInput,Light,1"));
    QVERIFY(!split.image.isNull());
    const int heavy = matchingPixels(split.image, QColor("#398667"));
    const int small = matchingPixels(split.image, QColor("#8061b3"));
    QVERIFY(small > 50);
    QVERIFY(qAbs(heavy - small * 3) <= small / 10);
    QVERIFY(light.image.width() > split.image.width());
    QVERIFY(!render::mermaid(QStringLiteral("sankey\nIn,A,1e308\nOther,B,6e307\n")).image.isNull());
    const QString repeated = QStringLiteral("sankey\n") + QStringLiteral("A,B,1\n").repeated(128);
    QVERIFY(!render::mermaid(repeated).image.isNull());
    QVERIFY(render::mermaid(repeated + QStringLiteral("A,B,1")).image.isNull());
    QString nodes = QStringLiteral("sankey\n");
    for (int i = 0; i < 32; ++i) nodes += QStringLiteral("A%1,B%1,1\n").arg(i);
    QVERIFY(!render::mermaid(nodes).image.isNull());
    QVERIFY(render::mermaid(nodes + QStringLiteral("A0,Extra,1")).image.isNull());

    for (const auto& invalid : {
             QStringLiteral("sankey"),
             QStringLiteral("sankey\nIn,Out"),
             QStringLiteral("sankey\nIn,Out,1,extra"),
             QStringLiteral("sankey\nIn,\"unterminated,2"),
             QStringLiteral("sankey\nIn,\"Quoted\" trailing,2"),
             QStringLiteral("sankey\n,Out,1"),
             QStringLiteral("sankey\nIn,Out,0"),
             QStringLiteral("sankey\nIn,Out,-0.2"),
             QStringLiteral("sankey\nIn,Out,NaN"),
             QStringLiteral("sankey\nIn,Out,1e308\nIn,Out,1e308"),
             QStringLiteral("sankey\nA,A,1"),
             QStringLiteral("sankey\nA,B,1\nB,C,1\nC,A,1"),
             QStringLiteral("sankey\nA,B,1\n%%{custom,b,c\n"),
             QStringLiteral("%%{init: {}}%%\nsankey\nA,B,1"),
         }) {
      const auto result = render::mermaid(invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/sankey-light.png"));
      QVERIFY(dark.image.save(retained + "/sankey-dark.png"));
    }
  }

  void gitGraphsTrackBranchesMergesTypesAndDirections() {
    const QString history = QStringLiteral(
        "gitGraph\n"
        "commit id: \"起点\"\n"
        "branch writing\ncommit id: \"完成初稿\"\n"
        "checkout main\ncommit id: \"修复问题\" type: REVERSE\n"
        "checkout writing\ncommit id: \"复核\" type: HIGHLIGHT\n"
        "checkout main\nmerge writing id: \"合并写作\" tag: \"v1.0\"\n"
        "commit id: \"发布\"\n");
    const auto light = render::mermaid(history);
    const auto dark = render::mermaid(history, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    auto equivalent = history;
    equivalent.replace("gitGraph\n", "gitGraph LR:\n");
    equivalent.replace("checkout", "switch");
    equivalent.replace("commit id: \"起点\"\n", "commit id: \"起点\"; %% starting point\n");
    equivalent.replace("branch writing\n", "branch \"writing\"\ncheckout writing\n");
    QCOMPARE(light.image, render::mermaid(equivalent).image);

    auto topDown = history;
    topDown.replace("gitGraph\n", "gitGraph TB:\n");
    auto bottomUp = history;
    bottomUp.replace("gitGraph\n", "gitGraph BT:\n");
    const auto vertical = render::mermaid(topDown), upward = render::mermaid(bottomUp);
    QVERIFY(!vertical.image.isNull() && !upward.image.isNull());
    QCOMPARE(vertical.image.size(), upward.image.size());
    QVERIFY(vertical.image != upward.image);
    QVERIFY(vertical.image.width() < light.image.width());
    QVERIFY(vertical.image.height() > light.image.height());
    auto normal = history;
    normal.remove(" type: REVERSE").remove(" type: HIGHLIGHT");
    QVERIFY(light.image != render::mermaid(normal).image);
    auto unmerged = history;
    unmerged.replace("merge writing", "commit");
    const auto withoutMerge = render::mermaid(unmerged);
    QCOMPARE(light.image.size(), withoutMerge.image.size());
    QVERIFY(light.image != withoutMerge.image);
    auto fromMain = history;
    fromMain.replace("checkout writing\ncommit id: \"复核\"", "checkout main\ncommit id: \"复核\"");
    QVERIFY(light.image != render::mermaid(fromMain).image);

    const auto generated = render::mermaid(QStringLiteral("gitGraph\ncommit\ncommit\n"));
    QCOMPARE(
        generated.image,
        render::mermaid(QStringLiteral("gitGraph:\ncommit id:\"c1\"\ncommit id:\"c2\"\n")).image);
    const auto tags = render::mermaid(
        QStringLiteral("gitGraph\ncommit id: \"root; %\" tag: \"a;b\" tag: \"v2\"\n"));
    QVERIFY(!tags.image.isNull());
    const auto ordered = render::mermaid(
        QStringLiteral("gitGraph\ncommit\nbranch a order: 2\ncommit\ncheckout main\n"
                       "branch b order: 1\ncommit\n"));
    const auto creationOrder = render::mermaid(
        QStringLiteral("gitGraph\ncommit\nbranch a\ncommit\ncheckout main\nbranch b\ncommit\n"));
    QCOMPARE(ordered.image.size(), creationOrder.image.size());
    QVERIFY(ordered.image != creationOrder.image);
    const auto inherited = render::mermaid(
        QStringLiteral("gitGraph\ncommit\nbranch a\nbranch b\ncommit\ncheckout main\nmerge b\n"));
    QVERIFY(!inherited.image.isNull());
    const QString maxCommits =
        QStringLiteral("gitGraph\n") + QStringLiteral("commit\n").repeated(32);
    QVERIFY(!render::mermaid(maxCommits).image.isNull());
    QVERIFY(render::mermaid(maxCommits + QStringLiteral("commit")).image.isNull());
    QString maxBranches = QStringLiteral("gitGraph\ncommit\n");
    for (int i = 0; i < 7; ++i) maxBranches += QStringLiteral("branch branch%1\ncommit\n").arg(i);
    QVERIFY(!render::mermaid(maxBranches).image.isNull());
    QVERIFY(render::mermaid(maxBranches + QStringLiteral("branch extra")).image.isNull());

    for (const auto& invalid : {
             QStringLiteral("gitGraph"),
             QStringLiteral("gitGraph RL:\ncommit"),
             QStringLiteral("gitGraph\ncommit id: \"unfinished"),
             QStringLiteral("gitGraph\ncommit id:\"same\"\ncommit id:\"same\""),
             QStringLiteral("gitGraph\nbranch main\ncommit"),
             QStringLiteral("gitGraph\ncheckout missing\ncommit"),
             QStringLiteral("gitGraph\ncommit\nmerge main"),
             QStringLiteral("gitGraph\ncommit\nmerge missing"),
             QStringLiteral("gitGraph\ncommit\nbranch a\nmerge main"),
             QStringLiteral("gitGraph\nbranch a\ncommit\ncheckout main\nmerge a"),
             QStringLiteral("gitGraph\ncommit type: UNKNOWN"),
             QStringLiteral("gitGraph\ncommit id:\"x\" id:\"y\""),
             QStringLiteral("gitGraph\ncommit\nbranch a order: 9999999999999"),
             QStringLiteral("gitGraph\ncommit\ncherry-pick id:\"c1\""),
             QStringLiteral("%%{init: {}}%%\ngitGraph\ncommit"),
         }) {
      const auto result = render::mermaid(invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/gitgraph-light.png"));
      QVERIFY(dark.image.save(retained + "/gitgraph-dark.png"));
      QVERIFY(vertical.image.save(retained + "/gitgraph-tb.png"));
      QVERIFY(upward.image.save(retained + "/gitgraph-bt.png"));
    }
  }

  void sequenceFragmentsNestAndKeepBranchRules() {
    const QString source = QStringLiteral(
        "sequenceDiagram\n actor User as 作者\n participant App as 编辑器\n"
        " participant Disk as 磁盘\n autonumber\n"
        " User->>App: 保存文档\n"
        " alt 有修改\n"
        "   loop 逐项校验\n App->>App: 核对格式\n end\n"
        "   par 保存正文\n App->>Disk: 写入 Markdown\n"
        "   and 保留草稿\n App->>Disk: 更新恢复文件\n end\n"
        " else 相同内容\n App-->>User: 已是最新版本\n end\n"
        " opt 显示结果\n Note over User,App: 保存完成<br>可继续写作\n end\n");
    const auto light = render::mermaid(source), dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    auto equivalent = source;
    equivalent.replace("autonumber\n", "%% Messages have explicit numbers\n");
    equivalent.replace("保存文档\n", "1. 保存文档\n").replace("核对格式\n", "2. 核对格式\n");
    equivalent.replace("写入 Markdown\n", "3. 写入 Markdown\n");
    equivalent.replace("更新恢复文件\n", "4. 更新恢复文件\n");
    equivalent.replace("已是最新版本\n", "5. 已是最新版本\n");
    QCOMPARE(light.image, render::mermaid(equivalent).image);

    const auto simple = QStringLiteral(
        "sequenceDiagram\n participant A\n participant B\n A->>B: Hello\n B-->>A: Reply\n");
    const auto plain = render::mermaid(simple);
    QVERIFY(!plain.image.isNull());
    auto opt = simple;
    opt.replace("A->>B: Hello\n", "opt Available\n A->>B: Hello\n end\n");
    const auto fragment = render::mermaid(opt);
    QVERIFY(!fragment.image.isNull());
    QVERIFY(fragment.image.height() > plain.image.height());
    auto loop = opt;
    loop.replace("opt Available", "loop Available");
    QCOMPARE(fragment.image.size(), render::mermaid(loop).image.size());
    QVERIFY(fragment.image != render::mermaid(loop).image);
    auto actor = simple;
    actor.replace("participant A", "actor A");
    QVERIFY(!render::mermaid(actor).image.isNull());
    QVERIFY(plain.image != render::mermaid(actor).image);
    const auto shortMessage = render::mermaid(QStringLiteral("sequenceDiagram\n A->>B: I"));
    const auto multiline = render::mermaid(QStringLiteral("sequenceDiagram\n A->>B: I<br>I"));
    QVERIFY(multiline.image.height() > shortMessage.image.height());
    QVERIFY(!render::mermaid(QStringLiteral("sequenceDiagram\n par One\n A->>B: A\n and Two\n"
                                            " par Nested\n A->>C: B\n and Other\n C-->>A: C\n"
                                            " end\n and Three\n B->>A: D\n end\n"))
                 .image.isNull());

    const auto message = QStringLiteral(" A->>B: Message\n");
    const auto maxMessages = QStringLiteral("sequenceDiagram\n") + message.repeated(60);
    QVERIFY(!render::mermaid(maxMessages).image.isNull());
    QVERIFY(render::mermaid(maxMessages + message).image.isNull());
    const auto maxFrames = QStringLiteral("sequenceDiagram\n") +
                           (QStringLiteral("loop Work\n") + message + "end\n").repeated(24);
    QVERIFY(!render::mermaid(maxFrames).image.isNull());
    QVERIFY(render::mermaid(maxFrames + QStringLiteral("opt Extra\n") + message + "end\n")
                .image.isNull());
    const auto maxDepth = QStringLiteral("sequenceDiagram\n") +
                          QStringLiteral("opt Condition\n").repeated(6) + message +
                          QStringLiteral("end\n").repeated(6);
    QVERIFY(!render::mermaid(maxDepth).image.isNull());
    QVERIFY(render::mermaid(QStringLiteral("sequenceDiagram\nopt Extra\n") +
                            maxDepth.mid(QStringLiteral("sequenceDiagram\n").size()) + "end\n")
                .image.isNull());
    for (const auto& invalid : {
             QStringLiteral("sequenceDiagram\n alt Missing end\n A->>B: Hello"),
             QStringLiteral("sequenceDiagram\n A->>B: Hello\nend"),
             QStringLiteral("sequenceDiagram\n else Wrong\n A->>B: Hello"),
             QStringLiteral("sequenceDiagram\n loop Work\n A->>B: Hello\n else Wrong\n end"),
             QStringLiteral("sequenceDiagram\n opt Work\n A->>B: Hello\n and Wrong\n end"),
             QStringLiteral("sequenceDiagram\n alt Work\n A->>B: Hello\nelse First\n"
                            "B->>A: Reply\nelse Second\nB->>A: Another\nend"),
             QStringLiteral("sequenceDiagram\n par Work\n A->>B: Hello\n else Wrong\n end"),
             QStringLiteral("sequenceDiagram\n A->>B: Hello\n deactivate B"),
             QStringLiteral("sequenceDiagram\n A->>+-B: Hello"),
             QStringLiteral("sequenceDiagram\nparticipant A\nparticipant B\nparticipant C\n"
                            "participant D\nparticipant E\nparticipant F\nparticipant G\n"
                            "participant H\nparticipant I\n"),
             QStringLiteral("%%{init: {}}%%\nsequenceDiagram\n A->>B: Hello"),
         }) {
      const auto result = render::mermaid(invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/sequence-light.png"));
      QVERIFY(dark.image.save(retained + "/sequence-dark.png"));
    }
  }

  void sequenceActivationsFollowCallsAndNestedReturns() {
    const QString source = QStringLiteral(
        "sequenceDiagram\n actor User as 作者\n participant App as 编辑器\n"
        " participant Disk as 磁盘\n autonumber\n"
        " User->>+App: 保存文档\n"
        " loop 校验\n App->>+App: 递归校验\n"
        " App-->>-App: 完成校验\n end\n"
        " App->>+Disk: 写入正文\n Disk-->>-App: 写入完成\n"
        " App-->>-User: 已保存\n");
    const auto light = render::mermaid(source), dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    auto explicitBars = source;
    explicitBars.replace("User->>+App: 保存文档", "User->>App: 保存文档\n activate App");
    explicitBars.replace("App->>+App: 递归校验", "App->>App: 递归校验\n activate App");
    explicitBars.replace("App-->>-App: 完成校验", "App-->>App: 完成校验\n deactivate App");
    explicitBars.replace("App->>+Disk: 写入正文", "App->>Disk: 写入正文\n activate Disk");
    explicitBars.replace("Disk-->>-App: 写入完成", "Disk-->>App: 写入完成\n deactivate Disk");
    explicitBars.replace("App-->>-User: 已保存", "App-->>User: 已保存\n deactivate App");
    QCOMPARE(light.image, render::mermaid(explicitBars).image);

    const auto matchingPixels = [](const QImage& diagram, QColor color) {
      int count = 0;
      for (int y = 0; y < diagram.height(); ++y)
        for (int x = 0; x < diagram.width(); ++x) count += diagram.pixelColor(x, y) == color;
      return count;
    };
    auto plain = source;
    plain.replace("->>+", "->>").replace("-->>-", "-->>");
    const auto inactive = render::mermaid(plain);
    QVERIFY(!inactive.image.isNull());
    QVERIFY(matchingPixels(light.image, QColor("#d8e7ff")) > 1000);
    QCOMPARE(matchingPixels(inactive.image, QColor("#d8e7ff")), 0);
    const QString flatSource =
        QStringLiteral("sequenceDiagram\n A->>+B: Call\n B->>B: Work\n B-->>-A: Return\n");
    const auto flat = render::mermaid(flatSource);
    const auto stacked =
        render::mermaid(QStringLiteral("sequenceDiagram\n A->>+B: Call\n activate B\n B->>B: Work\n"
                                       " deactivate B\n B-->>-A: Return\n"));
    QVERIFY(!flat.image.isNull() && !stacked.image.isNull());
    QVERIFY(matchingPixels(stacked.image, QColor("#d8e7ff")) >
            matchingPixels(flat.image, QColor("#d8e7ff")));

    const auto open = render::mermaid(QStringLiteral("sequenceDiagram\n A->>+B: Call\n"));
    const auto closed =
        render::mermaid(QStringLiteral("sequenceDiagram\n A->>+B: Call\n deactivate B\n"));
    QVERIFY(!open.image.isNull() && !closed.image.isNull());
    QVERIFY(open.image != closed.image);
    QVERIFY(!render::mermaid(QStringLiteral("sequenceDiagram\n participant A\n activate A\n"
                                            " A->>+B: Call\n B-->>-A: Return\n deactivate A\n"))
                 .image.isNull());
    const auto maxDepth = QStringLiteral("sequenceDiagram\n participant A\n") +
                          QStringLiteral("activate A\n").repeated(8) +
                          QStringLiteral("A->>A: Work\n") +
                          QStringLiteral("deactivate A\n").repeated(8);
    QVERIFY(!render::mermaid(maxDepth).image.isNull());
    QVERIFY(render::mermaid(QStringLiteral("sequenceDiagram\n") +
                            QStringLiteral("activate A\n").repeated(9) + "A->>A: Work\n")
                .image.isNull());
    const auto interval = QStringLiteral("activate A\ndeactivate A\n");
    QVERIFY(matchingPixels(render::mermaid(QStringLiteral("sequenceDiagram\n participant A\n") +
                                           interval + "A->>A: Continue\n")
                               .image,
                           QColor("#d8e7ff")) > 0);
    QVERIFY(!render::mermaid(QStringLiteral("sequenceDiagram\n") + interval.repeated(64))
                 .image.isNull());
    QVERIFY(render::mermaid(QStringLiteral("sequenceDiagram\n") + interval.repeated(65))
                .image.isNull());
    for (const auto& invalid : {
             QStringLiteral("sequenceDiagram\n deactivate A"),
             QStringLiteral("sequenceDiagram\n A->>B: Call\n B-->>-A: Return"),
             QStringLiteral("sequenceDiagram\n A->>+B: Call\n A-->>-B: Wrong sender"),
             QStringLiteral("sequenceDiagram\n activate A\n deactivate A\n deactivate A"),
             QStringLiteral("sequenceDiagram\n A->>++B: Call"),
             QStringLiteral("sequenceDiagram\n A->>+B: Call\n alt Missing end"),
         }) {
      const auto result = render::mermaid(invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/activation-light.png"));
      QVERIFY(dark.image.save(retained + "/activation-dark.png"));
    }
  }

  void sequenceLifecyclesStartAndStopAtMessages() {
    const QString source = QStringLiteral(
        "sequenceDiagram\n participant App as 编辑器\n actor User as 作者\n autonumber\n"
        " User->>App: 打开文档\n create participant Task as 保存任务<br>Worker\n"
        " App->>+Task: 创建任务\n loop 校验\n Task->>+Task: 递归检查\n"
        " Task-->>-Task: 检查完成\n end\n create actor Reviewer as 审核员\n"
        " Task->>Reviewer: 请求复核\n Reviewer-->>Task: 已通过\n"
        " destroy Task\n Task-->>-App: 保存成功\n destroy Reviewer\n"
        " App--xReviewer: 结束审核\n App-->>User: 完成\n");
    const auto light = render::mermaid(source), dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);

    const QString simple = QStringLiteral(
        "sequenceDiagram\n participant A\n A->>A: Before\n create participant B\n"
        " A->>B: Create\n B->>A: Reply\n destroy B\n A-xB: Destroy\n A->>A: After\n");
    const auto lifecycle = render::mermaid(simple).image;
    auto permanent = simple;
    permanent.replace("create participant B", "participant B").remove("destroy B\n");
    const auto normal = render::mermaid(permanent).image;
    QVERIFY(!lifecycle.isNull() && !normal.isNull());
    const auto inkInColumn = [](const QImage& image, int start, int end) {
      int count = 0;
      const auto background = image.pixelColor(5, 5);
      const auto ratio = image.devicePixelRatio();
      for (int y = qRound(start * ratio); y < qRound(end * ratio); ++y)
        for (int x = qRound(356 * ratio); x <= qRound(358 * ratio); ++x)
          count += image.pixelColor(x, y) != background;
      return count;
    };
    QCOMPARE(inkInColumn(lifecycle, 18, 100), 0);
    QVERIFY(inkInColumn(normal, 18, 100) > 30);
    const int lifeHeight = qRound(lifecycle.height() / lifecycle.devicePixelRatio()),
              normalHeight = qRound(normal.height() / normal.devicePixelRatio());
    QCOMPARE(inkInColumn(lifecycle, lifeHeight - 45, lifeHeight - 20), 0);
    QVERIFY(inkInColumn(normal, normalHeight - 45, normalHeight - 20) > 10);
    QVERIFY(!render::mermaid(QStringLiteral("sequenceDiagram\n participant B\n"
                                            "create participant B as Worker\n A->>B: Create\n"
                                            "destroy B\n B->>B: Finish\n"))
                 .image.isNull());
    const auto active = render::mermaid(
        QStringLiteral("sequenceDiagram\n A->>+B: Start\n activate B\n destroy B\n A-xB: Stop\n"
                       " A->>A: After\n"));
    QVERIFY(!active.image.isNull());
    const auto ratio = active.image.devicePixelRatio();
    for (int y = active.image.height() - qRound(45 * ratio); y < active.image.height(); ++y)
      for (int x = qRound(350 * ratio); x <= qRound(370 * ratio); ++x)
        QVERIFY(active.image.pixelColor(x, y) != QColor("#d8e7ff"));
    const auto solid = render::mermaid(QStringLiteral("sequenceDiagram\n A-xB: Lost\n B->>A: OK"));
    const auto dashed =
        render::mermaid(QStringLiteral("sequenceDiagram\n A--xB: Lost\n B->>A: OK"));
    QVERIFY(!solid.image.isNull() && !dashed.image.isNull());
    QCOMPARE(solid.image.size(), dashed.image.size());
    QVERIFY(solid.image != dashed.image);
    for (const auto& invalid : {
             QStringLiteral("create participant B"),
             QStringLiteral("create actor B\n B->>A: Wrong sender"),
             QStringLiteral("create participant B\n B->>B: Self creation"),
             QStringLiteral("A->>B: Used\n create participant B\n A->>B: Duplicate"),
             QStringLiteral("create participant B\n Note over B: Before creation\n A->>B: Create"),
             QStringLiteral("destroy B\n A->>B: Unknown"),
             QStringLiteral("participant B\n destroy B"),
             QStringLiteral("participant B\n destroy B\n A->>C: Unrelated"),
             QStringLiteral("A->>B: Hi\n destroy B\n A-xB: Bye\n B->>A: Late"),
             QStringLiteral("A->>B: Hi\n destroy B\n A-xB: Bye\n A->>B: Late"),
             QStringLiteral("A->>B: Hi\n destroy B\n A-xB: Bye\n Note over B: Late"),
             QStringLiteral("A->>B: Hi\n destroy B\n A-xB: Bye\n activate B"),
             QStringLiteral("A->>B: Hi\n destroy B\n A-xB: Bye\n participant B"),
             QStringLiteral("A->>B: Hi\n destroy B\n A-xB: Bye\n destroy B\n A-xB: Again"),
         }) {
      const auto result = render::mermaid(QStringLiteral("sequenceDiagram\n") + invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/lifecycle-light.png"));
      QVERIFY(dark.image.save(retained + "/lifecycle-dark.png"));
    }
  }

  void sequenceNotesRespectPlacementAndCanvasEdges() {
    const QString source = QStringLiteral(
        "sequenceDiagram\n actor User as 作者\n participant Editor as 编辑器\n autonumber\n"
        " Note left of User: 先整理思路<br>再开始写作\n User->>+Editor: 保存文档\n"
        " loop 核对\n Note left of Editor: 保留原文\n Note right of Editor: 检查格式\n end\n"
        " create participant Task as 保存任务\n Editor->>+Task: 写入正文\n"
        " Note right of Task: 正在保存<br>请稍候\n destroy Task\n Task-->>-Editor: 已保存\n"
        " Note over Editor,User: 内容完整\n Editor-->>-User: 完成\n");
    const auto light = render::mermaid(source), dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    const auto boxes = [](const QImage& image) {
      QList<QRect> result;
      QRect current;
      for (int y = 0; y < image.height(); ++y) {
        int first = -1, last = -1;
        for (int x = 0; x < image.width(); ++x) {
          if (image.pixelColor(x, y) != QColor("#fff5d0")) continue;
          if (first < 0) first = x;
          last = x;
        }
        if (first >= 0)
          current = current.united(QRect(first, y, last - first + 1, 1));
        else if (!current.isNull()) {
          result.append(current);
          current = {};
        }
      }
      if (!current.isNull()) result.append(current);
      return result;
    };
    const auto headerCenters = [](const QImage& image) {
      QList<int> centers;
      int first = -1;
      const int y = qRound(21 * image.devicePixelRatio());
      for (int x = 0; x <= image.width(); ++x) {
        const bool panel = x < image.width() && image.pixelColor(x, y) == QColor("#edf3fc");
        if (panel && first < 0) first = x;
        if (!panel && first >= 0) {
          centers.append((first + x - 1) / 2);
          first = -1;
        }
      }
      return centers;
    };
    const QString simple = QStringLiteral(
        "sequenceDiagram\n participant A\n participant B\n Note left of A: Left\n"
        " Note right of B: Right\n Note over B,A: Shared\n");
    const auto image = render::mermaid(simple).image;
    const auto notes = boxes(image);
    const auto centers = headerCenters(image);
    QCOMPARE(notes.size(), 3);
    QCOMPARE(centers.size(), 2);
    QVERIFY(notes[0].right() < centers[0]);
    QVERIFY(notes[1].left() > centers[1]);
    QVERIFY(notes[2].left() < centers[0] && notes[2].right() > centers[1]);
    for (const auto& rect : notes) QVERIFY(rect.left() >= 16 && rect.right() < image.width() - 16);
    auto reversed = simple;
    reversed.replace("over B,A", "over A,B");
    QCOMPARE(image, render::mermaid(reversed).image);
    auto variedCase = simple;
    variedCase.replace("Note left of", "nOtE LEFT   OF").replace("Note right of", "note RIGHT of");
    QCOMPARE(image, render::mermaid(variedCase).image);
    const auto active =
        render::mermaid(QStringLiteral("sequenceDiagram\n participant A\n participant B\n") +
                        QStringLiteral("activate A\n").repeated(8) +
                        "Note right of A: Active\nNote left of B: Waiting\n");
    const auto activeNotes = boxes(active.image);
    const auto activeCenters = headerCenters(active.image);
    QCOMPARE(activeNotes.size(), 2);
    QCOMPARE(activeCenters.size(), 2);
    for (const auto& rect : activeNotes)
      QVERIFY(rect.left() > activeCenters[0] && rect.right() < activeCenters[1]);
    const auto single = render::mermaid(QStringLiteral(
        "sequenceDiagram\n Note left of A: L\n Note right of A: R\n Note over A: Center\n"));
    const auto singleNotes = boxes(single.image);
    const auto singleCenters = headerCenters(single.image);
    QCOMPARE(singleNotes.size(), 3);
    QCOMPARE(singleCenters.size(), 1);
    QVERIFY(qAbs(singleNotes[2].center().x() - singleCenters[0]) <= 2);
    for (const auto& rect : boxes(single.image))
      QVERIFY(rect.left() >= 16 && rect.right() < single.image.width() - 16);
    const auto note = QStringLiteral("Note right of A: Details\n");
    QVERIFY(
        !render::mermaid(QStringLiteral("sequenceDiagram\n") + note.repeated(60)).image.isNull());
    QVERIFY(
        render::mermaid(QStringLiteral("sequenceDiagram\n") + note.repeated(61)).image.isNull());
    for (const auto& invalid : {
             QStringLiteral("Note left of A,B: Two actors"),
             QStringLiteral("Note right of A,B: Two actors"),
             QStringLiteral("Note right A: Missing of"),
             QStringLiteral("Note below A: Unknown position"),
             QStringLiteral("Note left of A Missing colon"),
             QStringLiteral("A->>B: Start\n destroy B\n A-xB: Stop\n Note right of B: Late"),
         }) {
      const auto result = render::mermaid(QStringLiteral("sequenceDiagram\n") + invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const auto markdown = QStringLiteral("```mermaid\n") + source + "```\n";
    ui::ViewSwitch view;
    ui::Editor edit;
    QVERIFY(view.loadMarkdownIntoBoth(markdown, nullptr));
    view.showActiveIn(&edit);
    const auto undo = edit.document()->availableUndoSteps();
    edit.setDarkTheme(true);
    edit.setDarkTheme(false);
    QCOMPARE(edit.document()->availableUndoSteps(), undo);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(same(markdown, view.currentMarkdown()));
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/notes-light.png"));
      QVERIFY(dark.image.save(retained + "/notes-dark.png"));
      QVERIFY(active.image.save(retained + "/notes-activations.png"));
    }
  }

  void sequenceMessagesDistinguishArrowheadsAndDirections() {
    const QString source = QStringLiteral(
        "sequenceDiagram\n actor User as 作者\n participant App as 编辑器\n"
        " participant Worker as 工作进程\n autonumber\n"
        " User->App: 无箭头线段\n App-->User: 虚线线段\n"
        " User->>+App: 同步调用\n App-->>-User: 返回结果\n"
        " User<<->>App: 双向通信\n App<<-->>User: 虚线双向通信\n"
        " App-)+Worker: 异步请求\n Worker--)-App: 异步完成\n"
        " User-xApp: 取消请求\n App--xUser: 丢弃响应\n App<<->>App: 双向自调用\n");
    const auto light = render::mermaid(source), dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    const auto diagram = [](const QString& token, const QString& from = QStringLiteral("A"),
                            const QString& to = QStringLiteral("B")) {
      return render::mermaid(QStringLiteral("sequenceDiagram\n participant A\n participant B\n ") +
                             from + token + to + ": Message\n")
          .image;
    };
    for (const auto& token :
         {"->", "-->", "->>", "-->>", "<<->>", "<<-->>", "-x", "--x", "-)", "--)"}) {
      QVERIFY2(!diagram(token).isNull(), token);
      QVERIFY2(!diagram(token, "B", "A").isNull(), token);
      QVERIFY2(!diagram(token, "A", "A").isNull(), token);
    }
    const auto difference = [](const QImage& a, const QImage& b) {
      QRect result;
      for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
          if (a.pixel(x, y) != b.pixel(x, y)) result = result.united(QRect(x, y, 1, 1));
      return result;
    };
    const auto ink = [](const QImage& image) {
      int count = 0;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
          count += image.pixelColor(x, y) == QColor("#34455e");
      return count;
    };
    const auto none = diagram("->"), filled = diagram("->>"), open = diagram("-)");
    const auto both = diagram("<<->>"), crossed = diagram("-x");
    for (const auto& image : {filled, open, both, crossed}) QCOMPARE(image.size(), none.size());
    QVERIFY(ink(filled) > ink(open) && ink(open) > ink(none));
    const auto receiver = difference(none, filled), sender = difference(filled, both);
    QVERIFY(!receiver.isEmpty() && receiver.left() > none.width() / 2);
    QVERIFY(!sender.isEmpty() && sender.right() < none.width() / 2);
    QVERIFY(crossed != none && crossed != open);
    const auto reverse = diagram("->>", "B", "A");
    const auto reversedReceiver = difference(diagram("->", "B", "A"), reverse);
    QVERIFY(!reversedReceiver.isEmpty() && reversedReceiver.right() < reverse.width() / 2);
    const auto dotted = diagram("-->"), dottedBoth = diagram("<<-->>");
    QVERIFY(dottedBoth != both && dotted != none);
    const auto ratio = dotted.devicePixelRatio();
    const QRect middle(qRound(240 * ratio), 0, qRound(30 * ratio), dotted.height());
    QCOMPARE(dotted.copy(middle), dottedBoth.copy(middle));
    const auto self = diagram("<<->>", "A", "A");
    const auto selfHeads = difference(diagram("->", "A", "A"), self);
    QVERIFY(selfHeads.height() > 20 * self.devicePixelRatio());
    QVERIFY(!render::mermaid(
                 QStringLiteral(
                     "sequenceDiagram\n create participant W\n A-)+W: Create\n W<<->>W: Work\n"
                     " destroy W\n W--)-A: Done\n"))
                 .image.isNull());
    for (const auto& invalid : {
             QStringLiteral("A<<->B: Missing arrowhead"),
             QStringLiteral("A-)++B: Invalid activation"),
             QStringLiteral("A--))B: Invalid arrow"),
             QStringLiteral("A<-B: Unsupported arrow"),
             QStringLiteral("A()->>B: Central connection"),
             QStringLiteral("A->>()B: Central connection"),
             QStringLiteral("A-)B: Start\n B--)-A: Not active"),
         }) {
      const auto result = render::mermaid(QStringLiteral("sequenceDiagram\n") + invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const auto markdown = QStringLiteral("```mermaid\n") + source + "```\n";
    ui::ViewSwitch view;
    ui::Editor edit;
    QVERIFY(view.loadMarkdownIntoBoth(markdown, nullptr));
    view.showActiveIn(&edit);
    const auto undo = edit.document()->availableUndoSteps();
    edit.setDarkTheme(true);
    edit.setDarkTheme(false);
    QCOMPARE(edit.document()->availableUndoSteps(), undo);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(same(markdown, view.currentMarkdown()));
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/arrows-light.png"));
      QVERIFY(dark.image.save(retained + "/arrows-dark.png"));
      QVERIFY(self.save(retained + "/arrows-self.png"));
    }
  }

  void sequenceCriticalOptionsAndBreaksKeepScope() {
    const QString source = QStringLiteral(
        "sequenceDiagram\n actor User as 作者\n participant App as 编辑器\n participant Disk as "
        "磁盘\n"
        " autonumber\n User->>+App: 保存文档\n critical 完整写入数据\n"
        " App->>+Disk: 写入正文\n Disk-->>-App: 完成写入\n option 存储空间不足\n"
        " break 停止本次保存\n App-->>User: 显示错误\n end\n option 访问被拒绝\n"
        " critical 刷新访问权限\n App->>Disk: 重新检查\n option 用户取消\n"
        " Note right of Disk: 保留未保存内容\n end\n end\n App-->>-User: 处理完成\n");
    const auto light = render::mermaid(source), dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    auto numbered = source;
    numbered.remove("autonumber\n");
    const QStringList labels = {"保存文档", "写入正文", "完成写入",
                                "显示错误", "重新检查", "处理完成"};
    for (int i = 0; i < labels.size(); ++i)
      numbered.replace(": " + labels.at(i) + '\n',
                       ": " + QString::number(i + 1) + ". " + labels.at(i) + '\n');
    QCOMPARE(light.image, render::mermaid(numbered).image);

    const auto body = QStringLiteral("A->>B: Write\n B-->>A: Done\n");
    const auto prefix = QStringLiteral("sequenceDiagram\n");
    const auto plain = render::mermaid(prefix + body);
    const auto critical = render::mermaid(prefix + "critical Write\n" + body + "end\n");
    const auto stopped = render::mermaid(prefix + "break Failure\n" + body + "end\n");
    QVERIFY(!critical.image.isNull() && !stopped.image.isNull());
    QVERIFY(critical.image.height() > plain.image.height());
    QVERIFY(stopped.image.height() > plain.image.height());
    QVERIFY(critical.image != stopped.image);
    const auto option = prefix + "critical Write\n" + body + "option Retry\n A->>B: Retry\nend\n";
    const auto withOption = render::mermaid(option);
    QVERIFY(!withOption.image.isNull());
    QVERIFY(withOption.image.height() > critical.image.height());
    auto longOption = option;
    longOption.replace("option Retry",
                       "option " + QStringLiteral("核对访问权限与磁盘剩余空间 ").repeated(12));
    const auto wrapped = render::mermaid(longOption);
    QVERIFY(!wrapped.image.isNull());
    QVERIFY(wrapped.image.height() > withOption.image.height());
    const auto explicitBreak =
        render::mermaid(prefix + "critical Write<br>atomically\n" + body + "end\n");
    QVERIFY(!explicitBreak.image.isNull());
    QVERIFY(explicitBreak.image.height() > critical.image.height());
    QVERIFY(!render::mermaid(prefix + "critical\n" + body + "option\n A->>B: Retry\nend\n")
                 .image.isNull());
    const auto deep = prefix + QStringLiteral("critical Nested\n").repeated(6) + body +
                      QStringLiteral("end\n").repeated(6);
    QVERIFY(!render::mermaid(deep).image.isNull());
    QVERIFY(render::mermaid(prefix + "break Extra\n" + deep.mid(prefix.size()) + "end\n")
                .image.isNull());
    const auto frame =
        QStringLiteral("critical Write\n A->>B: Write\n option Retry\n A->>B: Retry\n end\n");
    QVERIFY(!render::mermaid(prefix + frame.repeated(24)).image.isNull());
    QVERIFY(render::mermaid(prefix + frame.repeated(25)).image.isNull());
    for (const auto& invalid : {
             QStringLiteral("option Orphan\n A->>B: Retry"),
             QStringLiteral("critical Missing end\n A->>B: Write"),
             QStringLiteral("break Missing end\n A->>B: Stop"),
             QStringLiteral("critical Work\n A->>B: Write\n else Wrong\n B->>A: Reply\nend"),
             QStringLiteral("critical Work\n A->>B: Write\n and Wrong\n B->>A: Reply\nend"),
             QStringLiteral("break Work\n A->>B: Write\n option Wrong\n B->>A: Reply\nend"),
             QStringLiteral("loop Work\n A->>B: Write\n option Wrong\n B->>A: Reply\nend"),
             QStringLiteral(
                 "critical Work\n alt Nested\n A->>B: Write\n option Crossed scope\nend\nend"),
             QStringLiteral("critical Work\n A->>B: Write\nend\noption Too late\nB->>A: Reply"),
             QStringLiteral("break Work\n A->>B: Write\nend\nend"),
         }) {
      const auto result = render::mermaid(prefix + invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const auto markdown = QStringLiteral("```mermaid\n") + source + "```\n";
    ui::ViewSwitch view;
    ui::Editor edit;
    QVERIFY(view.loadMarkdownIntoBoth(markdown, nullptr));
    view.showActiveIn(&edit);
    const auto undo = edit.document()->availableUndoSteps();
    edit.setDarkTheme(true);
    edit.setDarkTheme(false);
    QCOMPARE(edit.document()->availableUndoSteps(), undo);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(same(markdown, view.currentMarkdown()));
    const auto longMarkdown = QStringLiteral("```mermaid\n") + longOption + "```\n";
    QTextDocument longDocument;
    QVERIFY(editor::loadMarkdown(longMarkdown, &longDocument, nullptr));
    QVERIFY(same(longMarkdown, editor::serializeDocument(&longDocument)));
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/critical-break-light.png"));
      QVERIFY(dark.image.save(retained + "/critical-break-dark.png"));
      QVERIFY(wrapped.image.save(retained + "/critical-break-long.png"));
    }
  }

  void sequenceHighlightsBlendAndKeepScope() {
    const QString source = QStringLiteral(
        "sequenceDiagram\n actor User as 作者\n participant App as 编辑器\n participant Disk as "
        "磁盘\n"
        " autonumber\n User->>+App: 保存文档\n rect rgba(80,160,240,.18)\n"
        " Note left of User: 核对内容\n critical 完整写入\n App->>+Disk: 写入正文\n"
        " rect rgba(180,100,220,.2)\n Disk-->>-App: 写入完成\n end\n"
        " option 空间不足\n Note right of Disk: 保留草稿\n end\n end\n"
        " App-->>-User: 保存完成\n");
    const auto light = render::mermaid(source), dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    auto numbered = source;
    numbered.remove("autonumber\n");
    const QStringList labels = {"保存文档", "写入正文", "写入完成", "保存完成"};
    for (int i = 0; i < labels.size(); ++i)
      numbered.replace(": " + labels.at(i) + '\n',
                       ": " + QString::number(i + 1) + ". " + labels.at(i) + '\n');
    QCOMPARE(light.image, render::mermaid(numbered).image);

    const auto prefix = QStringLiteral("sequenceDiagram\n participant A\n participant B\n");
    const auto diagram = [&](const QString& color, bool dark = false) {
      return render::mermaid(prefix + "A->>B: Before\n rect " + color +
                                 "\n A->>B: Highlight\n end\n A->>B: After\n",
                             dark)
          .image;
    };
    const auto colorBounds = [](const QImage& image, QColor color, int tolerance = 0) {
      QRect bounds;
      int count = 0;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
          const auto pixel = image.pixelColor(x, y);
          if (qAbs(pixel.red() - color.red()) > tolerance ||
              qAbs(pixel.green() - color.green()) > tolerance ||
              qAbs(pixel.blue() - color.blue()) > tolerance)
            continue;
          bounds = bounds.united(QRect(x, y, 1, 1));
          ++count;
        }
      return qMakePair(bounds, count);
    };
    const auto opaque = diagram("rgb(12,34,56)");
    QVERIFY(!opaque.isNull());
    QCOMPARE(opaque, diagram("rgba(12,34,56,1)"));
    QCOMPARE(opaque, diagram("RGB( 12 , 34 , 56 )"));
    QCOMPARE(diagram("rgba(12,34,56,0)"), diagram("rgba(90,100,110,0)"));
    const auto [bounds, count] = colorBounds(opaque, QColor(12, 34, 56));
    QVERIFY(count > 1000);
    QVERIFY(bounds.top() > opaque.height() / 4 && bounds.bottom() < opaque.height() * .8);
    int lifeline = 0;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y)
      lifeline +=
          opaque.pixelColor(qRound(147 * opaque.devicePixelRatio()), y) == QColor("#7e9cc3");
    QVERIFY(lifeline > 10);
    const auto inside = opaque.copy(bounds);
    QVERIFY(colorBounds(inside, QColor("#34455e")).second > 100);
    const auto translucent = diagram("rgba(255,0,0,.5)");
    const auto translucentDark = diagram("rgba(255,0,0,.5)", true);
    QVERIFY(colorBounds(translucent, QColor(255, 128, 128), 2).second > 1000);
    QVERIFY(colorBounds(translucentDark, QColor(147, 22, 25), 2).second > 1000);
    const auto nested = render::mermaid(prefix +
                                        "rect rgb(0,0,255)\n A->>B: Outer\n"
                                        "rect rgba(255,0,0,.5)\n A->>B: Inner\n end\n end\n");
    QVERIFY(!nested.image.isNull());
    QVERIFY(colorBounds(nested.image, QColor(0, 0, 255)).second > 1000);
    QVERIFY(colorBounds(nested.image, QColor(128, 0, 128), 2).second > 1000);
    const auto frame = QStringLiteral("rect rgba(80,160,240,.1)\n A->>B: Work\n end\n");
    QVERIFY(!render::mermaid(prefix + frame.repeated(24)).image.isNull());
    QVERIFY(render::mermaid(prefix + frame.repeated(25)).image.isNull());
    const auto deep = QStringLiteral("rect rgb(100,150,200)\n").repeated(6) + "A->>B: Work\n" +
                      QStringLiteral("end\n").repeated(6);
    QVERIFY(!render::mermaid(prefix + deep).image.isNull());
    QVERIFY(render::mermaid(prefix + "rect rgb(100,150,200)\n" + deep + "end\n").image.isNull());
    for (const auto& color :
         {"red", "#aabbcc", "rgb(256,0,0)", "rgb(-1,0,0)", "rgb(1.5,0,0)", "rgb(1,2,3,.5)",
          "rgba(1,2,3)", "rgba(1,2,3,1.1)", "rgba(1,2,3,-.1)", "rgba(1,2,3,nan)"})
      QVERIFY2(diagram(color).isNull(), color);
    for (const auto& invalid : {
             QStringLiteral("rect\n A->>B: Missing color\nend"),
             QStringLiteral("rect rgb(1,2,3)\n A->>B: Missing end"),
             QStringLiteral("rect rgb(1,2,3)\n A->>B: Work\n else Wrong\nend"),
             QStringLiteral("rect rgb(1,2,3)\n A->>B: Work\n and Wrong\nend"),
             QStringLiteral("rect rgb(1,2,3)\n A->>B: Work\n option Wrong\nend"),
             QStringLiteral(
                 "alt Work\nrect rgb(1,2,3)\n A->>B: Work\n else Crossed scope\nend\nend"),
         }) {
      const auto result = render::mermaid(prefix + invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const auto markdown = QStringLiteral("```mermaid\n") + source + "```\n";
    ui::ViewSwitch view;
    ui::Editor edit;
    QVERIFY(view.loadMarkdownIntoBoth(markdown, nullptr));
    view.showActiveIn(&edit);
    const auto undo = edit.document()->availableUndoSteps();
    edit.setDarkTheme(true);
    edit.setDarkTheme(false);
    QCOMPARE(edit.document()->availableUndoSteps(), undo);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(same(markdown, view.currentMarkdown()));
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/highlights-light.png"));
      QVERIFY(dark.image.save(retained + "/highlights-dark.png"));
      QVERIFY(nested.image.save(retained + "/highlights-nested.png"));
    }
  }

  void sequenceGroupsLabelParticipantsAndKeepBoundaries() {
    const QString source = QStringLiteral(
        "sequenceDiagram\n box rgba(80,160,240,.12) 写作端\n actor User as 作者\n"
        " participant App as 编辑器\n end\n box rgba(180,100,220,.14) 持久化\n"
        " participant Disk as 磁盘\n participant Task as 保存任务\n end\n autonumber\n"
        " User->>+App: 保存文档\n rect rgba(60,160,180,.1)\n critical 写入正文\n"
        " App->>+Disk: 写入文件\n Disk-->>-App: 完成写入\n end\n end\n"
        " create participant Task\n App-)+Task: 创建任务\n destroy Task\n"
        " Task--)-App: 任务结束\n App-->>-User: 已保存\n");
    const auto light = render::mermaid(source), dark = render::mermaid(source, true);
    QVERIFY2(!light.image.isNull(), qPrintable(light.error));
    QVERIFY2(!dark.image.isNull(), qPrintable(dark.error));
    QCOMPARE(light.image.size(), dark.image.size());
    QVERIFY(light.image != dark.image);
    auto numbered = source;
    numbered.remove("autonumber\n");
    const QStringList labels = {"保存文档", "写入文件", "完成写入",
                                "创建任务", "任务结束", "已保存"};
    for (int i = 0; i < labels.size(); ++i)
      numbered.replace(": " + labels.at(i) + '\n',
                       ": " + QString::number(i + 1) + ". " + labels.at(i) + '\n');
    QCOMPARE(light.image, render::mermaid(numbered).image);
    const auto prefix = QStringLiteral("sequenceDiagram\n");
    const auto diagram = [&](const QString& header) {
      return render::mermaid(prefix + "box " + header +
                             "\n participant A\n participant B\n end\n A->>B: Work\n")
          .image;
    };
    const auto transparent = diagram("Team");
    QVERIFY(!transparent.isNull());
    QCOMPARE(transparent, diagram("transparent Team"));
    QCOMPARE(diagram("Aqua Team"), diagram("rgb(0,255,255) Team"));
    QCOMPARE(diagram("rgb(20,40,60) Team"), diagram("rgba(20,40,60,1) Team"));
    QVERIFY(!diagram("transparent Aqua").isNull());
    QVERIFY(diagram("transparent Aqua") != diagram("Aqua"));
    QVERIFY(!diagram(QString()).isNull());
    const auto longTitle = QStringLiteral("写作与文档资源的分组说明 ").repeated(15);
    const auto wrapped = diagram("transparent " + longTitle);
    QVERIFY(!wrapped.isNull());
    QVERIFY(wrapped.height() > transparent.height());
    const auto adjacent =
        render::mermaid(prefix +
                        "box rgb(210,230,250) Left\n participant A\n end\n"
                        "box rgba(240,200,220,.5) Right\n participant B\n end\n A->>B: Work\n");
    QVERIFY(!adjacent.image.isNull());
    const auto colorBounds = [](const QImage& image, QColor color, int tolerance = 0) {
      QRect bounds;
      int count = 0;
      for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) {
          const auto pixel = image.pixelColor(x, y);
          if (qAbs(pixel.red() - color.red()) > tolerance ||
              qAbs(pixel.green() - color.green()) > tolerance ||
              qAbs(pixel.blue() - color.blue()) > tolerance)
            continue;
          bounds = bounds.united(QRect(x, y, 1, 1));
          ++count;
        }
      return qMakePair(bounds, count);
    };
    const auto left = colorBounds(adjacent.image, QColor(210, 230, 250));
    const auto right = colorBounds(adjacent.image, QColor(248, 228, 238), 2);
    QVERIFY(left.second > 1000 && right.second > 1000);
    QVERIFY(left.first.right() < right.first.left());
    QVERIFY(left.first.left() > 0 && right.first.right() < adjacent.image.width() - 1);
    int lifeline = 0;
    for (int y = left.first.center().y(); y < left.first.bottom() - 20; ++y)
      lifeline += adjacent.image.pixelColor(qRound(147 * adjacent.image.devicePixelRatio()), y) ==
                  QColor("#7e9cc3");
    QVERIFY(lifeline > 10);
    QVERIFY(!render::mermaid(prefix + "participant A\nparticipant B\nbox Team\n"
                                      "participant B\nparticipant A\nend\nA->>B: Work\n")
                 .image.isNull());
    auto groups = prefix;
    for (int i = 0; i < 8; ++i) groups += "box\nparticipant P" + QString::number(i) + "\nend\n";
    QVERIFY(!render::mermaid(groups + "P0->>P7: Work\n").image.isNull());
    QVERIFY(render::mermaid(groups + "box\nparticipant P8\nend\nP0->>P8: Work\n").image.isNull());
    for (const auto& invalid : {
             QStringLiteral("box Empty\nend\nA->>B: Work"),
             QStringLiteral("box Missing end\nparticipant A"),
             QStringLiteral("box Outer\nbox Inner\nparticipant A\nend\nend"),
             QStringLiteral("box Team\nparticipant A\nA->>B: Message inside\nend"),
             QStringLiteral("box One\nparticipant A\nend\nbox Two\nparticipant A\nend"),
             QStringLiteral(
                 "participant A\nparticipant Gap\nbox Team\nparticipant A\nparticipant B\nend"),
             QStringLiteral("critical Work\nbox Team\nparticipant A\nend\nend"),
             QStringLiteral("box rgb(256,0,0) Team\nparticipant A\nend"),
             QStringLiteral("box hsl(120,40%,50%) Team\nparticipant A\nend"),
             QStringLiteral("box #abcdef Team\nparticipant A\nend"),
         }) {
      const auto result = render::mermaid(prefix + invalid);
      QVERIFY2(result.image.isNull() && !result.error.isEmpty(), qPrintable(invalid));
    }
    const auto markdown = QStringLiteral("```mermaid\n") + source + "```\n";
    ui::ViewSwitch view;
    ui::Editor edit;
    QVERIFY(view.loadMarkdownIntoBoth(markdown, nullptr));
    view.showActiveIn(&edit);
    const auto undo = edit.document()->availableUndoSteps();
    edit.setDarkTheme(true);
    edit.setDarkTheme(false);
    QCOMPARE(edit.document()->availableUndoSteps(), undo);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&edit);
    QVERIFY(same(markdown, view.currentMarkdown()));
    QTextDocument longDocument;
    const auto longSource = "```mermaid\n" + prefix + "box transparent " + longTitle +
                            "\n participant A\n end\n A->>A: Work\n```\n";
    QVERIFY(editor::loadMarkdown(longSource, &longDocument, nullptr));
    QVERIFY(same(longSource, editor::serializeDocument(&longDocument)));
    const QString retained = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (!retained.isEmpty()) {
      QVERIFY(light.image.save(retained + "/groups-light.png"));
      QVERIFY(dark.image.save(retained + "/groups-dark.png"));
      QVERIFY(wrapped.save(retained + "/groups-long.png"));
      QVERIFY(adjacent.image.save(retained + "/groups-adjacent.png"));
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
    QTest::newRow("xychart") << QStringLiteral(
                                    "xychart-beta\n  x-axis [A, B, C]\n"
                                    "  y-axis Volume -5 --> 10\n"
                                    "  bar Actual [4, 7, -3]\n  line Target [6, 6, 2]\n")
                             << QStringLiteral("xychart\n  bar [1, missing]\n");
    QTest::newRow("state") << QStringLiteral(
                                  "stateDiagram-v2\n  state \"Reading mode\" as Read\n"
                                  "  [*] --> Read\n  Read --> Edit : Open\n"
                                  "  Edit --> Read : Save\n  Edit --> [*]\n")
                           << QStringLiteral(
                                  "stateDiagram\n  state Parent {\n    [*] --> Child\n  }\n");
    QTest::newRow("gantt") << QStringLiteral(
                                  "gantt\n  title Release plan\n  section Preparation\n"
                                  "  Draft :done, draft, 2026-11-19, 2d\n"
                                  "  section Distribution\n"
                                  "  Publish :milestone, after draft, 0d\n")
                           << QStringLiteral("gantt\n  Publish :after missing, 0d\n");
    QTest::newRow("class") << QStringLiteral(
                                  "classDiagram\n class Document[\"文档\"] {\n -String title\n"
                                  " +save()\n }\n Document <|-- MarkdownDocument\n"
                                  " Document \"1\" *-- \"1..*\" Section : contains\n")
                           << QStringLiteral(
                                  "classDiagram\n namespace Core {\n   class Document\n }\n");
    QTest::newRow("quadrant") << QStringLiteral(
                                     "quadrantChart\n title Priorities\n x-axis Less --> More\n"
                                     " y-axis Low --> High\n quadrant-2 Start here\n"
                                     " Review: [0.2, 0.8]\n Coedit: [0.7, 0.6]\n")
                              << QStringLiteral("quadrantChart\n Invalid: [2, 0.5]\n");
    QTest::newRow("mindmap") << QStringLiteral(
                                    "mindmap\n  root((写作计划))\n    Planning[准备]\n"
                                    "    ::icon(fa fa-book)\n      Review<br>examples\n"
                                    "    Drafting(写作)\n      Chapter 1\n")
                             << QStringLiteral("mindmap\n  Root\n    Child\n    :::custom\n");
    QTest::newRow("sankey") << QStringLiteral(
                                   "sankey-beta\nPlan,Prepare,12\nPlan,Draft,8\n"
                                   "Prepare,\"Review, edit\",12\nDraft,\"Review, edit\",8\n")
                            << QStringLiteral("sankey\nA,B,1\nB,A,1\n");
    QTest::newRow("gitgraph") << QStringLiteral(
                                     "gitGraph\ncommit id:\"Start\"\nbranch writing\n"
                                     "commit id:\"Draft\"\ncheckout main\n"
                                     "merge writing id:\"Release\" tag:\"v1.0\"\n")
                              << QStringLiteral("gitGraph\ncommit\ncheckout missing\n");
    QTest::newRow("sequence") << QStringLiteral(
                                     "sequenceDiagram\n actor Author as 作者\n"
                                     " participant Editor\n autonumber\n"
                                     " alt Modified\n loop Blocks\n Author->>Editor: Save\n end\n"
                                     " else Ready\n Editor-->>Author: Saved\n end\n"
                                     " opt Finish\n Note over Author,Editor: Done\n end\n")
                              << QStringLiteral("sequenceDiagram\n alt Condition\n A->>B: Hello\n");
    QTest::newRow("activation") << QStringLiteral(
                                       "sequenceDiagram\n actor Author\n participant Editor\n"
                                       " Author->>+Editor: Save\n"
                                       " loop Blocks\n Editor->>+Editor: Validate\n"
                                       " Editor-->>-Editor: Valid\n end\n"
                                       " Editor-->>-Author: Saved\n")
                                << QStringLiteral("sequenceDiagram\n A->>B: Call\n deactivate B\n");
    QTest::newRow("lifecycle")
        << QStringLiteral(
               "sequenceDiagram\n actor Author\n participant Editor\n"
               " Author->>Editor: Save\n create participant Task as Worker\n"
               " Editor->>+Task: Create\n create actor Reviewer\n"
               " Task->>Reviewer: Review\n destroy Task\n"
               " Task-->>-Editor: Done\n destroy Reviewer\n Editor-xReviewer: Stop\n")
        << QStringLiteral("sequenceDiagram\n create actor B\n B->>A: Wrong\n");
    QTest::newRow("sequence-note")
        << QStringLiteral(
               "sequenceDiagram\n participant A\n participant B\n"
               " Note left of A: Prepare<br>the document\n A->>+B: Save\n"
               " Note right of B: Writing\n Note over B,A: Keep the source\n B-->>-A: Done\n")
        << QStringLiteral("sequenceDiagram\n Note right of A,B: Invalid\n");
    QTest::newRow("sequence-arrows")
        << QStringLiteral(
               "sequenceDiagram\n participant A\n participant B\n"
               " A->B: No arrowhead\n A<<->>+B: Bidirectional\n"
               " B<<-->>B: Self call\n B--)-A: Async result\n"
               " create participant W\n A-)W: Create\n destroy W\n A--xW: Stop\n")
        << QStringLiteral("sequenceDiagram\n A-)++B: Invalid\n");
    QTest::newRow("sequence-critical-break")
        << QStringLiteral(
               "sequenceDiagram\n participant App\n participant Disk\n"
               " critical Write\n App->>+Disk: Save\n Disk-->>-App: Done\n"
               " option Full disk\n break Stop saving\n App->>App: Retain draft\n end\n"
               " option Retry\n App-)Disk: Retry\n end\n")
        << QStringLiteral(
               "sequenceDiagram\n critical Write\n App->>Disk: Save\n else Invalid\n end\n");
    QTest::newRow("sequence-highlights")
        << QStringLiteral(
               "sequenceDiagram\n participant A\n participant B\n"
               " rect rgb(210,230,250)\n A->>+B: Save\n"
               " rect rgba(180,100,220,.2)\n B-->>-A: Done\n end\n end\n")
        << QStringLiteral("sequenceDiagram\n rect rgb(256,0,0)\n A->>B: Invalid color\n end\n");
    QTest::newRow("sequence-groups")
        << QStringLiteral(
               "sequenceDiagram\n box rgba(80,160,240,.15) Writing\n actor A\n"
               " participant B\n end\n box transparent Storage\n participant Disk\n end\n"
               " A->>+B: Save\n critical Write\n B->>Disk: Write\n end\n B-->>-A: Done\n")
        << QStringLiteral("sequenceDiagram\n box Empty\n end\n A->>B: Work\n");
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
