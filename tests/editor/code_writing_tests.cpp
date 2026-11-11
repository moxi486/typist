#include "CodeEditing.h"
#include "CodeLineNumberGutter.h"
#include "Editor.h"
#include "ViewSwitch.h"
#include "markdown.h"

#include <QAbstractItemView>
#include <QClipboard>
#include <QComboBox>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QScrollBar>
#include <QTextList>
#include <QtTest>

using namespace typist;
namespace {
void prepare(ui::Editor& editor, ui::ViewSwitch& view, const QString& source) {
  QVERIFY(view.loadMarkdownIntoBoth(source, nullptr));
  view.showActiveIn(&editor);
  editor.resize(1000, 650);
  editor.show();
  editor.activateWindow();
  editor.raise();
  editor.setFocus();
  QTest::qWait(5);
}
QTextCursor at(ui::Editor& editor, int line, int column = 0) {
  QTextCursor cursor(editor.document()->findBlockByNumber(line));
  cursor.setPosition(cursor.position() + column);
  editor.setTextCursor(cursor);
  return cursor;
}
QString code(ui::Editor& editor) { return editor::codeRange(editor.textCursor()).text(); }
bool gutterHasInk(QWidget* gutter) {
  const auto image = gutter->grab().toImage();
  for (int y = 0; y < image.height(); ++y) {
    const QRgb background = image.pixel(0, y);
    for (int x = 1; x < image.width(); ++x)
      if (image.pixel(x, y) != background) return true;
  }
  return false;
}

ui::CodeLineNumberGutter* enableNumberGutter(ui::Editor& editor) {
  editor.setCodeLineNumbers(true);
  return dynamic_cast<ui::CodeLineNumberGutter*>(
      editor.findChild<QWidget*>(QStringLiteral("codeLineNumberGutter")));
}
}  // namespace
class CodeWritingTests : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { QApplication::setStyle(QStringLiteral("Fusion")); }
  void defaultsUseTheCreationPathAndKeepUndoAtomic() {
    using Creation = ui::Editor::CodeFenceCreation;
    ui::Editor editor;
    ui::ViewSwitch view;
    editor.setDefaultCodeLanguageResolver([](Creation creation) {
      return creation == Creation::Command ? QStringLiteral("python") : QStringLiteral("sql");
    });
    QSignalSpy chosen(&editor, &ui::Editor::codeLanguageChosen);
    prepare(editor, view, QStringLiteral("alpha\n\nbeta\n"));
    const auto original = view.currentMarkdown();
    QTextCursor selection(editor.document());
    selection.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    editor.setTextCursor(selection);
    editor.formatParagraph(editor::ParagraphStyle::Code);
    QCOMPARE(editor::codeRange(editor.textCursor()).language(), QStringLiteral("python"));
    QCOMPARE(editor.textCursor().blockFormat().property(editor::kCodeInfoProperty).toString(),
             QStringLiteral("python"));
    QCOMPARE(code(editor), QStringLiteral("alpha\nbeta\n"));
    QVERIFY(view.currentMarkdown().startsWith(QStringLiteral("```python\n")));
    QCOMPARE(chosen.size(), 0);
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(!editor.document()->isUndoAvailable());
    editor.redo();
    QVERIFY(view.currentMarkdown().startsWith(QStringLiteral("```python\n")));

    prepare(editor, view, {});
    QTest::keyClicks(&editor, "```");
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor::codeRange(editor.textCursor()).language(), QStringLiteral("sql"));
    QCOMPARE(chosen.size(), 0);
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("```"));

    prepare(editor, view, {});
    QTest::keyClicks(&editor, "~~~rust");
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor::codeRange(editor.textCursor()).language(), QStringLiteral("rust"));
    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.first().first().toString(), QStringLiteral("rust"));
    QVERIFY(!editor.setCodeLanguage(QStringLiteral("bad`label")));
    QCOMPARE(chosen.size(), 1);
    QVERIFY(editor.setCodeLanguage(QStringLiteral("cpp")));
    QCOMPARE(chosen.last().first().toString(), QStringLiteral("cpp"));
    editor.undo();
    QCOMPARE(editor::codeRange(editor.textCursor()).language(), QStringLiteral("rust"));
  }

  void commandPreservesExistingLanguageAndWrapsLiteralSource() {
    ui::Editor editor;
    ui::ViewSwitch view;
    editor.setDefaultCodeLanguageResolver(
        [](ui::Editor::CodeFenceCreation) { return QStringLiteral("python"); });
    prepare(editor, view, QStringLiteral("```ruby\nputs 42\n```\n"));
    editor.formatParagraph(editor::ParagraphStyle::Code);
    QCOMPARE(editor::codeRange(editor.textCursor()).language(), QStringLiteral("ruby"));

    const QString literal = QStringLiteral("Literal ``` marker\n");
    prepare(editor, view, literal);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    editor.moveCursor(QTextCursor::Start);
    editor.formatParagraph(editor::ParagraphStyle::Code);
    QCOMPARE(view.currentMarkdown(), QStringLiteral("````python\nLiteral ``` marker\n````\n"));
    const auto parsed = core::parseMarkdown(view.currentMarkdown().toStdString());
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.blocks.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(parsed.blocks.front().text), literal);
    editor.undo();
    QCOMPARE(view.currentMarkdown(), literal);

    editor.setDefaultCodeLanguageResolver(
        [](ui::Editor::CodeFenceCreation) { return QStringLiteral("bad`label"); });
    editor.moveCursor(QTextCursor::Start);
    editor.formatParagraph(editor::ParagraphStyle::Code);
    QVERIFY(view.currentMarkdown().startsWith(QStringLiteral("````\n")));
  }

  void lineNumbersFollowLogicalCodeLinesAndResetForEachFence() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view,
            QStringLiteral("Intro\n\n```text\n") + QString(126, QLatin1Char('m')) +
                QStringLiteral("\n\nlast\n```\n\nBody\n\n```text\nsecond fence\n```\n"));
    const auto markdown = view.currentMarkdown();
    const auto plain = editor.toPlainText();
    QVERIFY(!editor.findChild<QWidget*>(QStringLiteral("codeLineNumberGutter")));
    editor.resize(500, 650);
    auto* gutter = enableNumberGutter(editor);
    QVERIFY(gutter && gutter->isVisible());
    QCOMPARE(gutter->geometry().top(), editor.viewport()->geometry().top());
    QCOMPARE(gutter->geometry().bottom(), editor.viewport()->geometry().bottom());
    QVERIFY(gutter->geometry().right() < editor.viewport()->geometry().left());

    const auto first = editor.document()->find(QStringLiteral("mmmm")).block();
    const auto second = editor.document()->find(QStringLiteral("second fence")).block();
    QVERIFY(first.layout()->lineCount() > 1);
    QVERIFY(first.next().text().isEmpty());
    const QList<QTextBlock> numbered{first, first.next(), first.next().next(), second};
    const QList<int> expected{1, 2, 3, 1};
    const auto rows = gutter->visibleRows();
    QCOMPARE(rows.size(), expected.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
      QCOMPARE(rows[i].number, expected[i]);
      const auto caret = editor.cursorRect(QTextCursor(numbered[i]));
      QCOMPARE(rows[i].bounds.top(), caret.top());
      QCOMPARE(rows[i].bounds.height(), caret.height());
    }
    QVERIFY(gutterHasInk(gutter));

    editor.setTextCursor(QTextCursor(first));
    QVERIFY(editor.copyCodeBlock());
    QCOMPARE(QApplication::clipboard()->text(), QString(126, QLatin1Char('m')) + "\n\nlast\n");
    QCOMPARE(view.currentMarkdown(), markdown);
    QCOMPARE(editor.toPlainText(), plain);
    QVERIFY(!editor.document()->isUndoAvailable());

    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QVERIFY(gutter->isHidden());
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QVERIFY(gutter->isVisible());
    editor.setCodeLineNumbers(false);
    QVERIFY(gutter->isHidden());
    QCOMPARE(view.currentMarkdown(), markdown);
  }

  void lineNumbersStayAlignedWhenScrollingAndRenumberAfterEditing() {
    ui::Editor editor;
    ui::ViewSwitch view;
    QString source = QStringLiteral("```text\n");
    for (int i = 1; i <= 140; ++i) source += QStringLiteral("row %1\n").arg(i);
    source += QStringLiteral("```\n");
    prepare(editor, view, source);
    auto* gutter = enableNumberGutter(editor);
    QVERIFY(gutter && gutter->isVisible());
    QCOMPARE(gutter->visibleRows().first().number, 1);

    const auto target = at(editor, 120);
    editor.ensureCursorVisible();
    QVERIFY(editor.verticalScrollBar()->value() > 0);
    const auto currentNumber = [&] {
      const int top = editor.cursorRect(target).top();
      for (const auto& row : gutter->visibleRows())
        if (row.bounds.top() == top) return row.number;
      return 0;
    };
    QCOMPARE(currentNumber(), 121);
    auto* scroll = editor.verticalScrollBar();
    scroll->setValue(scroll->value() + 45);
    QCOMPARE(currentNumber(), 121);

    auto previous = target;
    previous.movePosition(QTextCursor::PreviousBlock);
    previous.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(previous);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(target.block().text(), QStringLiteral("row 121"));
    QCOMPARE(currentNumber(), 122);
    editor.undo();
    QCOMPARE(currentNumber(), 121);
  }

  void enterSplitsPairsAndKeepsOneUndoStep() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```cpp\nif (ready) {}\n```\n\nTail."));
    at(editor, 0, 12);
    const auto before = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(code(editor), QStringLiteral("if (ready) {\n    \n}\n"));
    QCOMPARE(editor.textCursor().blockNumber(), 1);
    QCOMPARE(editor.textCursor().positionInBlock(), 4);
    QCOMPARE(editor.textCursor().blockFormat().property(editor::kCodeInfoProperty).toString(),
             QStringLiteral("cpp"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
    editor.redo();
    at(editor, 1, 4);
    QTest::keyClicks(&editor, "return 42;");
    QVERIFY(code(editor).contains("    return 42;\n}"));
    QVERIFY(view.currentMarkdown().endsWith("Tail.\n\n"));
  }
  void pythonTabsCommentsStringsAndLiteralEnter() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```python\n\tif ready:\n```"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(code(editor), QStringLiteral("\tif ready:\n\t\t\n"));
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
    QCOMPARE(code(editor), QStringLiteral("\tif ready:\n\t\t\n\t\t\n"));
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(!editor.textCursor().blockFormat().hasProperty(editor::kCodeBlockProperty));
    prepare(editor, view, QStringLiteral("```cpp\n/* comment\n  if (x) {\n```"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.textCursor().block().text(), QStringLiteral("  "));
    prepare(editor, view, QStringLiteral("```python\ntext = \"\"\"start\n  literal {\n```"));
    editor.setSyntaxHighlight(false);
    editor.moveCursor(QTextCursor::End);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.textCursor().block().text(), QStringLiteral("  "));
    prepare(editor, view, QStringLiteral("```python\nif ready: # comment\n```"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.textCursor().block().text(), QString());
  }
  void tabUsesStopsAndBackspaceRemovesIndentOnly() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```cpp\n  value\n```"));
    at(editor, 0, 2);
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(code(editor), QStringLiteral("    value\n"));
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QCOMPARE(code(editor), QStringLiteral("value\n"));
    editor.undo();
    QCOMPARE(code(editor), QStringLiteral("    value\n"));
    QTest::keyClick(&editor, Qt::Key_Backtab);
    QCOMPARE(code(editor), QStringLiteral("value\n"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QCOMPARE(code(editor), QStringLiteral("valu\n"));
  }
  void multiLineIndentPreservesReverseSelectionAndUndo() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```python\none\n  two\nthree\n```\n\nTail."));
    const auto original = view.currentMarkdown();
    QTextCursor cursor(editor.document());
    cursor.setPosition(editor.document()->findBlockByNumber(2).position());
    cursor.setPosition(0, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(code(editor), QStringLiteral("    one\n      two\nthree\n"));
    QVERIFY(editor.textCursor().position() < editor.textCursor().anchor());
    QTest::keyClick(&editor, Qt::Key_Backtab);
    QCOMPARE(view.currentMarkdown(), original);
    editor.undo();
    QVERIFY(code(editor).startsWith("    one"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
  }
  void pairingSelectionAndHomeDoNotLeakRichFormatting() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```json\n    value\n```"));
    at(editor, 0, 4);
    QTest::keyClick(&editor, Qt::Key_Home);
    QCOMPARE(editor.textCursor().positionInBlock(), 0);
    QTest::keyClick(&editor, Qt::Key_Home);
    QCOMPARE(editor.textCursor().positionInBlock(), 4);
    QTextCursor cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    QTest::keyClicks(&editor, "\"");
    QCOMPARE(code(editor), QStringLiteral("    \"value\"\n"));
    QVERIFY(editor.textCursor().hasSelection());
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, "()");
    QVERIFY(code(editor).endsWith("\"()\n"));
    editor.setAutoPair(false);
    QTest::keyClicks(&editor, "[");
    QVERIFY(code(editor).endsWith("()[\n"));
  }
  void richClipboardAndMarkdownStayLiteralInsideCode() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```markdown\nBEFORE TAIL\n```\n\nAfter."));
    at(editor, 0, 7);
    const auto original = view.currentMarkdown();
    auto* mime = new QMimeData;
    mime->setText(QStringLiteral("# 标题\r\n\r\n**bold**\r\n- [ ] task"));
    mime->setHtml(QStringLiteral("<h1>wrong rich HTML</h1>"));
    QApplication::clipboard()->setMimeData(mime);
    editor.paste();
    QCOMPARE(code(editor), QStringLiteral("BEFORE # 标题\n\n**bold**\n- [ ] taskTAIL\n"));
    for (auto block = editor.document()->begin();
         block.blockFormat().hasProperty(editor::kCodeBlockProperty); block = block.next()) {
      QVERIFY(!block.textList());
      QCOMPARE(block.blockFormat().headingLevel(), 0);
      QCOMPARE(block.blockFormat().property(editor::kCodeInfoProperty).toString(),
               QStringLiteral("markdown"));
    }
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    at(editor, 0, 7);
    editor.insertPlainText(QStringLiteral("a\nb"));
    QCOMPARE(code(editor), QStringLiteral("BEFORE a\nbTAIL\n"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
  }
  void exitKeepsTrailingBlanksAndReusesBodyWithoutUndo() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```cpp\nint x;\n\n```\n\nTail."));
    const auto original = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(editor.textCursor().block().text(), QStringLiteral("Tail."));
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(!editor.document()->isUndoAvailable());
    prepare(editor, view, QStringLiteral("```cpp\nint x;\n```"));
    const auto onlyCode = view.currentMarkdown();
    QVERIFY(editor.exitCodeBlock());
    QVERIFY(!editor.textCursor().blockFormat().hasProperty(editor::kCodeBlockProperty));
    QTest::keyClicks(&editor, "normal");
    QVERIFY(!editor.textCursor().charFormat().fontFamilies().toStringList().contains("Consolas"));
    editor.undo();
    editor.undo();
    QCOMPARE(view.currentMarkdown(), onlyCode);
  }
  void toolbarCopyLanguageThemeAndViewSwitchAreSafe() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view,
            QStringLiteral("```cpp\nint first = 1;\n```\n\nBody.\n\n```json\n{}\n```"));
    auto* combo = editor.findChild<QComboBox*>(QStringLiteral("codeBlockLanguage"));
    auto* copy = editor.findChild<QPushButton*>(QStringLiteral("copyCodeBlock"));
    auto* bar = editor.findChild<QFrame*>(QStringLiteral("codeBlockBar"));
    QVERIFY(combo && copy && bar && bar->isVisible());
    QVERIFY(bar->geometry().bottom() < editor.viewport()->geometry().top());
    const auto original = view.currentMarkdown();
    QTest::mouseClick(copy, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("int first = 1;\n"));
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(!editor.document()->isUndoAvailable());
    editor.setDarkTheme(true);
    editor.resize(760, 540);
    QTest::qWait(10);
    QVERIFY(bar->isVisible());
    QVERIFY(bar->geometry().right() <= editor.width());
    combo->setFocus();
    combo->lineEdit()->selectAll();
    QTest::keyClicks(combo->lineEdit(), "custom-lang");
    QTest::keyClick(combo->lineEdit(), Qt::Key_Return);
    QVERIFY(view.currentMarkdown().startsWith("```custom-lang"));
    QVERIFY(view.currentMarkdown().contains("```json\n{}"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    at(editor, 0);
    combo->setFocus();
    combo->lineEdit()->selectAll();
    QTest::keyClicks(combo->lineEdit(), "discarded");
    QTest::keyClick(combo->lineEdit(), Qt::Key_Escape);
    QCOMPARE(view.currentMarkdown(), original);
    QCOMPARE(combo->currentText(), QStringLiteral("cpp"));
    combo->setFocus();
    QTest::qWait(20);
    combo->showPopup();
    QTRY_VERIFY(combo->view()->isVisible());
    const auto index = combo->model()->index(combo->findData("python"), 0);
    combo->view()->scrollTo(index);
    QTest::mouseClick(combo->view()->viewport(), Qt::LeftButton, Qt::NoModifier,
                      combo->view()->visualRect(index).center());
    QVERIFY(view.currentMarkdown().startsWith("```python\nint first = 1;\n```"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    at(editor, 1);
    QVERIFY(!bar->isVisible());
    at(editor, 2);
    QTest::mouseClick(copy, Qt::LeftButton);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("{}\n"));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QVERIFY(!bar->isVisible());
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QCOMPARE(view.currentMarkdown(), original);
  }
  void replacingCodeSelectionAndEmptyCopyKeepSemanticSource() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```cpp\n  before\n  after\n```"));
    const auto original = view.currentMarkdown();
    QTextCursor cursor(editor.document());
    cursor.setPosition(4);
    cursor.setPosition(11, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(code(editor), QStringLiteral("  be\n  after\n"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    prepare(editor, view, QStringLiteral("```cpp\n```"));
    QVERIFY(editor.copyCodeBlock());
    QCOMPARE(QApplication::clipboard()->text(), QString());
    QVERIFY(!editor.document()->isUndoAvailable());
    QVERIFY(!editor.setCodeLanguage("bad```"));
    QVERIFY(editor.setCodeLanguage("cpp"));
    QVERIFY(!editor.document()->isUndoAvailable());
  }
  void convertedParagraphsKeepMonospaceAndInteriorBlankLines() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("original\n\nTail."));
    editor.formatParagraph(editor::ParagraphStyle::Code);
    editor.moveCursor(QTextCursor::EndOfBlock);
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "second");
    QVERIFY(editor.textCursor().charFormat().fontFamilies().toStringList().contains("Consolas"));
    editor.moveCursor(QTextCursor::Start);
    editor.formatParagraph(editor::ParagraphStyle::Body);
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "body");
    QVERIFY(!editor.textCursor().charFormat().fontFamilies().toStringList().contains("Consolas"));
    prepare(editor, view, QStringLiteral("```cpp\n{\n    \n}\n```\n\nTail."));
    at(editor, 1, 4);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(code(editor), QStringLiteral("{\n    \n    \n}\n"));
    QVERIFY(view.currentMarkdown().endsWith("Tail.\n\n"));
  }
  void pastedFencesSurviveSavingReloadingAndViewSwitch() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("```markdown\n```\n\nTail."));
    const auto original = view.currentMarkdown();
    const QString example = QStringLiteral("## 示例\n\n```cpp\nint answer = 42;\n```\n");
    editor.insertPlainText(example);
    const auto draft = view.currentMarkdown();
    QVERIFY(draft.startsWith("````markdown\n"));
    const auto parsed = core::parseMarkdown(draft.toStdString());
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.blocks.size(), std::size_t(2));
    QCOMPARE(QString::fromStdString(parsed.blocks.front().text), example + '\n');
    QTextDocument reloaded;
    QVERIFY(editor::loadMarkdown(draft, &reloaded, nullptr));
    const auto round = core::parseMarkdown(editor::serializeDocument(&reloaded).toStdString());
    QVERIFY(round.ok && core::astEqual(parsed.blocks, round.blocks));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QVERIFY(editor.toPlainText().contains("````markdown"));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QCOMPARE(view.currentMarkdown(), draft);
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
  }
};
QTEST_MAIN(CodeWritingTests)
#include "code_writing_tests.moc"
