#include "CodeEditing.h"
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
}  // namespace
class CodeWritingTests : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { QApplication::setStyle(QStringLiteral("Fusion")); }
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
