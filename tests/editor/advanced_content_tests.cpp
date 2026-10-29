#include "DocxWriter.h"
#include "Editor.h"
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
    QVERIFY(html.contains(QStringLiteral("href=\"#heading-2\"")));
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
