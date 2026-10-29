#include "DocxWriter.h"
#include "Editor.h"
#include "ListEditing.h"
#include "PdfWriter.h"
#include "ViewSwitch.h"
#include "markdown.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextList>
#include <QtTest>

using namespace typist;
namespace {
void prepare(ui::Editor& editor, ui::ViewSwitch& view, const QString& source) {
  QVERIFY(view.loadMarkdownIntoBoth(source, nullptr));
  view.showActiveIn(&editor);
  editor.resize(1000, 740);
  editor.show();
  editor.setFocus();
  QTest::qWait(5);
}
QTextBlock item(ui::Editor& editor, const QString& text) {
  for (auto block = editor.document()->begin(); block.isValid(); block = block.next())
    if (block.text().contains(text)) return block;
  return {};
}
int depth(const QTextBlock& block) {
  return block.textList() ? block.textList()->format().indent() : 0;
}
void choose(ui::Editor& editor, const QString& text, bool end = false) {
  QTextCursor cursor(item(editor, text));
  if (end)
    cursor.movePosition(QTextCursor::EndOfBlock);
  else
    cursor.setPosition(editor::listContentStart(cursor.block()));
  editor.setTextCursor(cursor);
}
void select(ui::Editor& editor, const QString& first, const QString& last, bool backwards = false) {
  QTextCursor cursor(item(editor, first));
  const int begin = cursor.position();
  const auto end = item(editor, last);
  cursor.setPosition(end.position() + end.length() - 1, QTextCursor::KeepAnchor);
  if (backwards) {
    const int finish = cursor.position();
    cursor.setPosition(finish);
    cursor.setPosition(begin, QTextCursor::KeepAnchor);
  }
  editor.setTextCursor(cursor);
}
void roundTrip(const QString& source) {
  const auto parsed = core::parseMarkdown(source.toStdString());
  const auto coreRound = core::parseMarkdown(core::serializeMarkdown(parsed.blocks));
  QVERIFY(parsed.ok && coreRound.ok && core::astEqual(parsed.blocks, coreRound.blocks));
  QTextDocument document;
  QVERIFY(editor::loadMarkdown(source, &document, nullptr));
  const auto documentRound =
      core::parseMarkdown(editor::serializeDocument(&document).toStdString());
  QVERIFY2(core::astEqual(parsed.blocks, documentRound.blocks),
           qPrintable(editor::serializeDocument(&document)));
}
const QString tree = QStringLiteral(
    "Before.\n\n- First\n- **Parent**\n  - [x] Child\n    1. Grandchild\n- Last\n\nAfter.\n");
}  // namespace

class ListWorkflowTests : public QObject {
  Q_OBJECT
 private slots:
  void docxUsesListCountersRestartsAndNineLevels() {
    QString source = QStringLiteral("1. Parent\n   1. Child\n2. Sibling\n\nBody\n\n1. Restart\n");
    for (int level = 0; level < 9; ++level)
      source += QString(level * 3, ' ') + QStringLiteral("1. Level %1\n").arg(level + 1);
    QTemporaryDir temp;
    QString error;
    QVERIFY2(platform::exportDocx(temp.filePath("numbering.docx"), source, &error),
             qPrintable(error));
    QFile file(temp.filePath("numbering.docx"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(bytes.contains("<w:lvlText w:val=\"%1.\"/>"));
    QVERIFY(bytes.contains("<w:lvlText w:val=\"%9.\"/>"));
    QVERIFY(!bytes.contains("<w:lvlText w:val=\"1.\"/>"));
    QVERIFY(bytes.contains("<w:num w:numId=\"3\">"));
    QVERIFY(bytes.contains("<w:lvlOverride w:ilvl=\"8\"><w:startOverride w:val=\"1\"/>"));
    QVERIFY(bytes.contains("<w:ilvl w:val=\"8\"/>"));
  }
  void orderedNumbersContinueAcrossChildrenAndRestartAfterProse() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view,
            "1. First\n   - Child\n2. Second\n3. Third\n\nProse\n\n1. Restart\n2. Again\n");
    auto* list = item(editor, "First").textList();
    QCOMPARE(item(editor, "Second").textList(), list);
    QCOMPARE(list->itemText(item(editor, "Second")), QStringLiteral("2."));
    QCOMPARE(list->itemText(item(editor, "Third")), QStringLiteral("3."));
    QCOMPARE(item(editor, "Restart").textList()->itemText(item(editor, "Restart")),
             QStringLiteral("1."));
    choose(editor, "Third", true);
    QTest::keyClick(&editor, Qt::Key_Return);
    QTest::keyClicks(&editor, "Fourth");
    QCOMPARE(item(editor, "Fourth").textList()->itemText(item(editor, "Fourth")),
             QStringLiteral("4."));
    roundTrip(view.currentMarkdown());
  }
  void multilineCorporaRoundTrip() {
    const auto root = QString::fromUtf8(qgetenv("TYPIST_CORPUS_DIR"));
    for (const auto* name : {"list_multiline.md", "list_multiline_wide.md", "list-writing.md"}) {
      QFile file(root + '/' + name);
      QVERIFY(file.open(QIODevice::ReadOnly));
      roundTrip(QString::fromUtf8(file.readAll()));
    }
  }
  void rootOutdentPromotesChildrenAndKeepsSurroundings() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, tree);
    choose(editor, "Parent");
    const auto before = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_Backtab);
    QCOMPARE(depth(item(editor, "Parent")), 0);
    QCOMPARE(depth(item(editor, "Child")), 1);
    QCOMPARE(depth(item(editor, "Grandchild")), 2);
    QCOMPARE(depth(item(editor, "Last")), 1);
    QVERIFY(view.currentMarkdown().contains("**Parent**"));
    QVERIFY(view.currentMarkdown().contains("After."));
    roundTrip(view.currentMarkdown());
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
  }
  void indentMovesSubtreeAndRetainsMixedKinds() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, tree);
    choose(editor, "Parent");
    const auto before = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(depth(item(editor, "Parent")), 2);
    QCOMPARE(depth(item(editor, "Child")), 3);
    QCOMPARE(depth(item(editor, "Grandchild")), 4);
    QCOMPARE(depth(item(editor, "Last")), 1);
    QVERIFY(item(editor, "Child").blockFormat().boolProperty(editor::kTaskProperty));
    QCOMPARE(item(editor, "Grandchild").textList()->format().style(), QTextListFormat::ListDecimal);
    QVERIFY(view.currentMarkdown().contains("**Parent**"));
    roundTrip(view.currentMarkdown());
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
    editor.redo();
    choose(editor, "Parent");
    QTest::keyClick(&editor, Qt::Key_Backtab);
    QCOMPARE(view.currentMarkdown(), before);
  }
  void tabWorksOnReverseMultiItemSelection() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, tree);
    select(editor, "Parent", "Last", true);
    const auto before = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_Tab);
    QVERIFY(editor.textCursor().position() < editor.textCursor().anchor());
    QCOMPARE(depth(item(editor, "Last")), 2);
    QCOMPARE(depth(item(editor, "Child")), 3);
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
    choose(editor, "First");
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(view.currentMarkdown(), before);
    select(editor, "First", "After.");
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(view.currentMarkdown(), before);
  }
  void changingKindsRetainsDepthFormattingAndTaskState() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, tree);
    select(editor, "Parent", "Grandchild");
    const auto before = view.currentMarkdown();
    editor.formatParagraph(editor::ParagraphStyle::TaskList);
    QCOMPARE(depth(item(editor, "Parent")), 1);
    QCOMPARE(depth(item(editor, "Child")), 2);
    QCOMPARE(depth(item(editor, "Grandchild")), 3);
    QVERIFY(item(editor, "Child").blockFormat().boolProperty(editor::kTaskProperty));
    QVERIFY(!item(editor, "Parent").blockFormat().boolProperty(editor::kTaskProperty));
    QVERIFY(view.currentMarkdown().contains("**Parent**"));
    roundTrip(view.currentMarkdown());
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
    select(editor, "Child", "Grandchild");
    editor.formatParagraph(editor::ParagraphStyle::OrderedList);
    QVERIFY(!item(editor, "Child").blockFormat().hasProperty(editor::kTaskProperty));
    QCOMPARE(depth(item(editor, "Grandchild")), 3);
    roundTrip(view.currentMarkdown());
  }
  void togglingSelectedTasksUnifiesStateAndKeepsSelection() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, "- [x] **Done**\n- ordinary\n- [ ] Next\n\nTail.\n");
    select(editor, "Done", "Next", true);
    const auto before = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(item(editor, "Done").blockFormat().boolProperty(editor::kTaskProperty));
    QVERIFY(item(editor, "Next").blockFormat().boolProperty(editor::kTaskProperty));
    QVERIFY(!item(editor, "ordinary").blockFormat().hasProperty(editor::kTaskProperty));
    QVERIFY(editor.textCursor().position() < editor.textCursor().anchor());
    roundTrip(view.currentMarkdown());
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
    QVERIFY(editor.changeTaskStatus(editor::TaskStatus::Todo));
    QVERIFY(view.currentMarkdown().contains("- [ ] **Done**"));
    QVERIFY(editor.changeTaskStatus(editor::TaskStatus::Toggle));
    QVERIFY(editor.changeTaskStatus(editor::TaskStatus::Toggle));
    QVERIFY(!item(editor, "Next").blockFormat().boolProperty(editor::kTaskProperty));
    choose(editor, "Tail.");
    QVERIFY(!editor.changeTaskStatus(editor::TaskStatus::Done));
  }
  void emptyNestedItemOutdentsOneLevelBeforeExiting() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, "- Parent\n  - [x] Child\n\nTail.\n");
    choose(editor, "Child", true);
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(depth(editor.textCursor().block()), 2);
    QVERIFY(!editor.textCursor().blockFormat().boolProperty(editor::kTaskProperty));
    QTest::keyClick(&editor, Qt::Key_Return);
    QCOMPARE(depth(editor.textCursor().block()), 1);
    QVERIFY(editor.textCursor().blockFormat().hasProperty(editor::kTaskProperty));
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(!editor.textCursor().block().textList());
    QVERIFY(!editor.textCursor().blockFormat().hasProperty(editor::kTaskProperty));
    QTest::keyClicks(&editor, "Body");
    QVERIFY(view.currentMarkdown().contains("\n\nBody"));
    QVERIFY(view.currentMarkdown().contains("Tail."));
  }
  void taskHomeBackspaceAndSplittingKeepMarkersValid() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, "- [x] hello world\n\nTail.\n");
    choose(editor, "hello", true);
    QTest::keyClick(&editor, Qt::Key_Home, Qt::ShiftModifier);
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("hello world"));
    QTest::keyClick(&editor, Qt::Key_Home);
    QCOMPARE(editor.textCursor().positionInBlock(), 2);
    auto cursor = editor.textCursor();
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 5);
    editor.setTextCursor(cursor);
    const auto before = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_Return);
    QVERIFY(view.currentMarkdown().contains("- [x] hello\n- [ ]  world"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
    QTest::keyClick(&editor, Qt::Key_Home);
    QTest::keyClick(&editor, Qt::Key_Left);
    QTest::keyClick(&editor, Qt::Key_Backspace);
    QVERIFY(!item(editor, "hello").textList());
    QVERIFY(!editor.toPlainText().contains(QChar(0x2611)));
    QVERIFY(editor.toPlainText().contains("hello world"));
  }
  void softBreakRemainsInSameItemAcrossSaveAndExport() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, "- [x] First\n  1. Inner\n\nTail.\n");
    choose(editor, "First", true);
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ShiftModifier);
    QTest::keyClicks(&editor, "Second line");
    const auto source = view.currentMarkdown();
    QVERIFY2(source.contains("First  \n  Second line"), qPrintable(source));
    roundTrip(source);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QCOMPARE(view.currentMarkdown(), source);
    QTemporaryDir temp;
    QString error;
    QVERIFY2(platform::exportDocx(temp.filePath("lists.docx"), source, &error), qPrintable(error));
    QFile docx(temp.filePath("lists.docx"));
    QVERIFY(docx.open(QIODevice::ReadOnly));
    QVERIFY(docx.readAll().contains("<w:br/>"));
    QVERIFY2(platform::exportPdf(temp.filePath("lists.pdf"), editor.document(), &error),
             qPrintable(error));
  }
};
QTEST_MAIN(ListWorkflowTests)
#include "list_workflow_tests.moc"
