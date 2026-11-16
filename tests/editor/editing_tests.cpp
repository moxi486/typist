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
  void htmlCommentsKeepTheirSourceAndConvertUnicodeOnInput() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setAutoPair(false);
    QInputMethodEvent note;
    const auto unicode = QStringLiteral("<!\u2014 draft \u2014>");
    note.setCommitString(unicode);
    prepare(editor, view);
    QApplication::sendEvent(&editor, &note);
    QCOMPARE(editor.toPlainText(), unicode);

    editor.setUnicodeBlockMarkers(true);
    prepare(editor, view, QStringLiteral("tail"));
    editor.moveCursor(QTextCursor::Start);
    QApplication::sendEvent(&editor, &note);
    const auto ascii = QStringLiteral("<!-- draft -->");
    QCOMPARE(editor.toPlainText(), ascii + QStringLiteral("tail"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), unicode + QStringLiteral("tail"));
    editor.redo();
    QCOMPARE(editor.toPlainText(), ascii + QStringLiteral("tail"));

    prepare(editor, view);
    QTest::keyClicks(&editor, "**Bold** ");
    QInputMethodEvent start;
    start.setCommitString(QStringLiteral("<!\u2013 typed "));
    QApplication::sendEvent(&editor, &start);
    editor.setSmartDashes(true);
    QTest::keyClicks(&editor, "--> visible");
    QCOMPARE(markdown(editor).trimmed(), QStringLiteral("**Bold** <!-- typed --> visible"));

    prepare(editor, view, QStringLiteral("<!-- first\nsecond note\n\nlast -->\n\nVisible."));
    auto edit = editor.document()->find(QStringLiteral("second note"));
    QVERIFY(!edit.isNull());
    edit.setPosition(edit.selectionStart() + 7);
    editor.setTextCursor(edit);
    QTest::keyClicks(&editor, "*draft* ");
    QVERIFY(
        markdown(editor).contains(QStringLiteral("<!-- first\nsecond *draft* note\n\nlast -->")));
  }

  void unicodeCommentDelimitersStayLiteralInCodeAndSource() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setAutoPair(false);
    editor.setUnicodeBlockMarkers(true);
    const auto unicode = QStringLiteral("<!\u2014 draft \u2014>");
    QInputMethodEvent note;
    note.setCommitString(unicode);
    prepare(editor, view, unicode);
    QCOMPARE(editor.toPlainText(), unicode);

    prepare(editor, view);
    QTest::keyClicks(&editor, "\x60");
    QApplication::sendEvent(&editor, &note);
    QTest::keyClicks(&editor, "\x60");
    QCOMPARE(editor.toPlainText(), unicode);
    QVERIFY(markdown(editor).startsWith(QStringLiteral("\x60") + unicode + QStringLiteral("\x60")));

    prepare(editor, view, QStringLiteral("\x60\x60\x60text\nplaceholder\n\x60\x60\x60\n"));
    editor.moveCursor(QTextCursor::End);
    QApplication::sendEvent(&editor, &note);
    QVERIFY(markdown(editor).contains(QStringLiteral("placeholder") + unicode));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    editor.clear();
    QApplication::sendEvent(&editor, &note);
    QCOMPARE(editor.toPlainText(), unicode);
  }

  void unicodeLinkTitleQuotesRenderOnKeyboardAndInputMethodCommit() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setUnicodeBlockMarkers(true);
    editor.setAutoPair(false);
    prepare(editor, view);
    QTest::keyClicks(&editor, "Before [Guide](https://example.test/a_b(c) ");
    const auto title = QStringLiteral("Read \"this\" (draft)");
    QInputMethodEvent titleInput;
    titleInput.setCommitString(QStringLiteral("\u201c") + title + QStringLiteral("\u201d"));
    QApplication::sendEvent(&editor, &titleInput);
    QTest::keyClicks(&editor, ")");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Before Guide"));
    const auto guide = editor.document()->find(QStringLiteral("Guide"));
    QVERIFY(guide.charFormat().isAnchor());
    QCOMPARE(guide.charFormat().anchorHref(), QStringLiteral("https://example.test/a_b(c)"));
    QCOMPARE(guide.charFormat().property(typist::editor::kLinkTitleProperty).toString(), title);
    QVERIFY(markdown(editor).contains(QStringLiteral(" \"Read &quot;this&quot; (draft)\"")));
    editor.undo();
    QCOMPARE(editor.toPlainText(),
             QStringLiteral("Before [Guide](https://example.test/a_b(c) \u201c") + title +
                 QStringLiteral("\u201d)"));
    editor.redo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("Before Guide"));

    prepare(editor, view, QStringLiteral("tail"));
    editor.moveCursor(QTextCursor::Start);
    QInputMethodEvent imageInput;
    imageInput.setCommitString(QStringLiteral("![](thumb.png\u3000\u00abPreview\u00bb)"));
    QApplication::sendEvent(&editor, &imageInput);
    QCOMPARE(editor.toPlainText(), QStringLiteral("thumb.pngtail"));
    QVERIFY(markdown(editor).startsWith(QStringLiteral("![](thumb.png \"Preview\")")));
  }

  void unicodeLinkTitlesRespectTheSettingAndLiteralMarkdown() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setAutoPair(false);
    const auto source = QStringLiteral("[Guide](note.md \u201cTitle\u201d)");
    prepare(editor, view);
    QInputMethodEvent input;
    input.setCommitString(source);
    QApplication::sendEvent(&editor, &input);
    QCOMPARE(editor.toPlainText(), source);

    editor.setUnicodeBlockMarkers(true);
    prepare(editor, view, source);
    QCOMPARE(editor.toPlainText(), source);
    prepare(editor, view);
    QTest::keyClicks(&editor, "\x60");
    QApplication::sendEvent(&editor, &input);
    QTest::keyClicks(&editor, "\x60");
    QCOMPARE(editor.toPlainText(), source);
    QVERIFY(!editor.document()->find(QStringLiteral("Guide")).charFormat().isAnchor());
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    editor.clear();
    QApplication::sendEvent(&editor, &input);
    QCOMPARE(editor.toPlainText(), source);
  }

  void unicodeBlockMarkersConvertKeyboardAndInputMethodText() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setUnicodeBlockMarkers(true);
    prepare(editor, view);
    QInputMethodEvent prefix;
    prefix.setCommitString(QStringLiteral("\u300b"));
    QApplication::sendEvent(&editor, &prefix);
    QTest::keyClick(&editor, Qt::Key_Space);
    QVERIFY(editor.document()->begin().blockFormat().hasProperty(typist::editor::kQuoteProperty));
    QVERIFY(editor.toPlainText().isEmpty());
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("\u300b "));
    editor.redo();
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, "A quotation");
    QCOMPARE(markdown(editor).trimmed(), QStringLiteral("> A quotation"));

    prepare(editor, view, QStringLiteral("tail"));
    editor.moveCursor(QTextCursor::Start);
    QInputMethodEvent withText;
    const auto quoted = QStringLiteral("\u5f15\u7528 ");
    withText.setCommitString(QStringLiteral("\uff1e\u3000") + quoted);
    QApplication::sendEvent(&editor, &withText);
    QCOMPARE(editor.toPlainText(), quoted + QStringLiteral("tail"));
    QCOMPARE(editor.textCursor().position(), quoted.size());
    QCOMPARE(markdown(editor).trimmed(), QStringLiteral("> ") + quoted + QStringLiteral("tail"));
    editor.undo();
    QCOMPARE(editor.toPlainText(),
             QStringLiteral("\uff1e\u3000") + quoted + QStringLiteral("tail"));

    prepare(editor, view);
    const auto input = QStringLiteral(" \u2014 ");
    editor.insertPlainText(input);
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(editor.document()->begin().blockFormat().hasProperty(typist::editor::kRuleProperty));
    QCOMPARE(markdown(editor).trimmed(), QStringLiteral("---"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), input);
  }

  void unicodeBlockMarkersKeepLiteralTextAndStructuredBlocks() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setUnicodeBlockMarkers(true);
    prepare(editor, view);
    const auto ordinary = QStringLiteral("Text \u300b quotation and a dash \u2014");
    editor.insertPlainText(ordinary);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(markdown(editor).trimmed(), ordinary);

    for (const auto& literal : {QStringLiteral("`\u2014`"), QStringLiteral("**\u2014**"),
                                QStringLiteral("[\u2014](note)"), QStringLiteral("# \u2014"),
                                QStringLiteral("- \u2014"), QStringLiteral("> \u2014")}) {
      prepare(editor, view, literal);
      editor.moveCursor(QTextCursor::End);
      QTest::keyClick(&editor, Qt::Key_Return);
      QVERIFY(!editor.document()->begin().blockFormat().hasProperty(typist::editor::kRuleProperty));
      QVERIFY(markdown(editor).startsWith(literal));
    }

    prepare(editor, view, QStringLiteral("```text\n\u300b\n\u2014\n```\n"));
    editor.moveCursor(QTextCursor::Start);
    editor.moveCursor(QTextCursor::EndOfBlock);
    QTest::keyClick(&editor, Qt::Key_Space);
    QVERIFY(editor.document()->begin().text().startsWith(QStringLiteral("\u300b ")));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(markdown(editor).contains(QStringLiteral("\n\u2014\n")));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    editor.clear();
    QInputMethodEvent source;
    source.setCommitString(QStringLiteral("\uff1e\u3000title"));
    QApplication::sendEvent(&editor, &source);
    QCOMPARE(editor.toPlainText(), QStringLiteral("\uff1e\u3000title"));
  }

  void smartDashesConvertProseAndUndoToAscii() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setSmartDashes(true);
    prepare(editor, view);
    QTest::keyClicks(&editor, "10--");
    QCOMPARE(editor.toPlainText(), QStringLiteral("10\u2013"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("10--"));
    editor.redo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("10\u2013"));

    prepare(editor, view);
    QTest::keyClicks(&editor, "Wait---");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Wait\u2014"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("Wait---"));
    editor.redo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("Wait\u2014"));
    QTest::keyClicks(&editor, " what...");
    const auto expected = QStringLiteral("Wait\u2014 what\u2026");
    QCOMPARE(editor.toPlainText(), expected);
    QVERIFY(markdown(editor).startsWith(expected));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("Wait\u2014 what..."));

    prepare(editor, view, QStringLiteral("\u2026"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, ".");
    QCOMPARE(editor.toPlainText(), QStringLiteral("\u2026."));
    prepare(editor, view, QStringLiteral("Existing \u2013"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, "-");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Existing \u2013-"));

    prepare(editor, view, QStringLiteral("Before tail"));
    auto cursor = editor.textCursor();
    cursor.setPosition(6);
    editor.setTextCursor(cursor);
    QTest::keyClicks(&editor, "---");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Before\u2014 tail"));
  }

  void smartDashesPreserveMarkdownSyntaxAndLiteralInput() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setSmartDashes(true);
    editor.setAutoPair(false);
    prepare(editor, view);
    for (const auto* text :
         {"Text \\--- and \\...", "Formula $x--1...", "Link [label](foo--bar...",
          "URL https://example.org/a--b...", "Code `run--...", "Text <!-- a--b... -->"}) {
      QTest::keyClicks(&editor, text);
      QCOMPARE(editor.toPlainText(), QString::fromLatin1(text));
      prepare(editor, view);
    }
    QTest::keyClicks(&editor, "---");
    QCOMPARE(editor.toPlainText(), QStringLiteral("---"));
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(editor.document()->begin().blockFormat().hasProperty(typist::editor::kRuleProperty));

    prepare(editor, view, QStringLiteral("```text\ncode\n```\n"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, " --...");
    QVERIFY(markdown(editor).contains(QStringLiteral("code --...")));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    editor.clear();
    QTest::keyClicks(&editor, "Source --- ...");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Source --- ..."));
  }

  void smartQuotesConvertTypingAndRespectUndo() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setSmartQuotes(true);
    editor.setAutoPair(false);
    prepare(editor, view);
    QTest::keyClicks(&editor, "\"It's a 'word'.\"");
    const auto curly = QStringLiteral("\u201cIt\u2019s a \u2018word\u2019.\u201d");
    QCOMPARE(editor.toPlainText(), curly);
    QVERIFY(markdown(editor).startsWith(curly));
    editor.undo();
    QCOMPARE(editor.toPlainText(), curly.chopped(1) + '"');
    editor.redo();
    QCOMPARE(editor.toPlainText(), curly);

    prepare(editor, view);
    QTest::keyClicks(&editor, "'It's spaced '");
    QCOMPARE(editor.toPlainText(), QStringLiteral("\u2018It\u2019s spaced \u2019"));

    prepare(editor, view);
    editor.insertPlainText(QStringLiteral("他说"));
    QTest::keyClicks(&editor, "\"");
    QInputMethodEvent input;
    input.setCommitString(QStringLiteral("你好"));
    QApplication::sendEvent(&editor, &input);
    QTest::keyClicks(&editor, "\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("他说\u201c你好\u201d"));

    editor.setAutoPair(true);
    prepare(editor, view);
    QTest::keyClicks(&editor, "\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("\u201c\u201d"));
    QCOMPARE(editor.textCursor().positionInBlock(), 1);
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QVERIFY(editor.toPlainText().isEmpty());
    QTest::keyClicks(&editor, "\"It's a 'word'.\"");
    QCOMPARE(editor.toPlainText(), curly);
    QCOMPARE(editor.textCursor().positionInBlock(), curly.size());

    prepare(editor, view, QStringLiteral("Start **word** end"));
    const auto word = editor.document()->find(QStringLiteral("word"));
    auto reverse = word;
    reverse.setPosition(word.selectionEnd());
    reverse.setPosition(word.selectionStart(), QTextCursor::KeepAnchor);
    editor.setTextCursor(reverse);
    QTest::keyClicks(&editor, "\"");
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("word"));
    QVERIFY(editor.textCursor().position() < editor.textCursor().anchor());
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Start \u201c**word**\u201d end")));
    editor.undo();
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Start **word** end")));

    prepare(editor, view);
    QTest::keyClicks(&editor, "Literal \\\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Literal \""));
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Literal \"")));
  }

  void smartQuoteStylesApplyToTypingPairsAndSelections() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setSmartQuotes(true);
    editor.setAutoPair(false);
    editor.setSmartQuotePair('"', {u'\u00ab', u'\u00bb'});
    editor.setSmartQuotePair('\'', {u'\u201a', u'\u2018'});
    prepare(editor, view);
    QTest::keyClicks(&editor, "\"'Word' isn't literal.\"");
    const auto styled = QStringLiteral("\u00ab\u201aWord\u2018 isn\u2019t literal.\u00bb");
    QCOMPARE(editor.toPlainText(), styled);
    QVERIFY(markdown(editor).startsWith(styled));
    editor.undo();
    QCOMPARE(editor.toPlainText(), styled.chopped(1) + '"');
    editor.redo();
    QCOMPARE(editor.toPlainText(), styled);

    editor.setSmartQuotePair('"', {u'\u300c', u'\u300d'});
    editor.setSmartQuotePair('\'', {u'\u300e', u'\u300f'});
    prepare(editor, view);
    editor.insertPlainText(QStringLiteral("\u5979\u8bf4"));
    QTest::keyClicks(&editor, "\"");
    QInputMethodEvent input;
    input.setCommitString(QStringLiteral("\u4f60\u597d"));
    QApplication::sendEvent(&editor, &input);
    QTest::keyClicks(&editor, "\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("\u5979\u8bf4\u300c\u4f60\u597d\u300d"));

    editor.setAutoPair(true);
    prepare(editor, view);
    QTest::keyClicks(&editor, "\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("\u300c\u300d"));
    QCOMPARE(editor.textCursor().position(), 1);
    QTest::keyClicks(&editor, "Word\".");
    QCOMPARE(editor.toPlainText(), QStringLiteral("\u300cWord\u300d."));

    prepare(editor, view);
    for (const auto* quote : {"\"", "'"}) {
      QTest::keyClicks(&editor, quote);
      QCOMPARE(editor.toPlainText().size(), 2);
      QTest::keyClick(&editor, Qt::Key_Backspace);
      QVERIFY(editor.toPlainText().isEmpty());
    }

    prepare(editor, view, QStringLiteral("Wrap"));
    auto cursor = editor.textCursor();
    cursor.setPosition(4);
    cursor.setPosition(0, QTextCursor::KeepAnchor);
    editor.setTextCursor(cursor);
    QTest::keyClicks(&editor, "'");
    QCOMPARE(editor.toPlainText(), QStringLiteral("\u300eWrap\u300f"));
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("Wrap"));
    QVERIFY(editor.textCursor().position() < editor.textCursor().anchor());
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("Wrap"));
  }

  void smartQuotesKeepMarkdownSyntaxAndCodeLiteral() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setSmartQuotes(true);
    editor.setAutoPair(false);
    editor.setSmartQuotePair('"', {u'\u201e', u'\u201c'});
    prepare(editor, view);
    QTest::keyClicks(&editor, "Code `value\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Code `value\""));
    prepare(editor, view);
    QTest::keyClicks(&editor, "Math $x\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Math $x\""));
    prepare(editor, view);
    QTest::keyClicks(&editor, "[label](note \"title ) rest\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("[label](note \"title ) rest\""));
    prepare(editor, view);
    QTest::keyClicks(&editor, "[ref]: note \"title\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("[ref]: note \"title\""));
    prepare(editor, view);
    QTest::keyClicks(&editor, "<span title=\"a > b\"> \"hey\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("<span title=\"a > b\"> \u201ehey\u201c"));
    prepare(editor, view, QStringLiteral("```text\ncode\n```\n"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, " \"literal\"");
    QVERIFY(markdown(editor).contains(QStringLiteral("code \"literal\"")));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    editor.clear();
    QTest::keyClicks(&editor, "Source \"literal\"");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Source \"literal\""));
  }

  void markdownAutoPairTypesAndClosesInlineDelimiters() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setMarkdownAutoPair(true);
    prepare(editor, view, QStringLiteral("Lead"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, " *");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead **"));
    QCOMPARE(editor.textCursor().positionInBlock(), 6);
    QTest::keyClicks(&editor, "*bold*");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead **bold**"));
    QTest::keyClicks(&editor, "* and _italic_ and `code`");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead bold and italic and code"));
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Lead **bold** and *italic* and `code`")));
    editor.undo();
    QVERIFY(editor.toPlainText().endsWith(QStringLiteral("`code`")));

    prepare(editor, view, QStringLiteral("Lead"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, " _");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead __"));
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead "));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead __"));

    prepare(editor, view, QStringLiteral("Lead"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, " $x^2$");
    QVERIFY(markdown(editor).contains(QStringLiteral("$x^2$")));
    QVERIFY(editor.toPlainText().endsWith(QChar::ObjectReplacementCharacter));

    prepare(editor, view, QStringLiteral("Lead"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, " *");
    QInputMethodEvent input;
    input.setCommitString(QStringLiteral("中文"));
    QApplication::sendEvent(&editor, &input);
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead *中文*"));
    QTest::keyClicks(&editor, "*");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead 中文"));
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Lead *中文*")));
  }

  void markdownAutoPairWrapsSelectionAndPreservesBlockSyntax() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    editor.setMarkdownAutoPair(true);
    prepare(editor, view, QStringLiteral("Lead **kept** tail"));
    const auto reversed = editor.document()->find(QStringLiteral("tail"));
    auto selection = reversed;
    selection.setPosition(reversed.selectionEnd());
    selection.setPosition(reversed.selectionStart(), QTextCursor::KeepAnchor);
    editor.setTextCursor(selection);
    QTest::keyClicks(&editor, "~");
    QCOMPARE(editor.textCursor().anchor(), reversed.selectionEnd());
    QCOMPARE(editor.textCursor().position(), reversed.selectionStart());
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Lead **kept** ~~tail~~")));
    editor.undo();
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Lead **kept** tail")));
    editor.setTextCursor(editor.document()->find(QStringLiteral("tail")));
    QTest::keyClicks(&editor, "_");
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Lead **kept** *tail*")));
    editor.undo();
    editor.setTextCursor(editor.document()->find(QStringLiteral("tail")));
    QTest::keyClicks(&editor, "`");
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Lead **kept** `tail`")));
    editor.undo();
    editor.setTextCursor(editor.document()->find(QStringLiteral("tail")));
    QTest::keyClicks(&editor, "$");
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Lead **kept** $tail$")));

    prepare(editor, view, QStringLiteral("Before two words after"));
    const auto spaced = QStringLiteral(" two words ");
    editor.setTextCursor(editor.document()->find(spaced));
    QTest::keyClicks(&editor, "*");
    QCOMPARE(editor.textCursor().selectedText(), spaced);
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Before *two words* after")));
    editor.undo();
    QVERIFY(markdown(editor).startsWith(QStringLiteral("Before two words after")));

    prepare(editor, view);
    QTest::keyClicks(&editor, "* item");
    QVERIFY(editor.textCursor().currentList());
    QCOMPARE(editor.toPlainText(), QStringLiteral("item"));
    prepare(editor, view);
    QTest::keyClicks(&editor, "```");
    QCOMPARE(editor.toPlainText(), QStringLiteral("```"));
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(editor.textCursor().blockFormat().hasProperty(typist::editor::kCodeBlockProperty));
    QTest::keyClicks(&editor, "code *");
    QCOMPARE(editor.toPlainText(), QStringLiteral("code *"));
    prepare(editor, view, QStringLiteral("Lead"));
    editor.moveCursor(QTextCursor::End);
    QTest::keyClicks(&editor, " \\*");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Lead \\*"));
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    editor.clear();
    QTest::keyClicks(&editor, "Source *");
    QCOMPARE(editor.toPlainText(), QStringLiteral("Source *"));
  }

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
    view.setZoomPercent(120);
    editor.showDocumentZoom(view.zoomPercent());
    view.setZoomPercent(typist::ui::kActualZoom);
    editor.showDocumentZoom(view.zoomPercent());
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

  void markdownReloadNotifiesAfterRebuildingTheDocument() {
    typist::ui::ViewSwitch view;
    typist::ui::Editor editor;
    prepare(editor, view, QStringLiteral("Before\n\n```cpp\nfirst\nsecond\n```\n"));
    const auto oldSelection = editor.document()->find(QStringLiteral("second"));
    QVERIFY(oldSelection.hasSelection());
    editor.setTextCursor(oldSelection);
    QStringList snapshots;
    connect(editor.document(), &QTextDocument::contentsChanged, &editor,
            [&] { snapshots.append(editor.toPlainText()); });
    QVERIFY(view.loadMarkdownIntoBoth(QStringLiteral("New heading\n\nNew paragraph\n"), nullptr));
    QCOMPARE(snapshots, QStringList{QStringLiteral("New heading\nNew paragraph")});
    QVERIFY(!editor.document()->isUndoAvailable());
    editor.moveCursor(QTextCursor::End);
    editor.insertPlainText(QStringLiteral(" tail"));
    editor.undo();
    QCOMPARE(editor.toPlainText(), QStringLiteral("New heading\nNew paragraph"));
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
