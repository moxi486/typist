#include "DocxWriter.h"
#include "Editor.h"
#include "PdfWriter.h"
#include "SpecialContent.h"
#include "SyntaxHighlighter.h"
#include "ViewSwitch.h"
#include "markdown.h"

#include <QFile>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextLayout>
#include <QtTest>

using namespace typist;
namespace {
bool same(const QString& a, const QString& b) {
  const auto lhs = core::parseMarkdown(a.toStdString()), rhs = core::parseMarkdown(b.toStdString());
  return lhs.ok && rhs.ok && core::astEqual(lhs.blocks, rhs.blocks);
}
QColor colorAt(const QTextBlock& block, int position) {
  for (const auto& range : block.layout()->formats())
    if (position >= range.start && position < range.start + range.length)
      return range.format.foreground().color();
  return {};
}
QPlainTextEdit* sourceInput(ui::Editor& editor) {
  return editor.findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
}
void prepare(ui::Editor& editor, ui::ViewSwitch& view, const QString& source) {
  QVERIFY(view.loadMarkdownIntoBoth(source, nullptr));
  view.showActiveIn(&editor);
  editor.resize(1000, 650);
  editor.show();
  editor.setFocus();
  QTest::qWait(5);
}
}  // namespace
class SyntaxParagraphTests : public QObject {
  Q_OBJECT
 private slots:
  void lexerRespectsStringsCommentsAndMultilineState() {
    render::SyntaxState state;
    const QString text = QStringLiteral("const char* url = \"https://example.com\"; // int 42");
    const auto spans = render::codeSyntax(text, "cpp", state);
    bool keyword = false, string = false, comment = false;
    for (const auto& span : spans) {
      keyword |= span.token == render::SyntaxToken::Keyword;
      string |= span.token == render::SyntaxToken::String &&
                text.mid(span.start, span.length).contains("https://");
      comment |=
          span.token == render::SyntaxToken::Comment && text.mid(span.start).startsWith("// int");
    }
    QVERIFY(keyword);
    QVERIFY(string);
    QVERIFY(comment);
    render::codeSyntax("/* begin", "cpp", state);
    QVERIFY(state.comment);
    const auto continued = render::codeSyntax("comment */ int count = 42;", "cpp", state);
    QVERIFY(!state.comment);
    QCOMPARE(continued.first().token, render::SyntaxToken::Comment);
    render::codeSyntax("text = \"\"\"start", "python", state);
    QCOMPARE(state.delimiter, QStringLiteral("\"\"\""));
    const auto triple = render::codeSyntax("# not a comment\"\"\" + 1", "python", state);
    QCOMPARE(triple.first().token, render::SyntaxToken::String);
    QVERIFY(state.delimiter.isEmpty());
    render::codeSyntax("auto raw = R\"tag(first", "cpp", state);
    QCOMPARE(state.delimiter, QStringLiteral(")tag\""));
    render::codeSyntax("second)tag\";", "cpp", state);
    QVERIFY(state.delimiter.isEmpty());
    QVERIFY(render::codeSyntax("int fake = 42;", "unknown-language", state).isEmpty());
    QVERIFY(render::codeSyntax(QString(40000, 'x'), "cpp", state).isEmpty());
  }
  void transientHighlightDoesNotChangeMarkdownUndoOrDirtyState() {
    ui::ViewSwitch view;
    ui::Editor editor;
    const QString source = QStringLiteral(
        "# A **title**\n\n```cpp\nconst int answer = 42;\n/* note\ncontinues */ return "
        "answer;\n```\n\nA **bold** paragraph.\n");
    prepare(editor, view, source);
    auto* document = editor.document();
    document->setModified(false);
    const int undo = document->availableUndoSteps();
    const QString before = view.currentMarkdown();
    auto* highlighter = render::installSyntaxHighlighter(document, false);
    highlighter->rehighlight();
    auto block = document->findBlockByNumber(1);
    QVERIFY(block.text().startsWith("const"));
    QCOMPARE(colorAt(block, 0), render::syntaxColor(render::SyntaxToken::Keyword, false));
    QCOMPARE(colorAt(block, block.text().indexOf("42")),
             render::syntaxColor(render::SyntaxToken::Number, false));
    editor.setDarkTheme(true);
    QCOMPARE(colorAt(block, 0), render::syntaxColor(render::SyntaxToken::Keyword, true));
    editor.setSyntaxHighlight(false);
    QVERIFY(block.layout()->formats().isEmpty());
    editor.setSyntaxHighlight(true);
    QCOMPARE(document->availableUndoSteps(), undo);
    QVERIFY(!document->isModified());
    QCOMPARE(view.currentMarkdown(), before);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QTest::qWait(5);
    auto sourceBlock = editor.document()->findBlockByNumber(0);
    QVERIFY(!sourceBlock.layout()->formats().isEmpty());
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QVERIFY(same(source, view.currentMarkdown()));
  }
  void fencedSourceStateAndIndependentCodeGroups() {
    QTextDocument source;
    source.setPlainText(
        QStringLiteral("````python\ndef work():\n```\n    return 1\n````\n# Heading\n"));
    render::SyntaxHighlighter syntax(&source, true);
    syntax.rehighlight();
    QCOMPARE(colorAt(source.findBlockByNumber(3), 4),
             render::syntaxColor(render::SyntaxToken::Keyword, false));
    QCOMPARE(colorAt(source.findBlockByNumber(5), 0),
             render::syntaxColor(render::SyntaxToken::Markup, false));
    QTextDocument rendered;
    QVERIFY(editor::loadMarkdown(
        QStringLiteral("```cpp\n/* unclosed\n```\n\n```cpp\nint value = 1;\n```\n"), &rendered,
        nullptr));
    render::SyntaxHighlighter code(&rendered, false);
    code.rehighlight();
    auto last = rendered.lastBlock();
    QCOMPARE(colorAt(last, 0), render::syntaxColor(render::SyntaxToken::Keyword, false));
    source.setPlainText(QStringLiteral("```json\n") + QString(40000, 'x') +
                        QStringLiteral("\n```\n# Heading\n"));
    syntax.rehighlight();
    QVERIFY(source.findBlockByNumber(1).layout()->formats().isEmpty());
    QCOMPARE(colorAt(source.findBlockByNumber(3), 0),
             render::syntaxColor(render::SyntaxToken::Markup, false));
  }
  void emptyFencesAndLiveCodeKeepTheirLanguage() {
    for (const auto& source : {QStringLiteral("```cpp\n```\n"), QStringLiteral("```cpp\n\n```\n"),
                               QStringLiteral("```\n```\n")}) {
      const auto parsed = core::parseMarkdown(source.toStdString());
      QVERIFY(same(source, QString::fromStdString(core::serializeMarkdown(parsed.blocks))));
      QTextDocument document;
      QVERIFY(editor::loadMarkdown(source, &document, nullptr));
      QVERIFY2(same(source, editor::serializeDocument(&document)),
               qPrintable(editor::serializeDocument(&document)));
    }
    ui::ViewSwitch emptyView;
    ui::Editor emptyEditor;
    prepare(emptyEditor, emptyView, QStringLiteral("```cpp\n```\n"));
    QCOMPARE(emptyEditor.document()->availableUndoSteps(), 0);
    QTest::keyClicks(&emptyEditor, "value");
    QVERIFY(emptyEditor.document()
                ->begin()
                .begin()
                .fragment()
                .charFormat()
                .fontFamilies()
                .toStringList()
                .contains(QStringLiteral("Consolas")));
    ui::ViewSwitch view;
    ui::Editor editor;
    prepare(editor, view, QString());
    QTest::keyClicks(&editor, "```python");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "def greet(name):");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "return name");
    QTest::qWait(5);
    QCOMPARE(editor.textCursor().blockFormat().property(editor::kCodeInfoProperty).toString(),
             QStringLiteral("python"));
    QCOMPARE(colorAt(editor.textCursor().block(), 4),
             render::syntaxColor(render::SyntaxToken::Keyword, false));
  }
  void codeLanguageChangesWholeFenceAndUndoesOnce() {
    ui::ViewSwitch view;
    ui::Editor editor;
    prepare(editor, view, QStringLiteral("```cpp\nint value = 42;\nreturn value;\n```\n\nTail.\n"));
    editor.moveCursor(QTextCursor::Start);
    const QString before = view.currentMarkdown();
    QVERIFY(editor.setCodeLanguage("python"));
    QVERIFY(view.currentMarkdown().startsWith("```python"));
    QCOMPARE(editor.document()
                 ->begin()
                 .next()
                 .blockFormat()
                 .property(editor::kCodeInfoProperty)
                 .toString(),
             QStringLiteral("python"));
    editor.undo();
    QVERIFY(same(before, view.currentMarkdown()));
  }
  void paragraphSourceAppliesNestedMarkupAndPreservesNeighbors() {
    ui::ViewSwitch view;
    ui::Editor editor;
    const QString source = QStringLiteral("First **bold** and *italic*.\n\nSecond stays.\n");
    prepare(editor, view, source);
    const int undo = editor.document()->availableUndoSteps();
    QVERIFY(editor.startParagraphSourceEdit());
    auto* input = sourceInput(editor);
    QVERIFY(input);
    QVERIFY(input->toPlainText().contains("**bold**"));
    QCOMPARE(editor.document()->availableUndoSteps(), undo);
    input->setPlainText(
        QStringLiteral("First **bold with *nested italic*** and [a link](https://example.com)."));
    QVERIFY(same(source, view.currentMarkdown()));
    QTest::keyClick(input, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(!editor.paragraphSourceActive());
    const QString expected = QStringLiteral(
        "First **bold with *nested italic*** and [a link](https://example.com).\n\nSecond "
        "stays.\n");
    QVERIFY2(same(expected, view.currentMarkdown()), qPrintable(view.currentMarkdown()));
    editor.undo();
    QVERIFY(same(source, view.currentMarkdown()));
    editor.redo();
    QVERIFY(same(expected, view.currentMarkdown()));
  }
  void cancelAndSaveWhileEditingKeepDraftSafe() {
    ui::ViewSwitch view;
    ui::Editor editor;
    prepare(editor, view, QStringLiteral("Original **word**.\n\nTail."));
    const QString before = view.currentMarkdown();
    QVERIFY(editor.startParagraphSourceEdit());
    sourceInput(editor)->setPlainText(QStringLiteral("Discarded draft."));
    QTest::keyClick(sourceInput(editor), Qt::Key_Escape);
    QCOMPARE(view.currentMarkdown(), before);
    QVERIFY(!editor.document()->isUndoAvailable());
    QVERIFY(editor.startParagraphSourceEdit());
    sourceInput(editor)->setPlainText(QStringLiteral("Saved **snapshot**."));
    QVERIFY(editor.flushParagraphSourceEdit());
    QVERIFY(editor.paragraphSourceActive());
    QVERIFY(view.currentMarkdown().contains("Saved **snapshot**"));
    sourceInput(editor)->setPlainText(QStringLiteral("Saved **snapshot** with *more*."));
    editor.setDarkTheme(true);
    QVERIFY(editor.paragraphSourceActive());
    editor.finishParagraphSourceEdit();
    QVERIFY(view.currentMarkdown().contains("with *more*"));
    editor.undo();
    QVERIFY(view.currentMarkdown().contains("Saved **snapshot**."));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
  }
  void paragraphCanChangeStructureAndClearItself() {
    ui::ViewSwitch view;
    ui::Editor editor;
    prepare(editor, view, QStringLiteral("Original.\n\nTail."));
    QVERIFY(editor.startParagraphSourceEdit());
    sourceInput(editor)->setPlainText(
        QStringLiteral("## New **heading**\n\nNew paragraph with $x^2$."));
    editor.finishParagraphSourceEdit();
    QVERIFY2(same(QStringLiteral("## New **heading**\n\nNew paragraph with $x^2$.\n\nTail."),
                  view.currentMarkdown()),
             qPrintable(view.currentMarkdown()));
    editor.undo();
    editor.moveCursor(QTextCursor::Start);
    QVERIFY(editor.startParagraphSourceEdit());
    sourceInput(editor)->clear();
    editor.finishParagraphSourceEdit();
    QVERIFY(same(QStringLiteral("Tail."), view.currentMarkdown()));
    editor.undo();
    QVERIFY(same(QStringLiteral("Original.\n\nTail."), view.currentMarkdown()));
    QVERIFY(view.loadMarkdownIntoBoth(QStringLiteral("## Heading **bold**\n\nTail."), nullptr));
    view.showActiveIn(&editor);
    editor.moveCursor(QTextCursor::Start);
    QVERIFY(editor.startParagraphSourceEdit());
    sourceInput(editor)->setPlainText(QStringLiteral("Body **bold**."));
    editor.finishParagraphSourceEdit();
    QCOMPARE(editor.document()->begin().blockFormat().headingLevel(), 0);
    QVERIFY(same(QStringLiteral("Body **bold**.\n\nTail."), view.currentMarkdown()));
  }
  void draftSnapshotDoesNotEditModelOrUndoHistory() {
    ui::ViewSwitch view;
    ui::Editor editor;
    prepare(editor, view, QStringLiteral("Original **paragraph**.\n\nTail."));
    const QString original = view.currentMarkdown();
    QVERIFY(editor.startParagraphSourceEdit());
    auto* input = sourceInput(editor);
    input->selectAll();
    QTest::keyClicks(input, "Pending **bold with *nested*** source.");
    QSignalSpy changes(editor.document(), &QTextDocument::contentsChanged);
    const QString snapshot = editor.markdownWithParagraphDraft();
    QVERIFY(same(QStringLiteral("Pending **bold with *nested*** source.\n\nTail."), snapshot));
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(!editor.document()->isUndoAvailable());
    QCOMPARE(changes.size(), 0);
    QVERIFY(editor.undoParagraphSource());
    QVERIFY(input->document()->isRedoAvailable());
    QVERIFY(editor.undoParagraphSource(true));
    QCOMPARE(editor.markdownWithParagraphDraft(), snapshot);
    QTest::keyClick(input, Qt::Key_Escape);
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(!editor.document()->isUndoAvailable());
  }
  void clickingAnotherParagraphAppliesAndKeepsCaretThere() {
    ui::ViewSwitch view;
    ui::Editor editor;
    QString source = QStringLiteral("First **paragraph**.\n\n");
    for (int i = 1; i <= 8; ++i) source += QStringLiteral("Paragraph %1.\n\n").arg(i);
    prepare(editor, view, source);
    QVERIFY(editor.startParagraphSourceEdit());
    sourceInput(editor)->setPlainText(QStringLiteral("Changed **paragraph**."));
    const QTextBlock target = editor.document()->findBlockByNumber(5);
    const QRect rect = editor.cursorRect(QTextCursor(target));
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(rect.left() + 6, rect.center().y()));
    QTest::qWait(10);
    QVERIFY(!editor.paragraphSourceActive());
    QCOMPARE(editor.textCursor().block().text(), QStringLiteral("Paragraph 5."));
    QVERIFY(view.currentMarkdown().contains(QStringLiteral("Changed **paragraph**.")));
  }
  void tableAndListEditingUseTheirExistingCommands() {
    ui::ViewSwitch view;
    ui::Editor editor;
    prepare(editor, view, QStringLiteral("- item **one**\n\n```cpp\nint a = 1;\n```\n"));
    QVERIFY(!editor.startParagraphSourceEdit());
    editor.moveCursor(QTextCursor::End);
    QVERIFY(!editor.startParagraphSourceEdit());
  }
  void exportsUseSameTokensAndEscapeHtml() {
    const QString source = QStringLiteral(
        "# Code / 代码\n\n```cpp\n#include <string>\nconst int answer = 42; // a "
        "comment\nstd::string url = \"https://example.com\";\n```\n\n```python\ndef hello(name):\n "
        "   return \"你好, \" + name\n```\n");
    const QString html = render::html(source);
    QVERIFY(html.contains("color:#8b3d91"));
    QVERIFY(html.contains("&lt;"));
    QVERIFY(html.contains("&gt;"));
    QVERIFY(!html.contains("<string>"));
    const QString markdown =
        QStringLiteral("# **Heading** with `code` and [link](https://example.com)");
    QTextDocument fragment;
    fragment.setHtml("<pre>" + render::highlightedCodeHtml(markdown, "markdown") + "</pre>");
    QCOMPARE(fragment.toPlainText(), markdown);
    QTemporaryDir temp;
    const QString retained = qEnvironmentVariable("TYPIST_SYNTAX_OUTPUT"),
                  out = retained.isEmpty() ? temp.path() : retained;
    QDir().mkpath(out);
    QFile fixture(out + "/syntax-sample.md");
    QVERIFY(fixture.open(QIODevice::WriteOnly));
    fixture.write(source.toUtf8());
    fixture.close();
    QFile web(out + "/syntax-sample.html");
    QVERIFY(web.open(QIODevice::WriteOnly));
    web.write(QByteArray::fromStdString(core::wrapHtmlDocument(html.toStdString(), "Code")));
    web.close();
    QString error;
    QVERIFY2(platform::exportDocx(out + "/syntax-sample.docx", source, &error), qPrintable(error));
    QFile docx(out + "/syntax-sample.docx");
    QVERIFY(docx.open(QIODevice::ReadOnly));
    QVERIFY(docx.readAll().contains("w:color w:val=\"8b3d91\""));
    QTextDocument document;
    QVERIFY(editor::loadMarkdown(source, &document, &error));
    QVERIFY2(platform::exportPdf(out + "/syntax-sample.pdf", &document, &error), qPrintable(error));
  }
};
QTEST_MAIN(SyntaxParagraphTests)
#include "syntax_paragraph_tests.moc"
