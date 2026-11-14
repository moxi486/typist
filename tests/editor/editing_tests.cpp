#include "DocxWriter.h"
#include "Editing.h"
#include "Editor.h"
#include "LinkEditing.h"
#include "Search.h"
#include "ViewSwitch.h"
#include "markdown.h"

#include <QClipboard>
#include <QFile>
#include <QImage>
#include <QInputMethodEvent>
#include <QTemporaryDir>
#include <QTextDocumentFragment>
#include <QTextList>
#include <QTextTable>
#include <QtTest>

class EditingTests : public QObject {
  Q_OBJECT
 private:
  QString markdown(const typist::ui::Editor& editor) {
    return typist::editor::serializeDocument(editor.document());
  }
  void prepare(typist::ui::Editor& editor, typist::ui::ViewSwitch& view,
               const QString& source = QString()) {
    QVERIFY(view.loadMarkdownIntoBoth(source, nullptr));
    view.showActiveIn(&editor);
    editor.resize(1000, 700);
    editor.show();
    editor.setFocus();
    QTest::qWait(5);
  }
 private slots:
  void liveHeadingAndUndo() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    QTest::keyClicks(&editor, "##");
    QTest::keyClick(&editor, Qt::Key_Space);
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 2);
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("##"));
    QTest::keyClick(&editor, Qt::Key_Space);
    QTest::keyClicks(&editor, "Heading");
    QVERIFY(markdown(editor).startsWith(QStringLiteral("## Heading")));
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(editor.textCursor().blockFormat().headingLevel(), 0);
    QTest::keyClicks(&editor, "Body");
    QVERIFY(markdown(editor).contains(QStringLiteral("\n\nBody")));
    QVERIFY(
        !editor.textCursor().charFormat().boolProperty(typist::editor::kPresentationBoldProperty));
  }

  void liveInlineFormattingAndUndo() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    QTest::keyClicks(&editor, "**bold**");
    QCOMPARE(editor.toPlainText(), QStringLiteral("bold"));
    QVERIFY(markdown(editor).contains(QStringLiteral("**bold**")));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("**bold**"));
    editor.redo();
    QTest::keyClicks(&editor, " and *italic* and `code` and ~~gone~~");
    QCOMPARE(editor.toPlainText(), QStringLiteral("bold and italic and code and gone"));
    const QString source = markdown(editor);
    QVERIFY2(source.contains(QStringLiteral("**bold** and *italic* and `code` and ~~gone~~")),
             qPrintable(source));
    QVERIFY(!editor.textCursor().charFormat().fontItalic());
    QVERIFY(!editor.textCursor().charFormat().fontStrikeOut());
  }

  void underlineTogglePreservesInlineStylesAndLinks() {
    using typist::editor::InlineStyle;
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view, QStringLiteral("**Bold** and [link](https://example.org) after.\n"));
    const auto original = markdown(editor);
    const auto plain = editor.toPlainText();
    auto cursor = editor.document()->find(QStringLiteral("Bold and link"));
    QVERIFY(cursor.hasSelection());
    const int end = cursor.selectionEnd();
    const int start = cursor.selectionStart();
    cursor.setPosition(end);
    cursor.setPosition(start, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    editor.formatInline(InlineStyle::Underline);
    QCOMPARE(editor.textCursor().position(), start);
    QCOMPARE(editor.textCursor().anchor(), end);
    QCOMPARE(editor.toPlainText(), plain);
    QVERIFY(markdown(editor).startsWith(
        QStringLiteral("<u>**Bold** and [link](https://example.org)</u> after.")));
    auto link = editor.document()->find(QStringLiteral("link"));
    QVERIFY(link.charFormat().boolProperty(typist::editor::kUnderlineProperty));
    QVERIFY(typist::editor::removeLink(link));
    QVERIFY(link.charFormat().fontUnderline());
    QVERIFY(!link.charFormat().isAnchor());
    editor.undo();
    editor.setTextCursor(cursor);
    editor.formatInline(InlineStyle::Underline);
    QCOMPARE(markdown(editor), original);
    link = editor.document()->find(QStringLiteral("link"));
    QVERIFY(link.charFormat().fontUnderline());
    QVERIFY(!link.charFormat().boolProperty(typist::editor::kUnderlineProperty));
    editor.undo();
    QVERIFY(markdown(editor).startsWith(QStringLiteral("<u>")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
  }

  void underlineLiveMarkupAndLiteralCode() {
    using typist::editor::InlineStyle;
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    QTest::keyClicks(&editor, "<u>**underlined**</u> next");
    QCOMPARE(editor.toPlainText(), QStringLiteral("underlined next"));
    const auto expected = typist::core::parseMarkdown("<u>**underlined**</u> next");
    const auto actual = typist::core::parseMarkdown(markdown(editor).toStdString());
    QVERIFY(typist::core::astEqual(expected.blocks, actual.blocks));
    editor.formatInline(InlineStyle::Underline);
    QTest::keyClicks(&editor, " note");
    QVERIFY(markdown(editor).contains(QStringLiteral("next<u> note</u>")));

    prepare(editor, view, QStringLiteral("```html\n<u>literal</u>\n```\n"));
    const auto code = markdown(editor);
    editor.setTextCursor(editor.document()->find(QStringLiteral("literal")));
    editor.formatInline(InlineStyle::Underline);
    QCOMPARE(markdown(editor), code);
    QVERIFY(!editor.document()->isUndoAvailable());
  }

  void liveListsContinueExitAndIndent() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    QTest::keyClicks(&editor, "- one");
    QVERIFY(editor.textCursor().block().textList());
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "two");
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(editor.textCursor().block().textList()->format().indent(), 2);
    QTest::keyClick(&editor, Qt::Key_Backtab);
    QCOMPARE(editor.textCursor().block().textList()->format().indent(), 1);
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(!editor.textCursor().block().textList());
    QTest::keyClicks(&editor, "Body");
    const QString source = markdown(editor);
    QVERIFY2(source.contains(QStringLiteral("- one\n- two\n")), qPrintable(source));
    QVERIFY2(source.contains(QStringLiteral("\n\nBody")), qPrintable(source));
  }

  void taskListsKeepCheckboxesAndExit() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    QTest::keyClicks(&editor, "- [x] Done");
    QVERIFY(editor.textCursor().blockFormat().boolProperty(typist::editor::kTaskProperty));
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(editor.textCursor().blockFormat().hasProperty(typist::editor::kTaskProperty));
    QVERIFY(!editor.textCursor().blockFormat().boolProperty(typist::editor::kTaskProperty));
    QTest::keyClicks(&editor, "Next");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(!editor.textCursor().block().textList());
    const QString source = markdown(editor);
    QVERIFY2(source.contains(QStringLiteral("- [x] Done\n- [ ] Next")), qPrintable(source));
  }

  void codeFencesRetainLanguageAndBlankLines() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    QTest::keyClicks(&editor, "```cpp");
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(editor.textCursor().blockFormat().hasProperty(typist::editor::kCodeBlockProperty));
    QTest::keyClicks(&editor, "int x = 1;");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClicks(&editor, "return x;");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "```");
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(!editor.textCursor().blockFormat().hasProperty(typist::editor::kCodeBlockProperty));
    QTest::keyClicks(&editor, "after");
    const QString source = markdown(editor);
    QVERIFY2(source.contains(QStringLiteral("```cpp\nint x = 1;\n\nreturn x;\n```")),
             qPrintable(source));
    QVERIFY(source.contains(QStringLiteral("after")));
  }

  void menusPreserveSelectionAndFormatting() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view, QStringLiteral("plain and tail\n"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(5, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    editor.formatInline(typist::editor::InlineStyle::Bold);
    QVERIFY(markdown(editor).contains(QStringLiteral("**plain** and tail")));
    editor.formatParagraph(typist::editor::ParagraphStyle::Heading, 3);
    QVERIFY(markdown(editor).startsWith(QStringLiteral("### **plain** and tail")));
    editor.formatParagraph(typist::editor::ParagraphStyle::Body);
    QVERIFY2(markdown(editor).startsWith(QStringLiteral("**plain** and tail")),
             qPrintable(markdown(editor)));
    editor.undo();
    QVERIFY(markdown(editor).startsWith(QStringLiteral("###")));
  }

  void headingLevelChangesPreserveRichTextAndReverseSelection() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view,
            QStringLiteral("# Title\n\n### **Bold** and *italic* with `code` and [link](#title)\n\n"
                           "Body text.\n\n- [x] Task\n\n```md\n# literal\n```\n\n"
                           "#### 小节\n\n## Outside\n"));
    const auto original = markdown(editor);
    const auto plain = editor.toPlainText();
    const int start = editor.document()->find(QStringLiteral("Title")).selectionStart() + 1;
    const int end = editor.document()->find(QStringLiteral("Outside")).block().position();
    QTextCursor cursor(editor.document());
    cursor.setPosition(end);
    cursor.setPosition(start, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);

    QVERIFY(editor.shiftHeadingLevels(-1));
    QCOMPARE(editor.textCursor().position(), start);
    QCOMPARE(editor.textCursor().anchor(), end);
    QCOMPARE(editor.toPlainText(), plain);
    QString expected = original;
    expected.replace(QStringLiteral("\n### **Bold**"), QStringLiteral("\n## **Bold**"));
    expected.replace(QStringLiteral("\n#### 小节"), QStringLiteral("\n### 小节"));
    QCOMPARE(markdown(editor), expected);
    const auto heading = editor.document()->find(QStringLiteral("Bold")).block();
    QCOMPARE(heading.begin().fragment().charFormat().fontPointSize(), 22.0);
    editor.undo();
    QCOMPARE(markdown(editor), original);
    QVERIFY(!editor.document()->isUndoAvailable());
    editor.redo();
    QCOMPARE(markdown(editor), expected);
  }

  void headingLevelLimitsLeaveTheUndoHistoryUntouched() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view, QStringLiteral("# Top\n\n###### Bottom\n\nBody\n"));
    const auto original = markdown(editor);
    editor.setTextCursor(editor.document()->find(QStringLiteral("Top")));
    QVERIFY(!editor.shiftHeadingLevels(-1));
    editor.setTextCursor(editor.document()->find(QStringLiteral("Bottom")));
    QVERIFY(!editor.shiftHeadingLevels(1));
    editor.setTextCursor(editor.document()->find(QStringLiteral("Body")));
    QVERIFY(!editor.shiftHeadingLevels(-1));
    QCOMPARE(markdown(editor), original);
    QVERIFY(!editor.document()->isUndoAvailable());

    editor.moveCursor(QTextCursor::End);
    editor.formatParagraph(typist::editor::ParagraphStyle::Heading, 3);
    QVERIFY(editor.shiftHeadingLevels(1));
    QTest::keyClicks(&editor, " continues");
    QVERIFY(markdown(editor).contains(QStringLiteral("#### Body continues")));
    QCOMPARE(editor.textCursor().charFormat().fontPointSize(), 15.0);
  }

  void rangeSelectionUsesSentenceStyleAndCodeBoundaries() {
    using Range = typist::editor::SelectionRange;
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view,
            QStringLiteral("# Title **bold phrase** and `inline code`\n\n"
                           "Price is 3.14. Second sentence! Last?\n\n"
                           "第一句。第二句！第三句？\n\n"
                           "- [x] Task sentence. Next sentence.\n\n"
                           "```cpp\n  first();\n  second();\n```\n\nTail.\n"));
    const auto before = markdown(editor);
    const int undo = editor.document()->availableUndoSteps();
    const auto select = [&](const QString& inside, Range range) {
      auto cursor = editor.document()->find(inside);
      if (cursor.isNull()) return QString();
      cursor.setPosition(cursor.selectionStart() + 1);
      editor.setTextCursor(cursor);
      editor.selectRange(range);
      return editor.textCursor()
          .selectedText()
          .replace(QChar::ParagraphSeparator, QLatin1Char('\n'))
          .replace(QChar::LineSeparator, QLatin1Char('\n'));
    };
    QCOMPARE(select(QStringLiteral("bold"), Range::Word), QStringLiteral("bold"));
    QCOMPARE(select(QStringLiteral("bold"), Range::StyledScope), QStringLiteral("bold phrase"));
    QCOMPARE(select(QStringLiteral("inline"), Range::StyledScope), QStringLiteral("inline code"));
    QCOMPARE(select(QStringLiteral("Title"), Range::StyledScope), QStringLiteral("Title"));
    QCOMPARE(select(QStringLiteral("phrase"), Range::Paragraph),
             QStringLiteral("Title bold phrase and inline code"));
    QCOMPARE(select(QStringLiteral("3.14"), Range::LineOrSentence),
             QStringLiteral("Price is 3.14."));
    QCOMPARE(select(QStringLiteral("Second sentence"), Range::LineOrSentence),
             QStringLiteral("Second sentence!"));
    QCOMPARE(select(QStringLiteral("第二句"), Range::LineOrSentence), QStringLiteral("第二句！"));
    QCOMPARE(select(QStringLiteral("Task"), Range::LineOrSentence),
             QStringLiteral("Task sentence."));
    QCOMPARE(select(QStringLiteral("second()"), Range::LineOrSentence),
             QStringLiteral("  second();"));
    QCOMPARE(select(QStringLiteral("second()"), Range::Paragraph),
             QStringLiteral("  first();\n  second();"));

    auto last = editor.document()->find(QStringLiteral("Last?"));
    last.clearSelection();
    editor.setTextCursor(last);
    editor.selectRange(Range::LineOrSentence);
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("Last?"));
    QCOMPARE(markdown(editor), before);
    QCOMPARE(editor.document()->availableUndoSteps(), undo);

    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QCOMPARE(select(QStringLiteral("Second sentence"), Range::LineOrSentence),
             QStringLiteral("Price is 3.14. Second sentence! Last?"));
    QCOMPARE(select(QStringLiteral("bold"), Range::StyledScope), QStringLiteral("bold"));
    editor.moveCursor(QTextCursor::End);
    editor.selectRange(Range::LineOrSentence);
    QVERIFY(!editor.textCursor().hasSelection());
  }

  void deleteTextRangesRespectSentenceSpacingAndExplicitSelection() {
    using Range = typist::editor::SelectionRange;
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view,
            QStringLiteral("First sentence. **Middle phrase!** Last sentence?\n\n"
                           "第一句。第二句！第三句？\n\n"
                           "**Bold first. Bold second.** Next text.\n"));
    const auto original = markdown(editor);
    const auto at = [&](const QString& text) {
      auto cursor = editor.document()->find(text);
      QVERIFY(!cursor.isNull());
      cursor.setPosition(cursor.selectionStart() + 1);
      editor.setTextCursor(cursor);
    };
    at(QStringLiteral("Middle"));
    QVERIFY(editor.deleteRange(Range::LineOrSentence));
    QVERIFY(markdown(editor).startsWith(QStringLiteral("First sentence. Last sentence?")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
    at(QStringLiteral("Last"));
    QVERIFY(editor.deleteRange(Range::LineOrSentence));
    QVERIFY(editor.toPlainText().contains(QStringLiteral("Middle phrase!\n")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
    at(QStringLiteral("第二句"));
    QVERIFY(editor.deleteRange(Range::LineOrSentence));
    QVERIFY(markdown(editor).contains(QStringLiteral("第一句。第三句？")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
    at(QStringLiteral("Bold second"));
    QVERIFY(editor.deleteRange(Range::LineOrSentence));
    QVERIFY(markdown(editor).contains(QStringLiteral("**Bold first.** Next text.")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
    at(QStringLiteral("Middle"));
    QVERIFY(editor.deleteRange(Range::StyledScope));
    QVERIFY(!markdown(editor).contains(QStringLiteral("Middle phrase")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
    at(QStringLiteral("Middle"));
    QVERIFY(editor.deleteRange(Range::Word));
    QVERIFY2(markdown(editor).contains(QStringLiteral("**phrase\\!**")),
             qPrintable(markdown(editor)));
    QVERIFY(!markdown(editor).contains(QStringLiteral("Middle")));
    editor.undo();
    QCOMPARE(markdown(editor), original);

    auto part = editor.document()->find(QStringLiteral("iddle"));
    QVERIFY(!part.isNull());
    const int start = part.selectionStart(), end = part.selectionEnd();
    part.setPosition(end);
    part.setPosition(start, QTextCursor::KeepAnchor);
    editor.setTextCursor(part);
    QVERIFY(editor.deleteRange(Range::Paragraph));
    QVERIFY(markdown(editor).startsWith(QStringLiteral("First sentence. **M phrase\\!**")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
  }

  void deletingBlocksKeepsHeadingListAndTableBoundaries() {
    using Range = typist::editor::SelectionRange;
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view,
            QStringLiteral("# Remove heading\n\n## **Keep heading**\n\n"
                           "- [x] Parent item\n  - Child item\n    - Deep item\n"
                           "- [ ] Keep item\n\n"
                           "Before table.\n\n| Key | Value |\n|---|---|\n| a | b |\n\n"
                           "## After table\n"));
    const auto original = markdown(editor);
    const auto remove = [&](const QString& label) {
      auto cursor = editor.document()->find(label);
      if (cursor.isNull()) return false;
      cursor.clearSelection();
      editor.setTextCursor(cursor);
      return editor.deleteRange(Range::Paragraph);
    };
    QVERIFY(remove(QStringLiteral("Remove heading")));
    QVERIFY(markdown(editor).startsWith(QStringLiteral("## **Keep heading**")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
    QVERIFY(remove(QStringLiteral("Parent item")));
    QVERIFY(!markdown(editor).contains(QStringLiteral("Child item")));
    QVERIFY(!markdown(editor).contains(QStringLiteral("Deep item")));
    QVERIFY(markdown(editor).contains(QStringLiteral("- [ ] Keep item")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
    QVERIFY(remove(QStringLiteral("Before table.")));
    QVERIFY(markdown(editor).contains(QStringLiteral("| a | b |")));
    QVERIFY(markdown(editor).endsWith(QStringLiteral("## After table\n\n")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
    QVERIFY(remove(QStringLiteral("After table")));
    QVERIFY(markdown(editor).contains(QStringLiteral("| a | b |")));
    QVERIFY(!markdown(editor).contains(QStringLiteral("After table")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
  }

  void deletingCodeAndSourceLinesPreservesTheFenceAndNextBlock() {
    using Range = typist::editor::SelectionRange;
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view,
            QStringLiteral("Before.\n\n```cpp\n  first();\n  middle();\n  last();\n```\n\n"
                           "## After\n"));
    const auto original = markdown(editor);
    const auto removeLine = [&](const QString& text) {
      auto cursor = editor.document()->find(text);
      if (cursor.isNull()) return false;
      cursor.clearSelection();
      editor.setTextCursor(cursor);
      return editor.deleteRange(Range::LineOrSentence);
    };
    QVERIFY(removeLine(QStringLiteral("first()")));
    QVERIFY(markdown(editor).contains(QStringLiteral("```cpp\n  middle();\n  last();\n```")));
    QVERIFY(removeLine(QStringLiteral("last()")));
    QVERIFY(markdown(editor).contains(QStringLiteral("```cpp\n  middle();\n```")));
    const auto oneLine = markdown(editor);
    QVERIFY(removeLine(QStringLiteral("middle()")));
    QVERIFY(markdown(editor).contains(QStringLiteral("```cpp\n```")));
    QVERIFY(markdown(editor).endsWith(QStringLiteral("## After\n\n")));
    const auto emptyFence = markdown(editor);
    QVERIFY(editor.deleteRange(Range::LineOrSentence));
    QVERIFY(!markdown(editor).contains(QStringLiteral("```")));
    QVERIFY(markdown(editor).endsWith(QStringLiteral("## After\n\n")));
    editor.undo();
    QCOMPARE(markdown(editor), emptyFence);
    editor.undo();
    QCOMPARE(markdown(editor), oneLine);
    editor.undo();
    editor.undo();
    QCOMPARE(markdown(editor), original);

    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    const auto source = editor.toPlainText();
    QVERIFY(removeLine(QStringLiteral("middle()")));
    QVERIFY(editor.toPlainText().contains(QStringLiteral("  first();\n  last();")));
    editor.undo();
    QCOMPARE(editor.toPlainText(), source);
  }

  void tableOperationsRoundTripAndUndo() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    editor.createTable(3, 2);
    QTextTable* table = editor.textCursor().currentTable();
    QVERIFY(table);
    for (int row = 0; row < 3; ++row)
      for (int column = 0; column < 2; ++column)
        table->cellAt(row, column)
            .firstCursorPosition()
            .insertText(QStringLiteral("%1_%2").arg(row).arg(column));
    editor.setTextCursor(table->cellAt(1, 1).firstCursorPosition());
    QVERIFY(editor.changeTable(typist::editor::TableCommand::AlignRight));
    QString source = markdown(editor);
    QVERIFY2(source.contains(QStringLiteral("---:")), qPrintable(source));
    const auto parsed = typist::core::parseMarkdown(source.toStdString());
    QVERIFY(parsed.ok);
    QCOMPARE(parsed.blocks.front().alignments.at(1), 3);
    QTextDocument reloaded;
    QVERIFY(typist::editor::loadMarkdown(source, &reloaded, nullptr));
    const auto round =
        typist::core::parseMarkdown(typist::editor::serializeDocument(&reloaded).toStdString());
    QVERIFY(typist::core::astEqual(parsed.blocks, round.blocks));
    QVERIFY(editor.changeTable(typist::editor::TableCommand::RowBelow));
    QCOMPARE(table->rows(), 4);
    editor.undo();
    QCOMPARE(table->rows(), 3);
    editor.setTextCursor(table->cellAt(2, 1).firstCursorPosition());
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(table->rows(), 4);
    QCOMPARE(table->cellAt(editor.textCursor().position()).row(), 3);
    QVERIFY(editor.changeTable(typist::editor::TableCommand::ColumnAfter));
    QCOMPARE(table->columns(), 3);
    QVERIFY(editor.changeTable(typist::editor::TableCommand::DeleteTable));
    QVERIFY(!editor.textCursor().currentTable());
    editor.undo();
    QVERIFY(markdown(editor).contains(QStringLiteral("0_0")));
  }

  void replaceAllUsesOriginalRangesAndIsOneUndo() {
    QTextDocument document;
    document.setPlainText(QStringLiteral("cat cat\nCat"));
    document.clearUndoRedoStacks();
    typist::ui::SearchController search;
    QCOMPARE(search.replaceAll(&document, QStringLiteral("cat"), QStringLiteral("catcat")), 3);
    QCOMPARE(document.toPlainText(), QStringLiteral("catcat catcat\ncatcat"));
    document.undo();
    QCOMPARE(document.toPlainText(), QStringLiteral("cat cat\nCat"));
    document.setPlainText(QStringLiteral("name=alice name=bob"));
    document.clearUndoRedoStacks();
    search.setRegex(true);
    QCOMPARE(search.replaceAll(&document, QStringLiteral("name=(\\w+)"), QStringLiteral("$1:$1")),
             2);
    QCOMPARE(document.toPlainText(), QStringLiteral("alice:alice bob:bob"));
    document.undo();
    QCOMPARE(document.toPlainText(), QStringLiteral("name=alice name=bob"));
    QCOMPARE(search.replaceAll(&document, QStringLiteral("("), QStringLiteral("broken")), 0);
  }

  void relativeImagesAndChineseInput() {
    QTemporaryDir directory;
    QImage image(20, 10, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(directory.path() + QStringLiteral("/image.png")));
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    view.rendered()->setBaseUrl(QUrl::fromLocalFile(directory.path() + QLatin1Char('/')));
    prepare(editor, view, QStringLiteral("![local](image.png)\n"));
    QVERIFY(editor.document()->toHtml().contains(QStringLiteral("<img")));
    QVERIFY(markdown(editor).contains(QStringLiteral("(image.png)")));
    const QString docxPath = directory.path() + QStringLiteral("/relative.docx");
    QString error;
    QVERIFY(typist::platform::exportDocx(docxPath, QStringLiteral("![local](image.png)\n"), &error,
                                         directory.path()));
    QFile output(docxPath);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("word/media/image1.png"));
    prepare(editor, view);
    QTest::keyClicks(&editor, "# ");
    QInputMethodEvent input;
    input.setCommitString(QStringLiteral("中文标题"));
    QApplication::sendEvent(&editor, &input);
    QCOMPARE(editor.toPlainText(), QStringLiteral("中文标题"));
    QVERIFY(markdown(editor).startsWith(QStringLiteral("# 中文标题")));
  }

  void writingModesDoNotChangeDocument() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view, QStringLiteral("# Heading\n\nBody and `code`\n\nLast\n"));
    const QString source = markdown(editor);
    editor.setFocusMode(true);
    editor.setTypewriterMode(true);
    editor.moveCursor(QTextCursor::End);
    editor.setDarkTheme(true);
    editor.zoomDocument(2);
    editor.resetZoom();
    QCOMPARE(markdown(editor), source);
    QVERIFY(!editor.document()->isUndoAvailable());
    editor.setTypewriterMode(false);
    editor.setFocusMode(false);
  }

  void pasteMarkdownInMiddleKeepsTailAndUndo() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view, QStringLiteral("before\n\nTAIL\n"));
    const QString original = markdown(editor);
    QTextCursor cursor(editor.document());
    cursor.movePosition(QTextCursor::EndOfBlock);
    editor.setTextCursor(cursor);
    QApplication::clipboard()->setText(
        QStringLiteral("\n\n| A | B |\n|---|---:|\n| one | two |\n\ninserted\n"));
    editor.paste();
    const QString source = markdown(editor);
    QVERIFY2(source.indexOf(QStringLiteral("inserted")) < source.indexOf(QStringLiteral("TAIL")),
             qPrintable(source));
    QVERIFY(source.contains(QStringLiteral("---:")));
    editor.undo();
    QCOMPARE(markdown(editor), original);
  }

  void softLineBreakAndLinkTypingRoundTrip() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    QTest::keyClicks(&editor, "first");
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClicks(&editor, "second");
    QVERIFY2(markdown(editor).contains(QStringLiteral("first  \nsecond")),
             qPrintable(markdown(editor)));
    auto source = typist::core::parseMarkdown(markdown(editor).toStdString());
    QVERIFY(source.ok);
    QVERIFY(typist::core::astEqual(
        source.blocks,
        typist::core::parseMarkdown(typist::core::serializeMarkdown(source.blocks)).blocks));
    prepare(editor, view);
    QTest::keyClicks(&editor, "[site](https://example.com)");
    QCOMPARE(editor.toPlainText(), QStringLiteral("site"));
    QTest::keyClicks(&editor, " tail");
    QVERIFY2(markdown(editor).contains(QStringLiteral("[site](https://example.com) tail")),
             qPrintable(markdown(editor)));
  }

  void typingInsideParagraphAndHeadingKeepsStyle() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view, QStringLiteral("before  tail\n"));
    QTextCursor cursor(editor.document());
    cursor.setPosition(7);
    editor.setTextCursor(cursor);
    QTest::keyClicks(&editor, "**middle**");
    QCOMPARE(editor.toPlainText(), QStringLiteral("before middle tail"));
    QVERIFY(markdown(editor).contains(QStringLiteral("before **middle** tail")));
    prepare(editor, view);
    QTest::keyClicks(&editor, "## A *heading* more");
    const QString text = markdown(editor);
    QVERIFY2(text.startsWith(QStringLiteral("## A *heading* more")), qPrintable(text));
    QTextCursor last(editor.document());
    last.movePosition(QTextCursor::End);
    QVERIFY(last.charFormat().fontPointSize() >= 22);
    prepare(editor, view);
    QTest::keyClicks(&editor, "(");
    QCOMPARE(editor.toPlainText(), QStringLiteral("()"));
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QCOMPARE(editor.toPlainText(), QString());
  }

  void viewSwitchDropsSelectionsFromThePreviousLayout() {
    typist::ui::ViewSwitch view;
    QTextEdit editor;
    QVERIFY(
        view.loadMarkdownIntoBoth(QStringLiteral("Text with a link and a code fence"), nullptr));
    view.showActiveIn(&editor);
    QTextEdit::ExtraSelection selection;
    selection.cursor = QTextCursor(editor.document());
    selection.cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    selection.format.setBackground(Qt::yellow);
    editor.setExtraSelections({selection});
    QCOMPARE(editor.extraSelections().size(), 1);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QCOMPARE(editor.document(), view.source());
    QVERIFY(editor.extraSelections().isEmpty());
    selection.cursor = QTextCursor(editor.document());
    selection.cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    editor.setExtraSelections({selection});
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QCOMPARE(editor.document(), view.rendered());
    QVERIFY(editor.extraSelections().isEmpty());
  }

  void sourceCommandsDoNotOverwriteSelectedLines() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view, QStringLiteral("- one\n- two\n"));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    editor.selectAll();
    editor.changeListIndent(1);
    QCOMPARE(editor.toPlainText(), QStringLiteral("  - one\n  - two\n"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("- one\n- two\n"));
    editor.selectAll();
    editor.formatParagraph(typist::editor::ParagraphStyle::Quote);
    QVERIFY(editor.toPlainText().contains(QStringLiteral("> one\n> two")));
  }

  void emptyQuoteReturnsToBodyWithoutEmptyQuoteLine() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view);
    QTest::keyClicks(&editor, "> quoted");
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "body");
    QVERIFY2(markdown(editor).contains(QStringLiteral("> quoted\n\nbody")),
             qPrintable(markdown(editor)));
  }
};
QTEST_MAIN(EditingTests)
#include "editing_tests.moc"
