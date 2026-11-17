#include "DocxWriter.h"
#include "Editor.h"
#include "PdfWriter.h"
#include "TableEditing.h"
#include "ViewSwitch.h"
#include "markdown.h"

#include <QAction>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextTable>
#include <QTimer>
#include <QToolButton>
#include <QtTest>

using namespace typist;
namespace {
void prepare(ui::Editor& editor, ui::ViewSwitch& view, const QString& source = {}) {
  QVERIFY(view.loadMarkdownIntoBoth(source, nullptr));
  view.showActiveIn(&editor);
  editor.resize(1000, 700);
  editor.show();
  editor.activateWindow();
  editor.setFocus();
  QTest::qWait(5);
}
QString plain(const QTextTableCell& cell) {
  auto cursor = cell.firstCursorPosition();
  cursor.setPosition(cell.lastPosition(), QTextCursor::KeepAnchor);
  return cursor.selectedText()
      .replace(QChar::ParagraphSeparator, '\n')
      .replace(QChar::LineSeparator, '\n');
}
void choose(ui::Editor& editor, QTextTable* table, int row, int column) {
  editor.setTextCursor(table->cellAt(row, column).firstCursorPosition());
}
void select(ui::Editor& editor, QTextTable* table, int row, int column, int lastRow,
            int lastColumn) {
  auto cursor = table->cellAt(row, column).firstCursorPosition();
  cursor.setPosition(table->cellAt(lastRow, lastColumn).lastPosition(), QTextCursor::KeepAnchor);
  editor.setTextCursor(cursor);
}
void paste(ui::Editor& editor, const QString& text) {
  QApplication::clipboard()->setText(text);
  editor.paste();
}
QTextTable* firstTable(QTextDocument* document) {
  for (auto block = document->begin(); block.isValid(); block = block.next())
    if (auto* table = QTextCursor(block).currentTable()) return table;
  return nullptr;
}
bool same(const QString& left, const QString& right) {
  const auto a = core::parseMarkdown(left.toStdString()),
             b = core::parseMarkdown(right.toStdString());
  return a.ok && b.ok && core::astEqual(a.blocks, b.blocks);
}
const QString fixture = QStringLiteral(
    "Before.\n\n| Name | Value |\n|:---|---:|\n| **one** | 1 |\n| two | 2 |\n\nTAIL.\n");
}  // namespace
class TableWorkflowTests : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() { QApplication::setStyle(QStringLiteral("Fusion")); }

  void resizeRetainsRichCellsAlignmentAndDocumentNeighbors() {
    ui::Editor edit;
    ui::ViewSwitch view;
    prepare(edit, view,
            QStringLiteral("Before.\n\n| **Name** | Value | Drop |\n|:---:|---:|---|\n"
                           "| [one](note.md \"Title\")<br>two | *italic* | extra |\n"
                           "| third | 2 | remove |\n\nAfter.\n"));
    auto* table = firstTable(edit.document());
    QVERIFY(table);
    const QString original = view.currentMarkdown();
    auto cursor = table->cellAt(1, 1).firstCursorPosition();
    cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 3);
    const auto first = cursor.selectedText();
    QVERIFY(!editor::resizeTable(cursor, 1, 1));
    QVERIFY(!editor::resizeTable(cursor, 3, editor::kMaxTableColumns + 1));
    QVERIFY(!editor::resizeTable(cursor, 3, 3));
    QVERIFY(!edit.document()->isUndoAvailable());
    QVERIFY(editor::resizeTable(cursor, 5, 2));
    QCOMPARE(cursor.selectedText(), first);
    QCOMPARE(table->rows(), 5);
    QCOMPARE(table->columns(), 2);
    QCOMPARE(table->format().headerRowCount(), 1);
    QCOMPARE(table->cellAt(4, 0).firstCursorPosition().blockFormat().alignment(),
             Qt::Alignment(Qt::AlignHCenter));
    QCOMPARE(table->cellAt(4, 1).firstCursorPosition().blockFormat().alignment(),
             Qt::Alignment(Qt::AlignRight));
    const auto expanded = view.currentMarkdown();
    QVERIFY2(same(expanded, QStringLiteral("Before.\n\n| **Name** | Value |\n|:---:|---:|\n"
                                           "| [one](note.md \"Title\")<br>two | *italic* |\n"
                                           "| third | 2 |\n| | |\n| | |\n\nAfter.\n")),
             qPrintable(expanded));
    edit.undo();
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(!edit.document()->isUndoAvailable());
    edit.redo();
    QCOMPARE(view.currentMarkdown(), expanded);

    cursor = table->cellAt(4, 1).firstCursorPosition();
    QVERIFY(editor::resizeTable(cursor, 2, 1));
    QCOMPARE(cursor.position(), table->cellAt(1, 0).firstPosition());
    edit.undo();
    QCOMPARE(view.currentMarkdown(), expanded);
  }

  void toolbarSizePickerSupportsGridNumbersAndCancellation() {
    ui::Editor edit;
    ui::ViewSwitch view;
    prepare(edit, view, fixture);
    auto* table = firstTable(edit.document());
    QVERIFY(table);
    choose(edit, table, 1, 0);
    auto* button = edit.findChild<QPushButton*>(QStringLiteral("tableDimensions"));
    QVERIFY(button && button->isVisible());
    QCOMPARE(button->text(), QStringLiteral("3 × 2"));
    const auto showPicker = [&](bool accept) {
      QTimer::singleShot(0, &edit, [&, accept] {
        auto* dialog = edit.findChild<QDialog*>(QStringLiteral("resizeTableDialog"));
        QVERIFY(dialog);
        auto reject = qScopeGuard([dialog] { dialog->reject(); });
        auto* grid = dialog->findChild<QTableWidget*>(QStringLiteral("tableResizeGrid"));
        auto* rows = dialog->findChild<QSpinBox*>(QStringLiteral("tableResizeRows"));
        auto* columns = dialog->findChild<QSpinBox*>(QStringLiteral("tableResizeColumns"));
        auto* warning = dialog->findChild<QLabel*>(QStringLiteral("tableResizeWarning"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>();
        QVERIFY(grid && rows && columns && warning && buttons);
        QCOMPARE(rows->value(), 3);
        QCOMPARE(columns->value(), 2);
        auto* apply = buttons->button(QDialogButtonBox::Ok);
        QVERIFY(!apply->isEnabled() && !warning->isVisible());
        QTest::mouseClick(grid->viewport(), Qt::LeftButton, {},
                          grid->visualItemRect(grid->item(1, 0)).center());
        QCOMPARE(rows->value(), 2);
        QCOMPARE(columns->value(), 1);
        QVERIFY(apply->isEnabled() && warning->isVisible());
        if (!accept) return;
        QTest::mouseClick(grid->viewport(), Qt::LeftButton, {},
                          grid->visualItemRect(grid->item(4, 3)).center());
        QCOMPARE(rows->value(), 5);
        QCOMPARE(columns->value(), 4);
        QVERIFY(!warning->isVisible());
        rows->setValue(12);
        columns->setValue(7);
        reject.dismiss();
        apply->click();
      });
      button->click();
    };
    const QString original = view.currentMarkdown();
    showPicker(false);
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(!edit.document()->isUndoAvailable());
    showPicker(true);
    QCOMPARE(table->rows(), 12);
    QCOMPARE(table->columns(), 7);
    QCOMPARE(button->text(), QStringLiteral("12 × 7"));
    edit.undo();
    QCOMPARE(view.currentMarkdown(), original);
  }

  void movesSelectedRowsWithRichContentsInOneUndo() {
    QTemporaryDir resources;
    QVERIFY(resources.isValid());
    QImage image(12, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    QVERIFY(image.save(resources.filePath(QStringLiteral("sample.png"))));
    const auto head = QStringLiteral("Before.\n\n| Name | Notes | Image |\n|:---|:---:|---:|\n");
    const auto first = QStringLiteral(
        "| **First** | [site](note.md \"Description\")<br>Second line | ![](sample.png \"Logo\") "
        "|\n");
    const auto second = QStringLiteral("| Second | *italic* | |\n");
    const auto third = QStringLiteral("| Third | plain | end |\n");
    const auto tail = QStringLiteral("\nAfter.\n");
    ui::Editor edit;
    ui::ViewSwitch view;
    view.setBaseUrl(QUrl::fromLocalFile(resources.path() + '/'));
    prepare(edit, view, head + first + second + third + tail);
    auto* table = firstTable(edit.document());
    QVERIFY(table);
    const auto original = view.currentMarkdown();
    const auto originalImage = table->cellAt(1, 2).firstCursorPosition().block().begin().fragment();
    QVERIFY(originalImage.charFormat().isImageFormat());
    auto selection = table->cellAt(2, 2).lastCursorPosition();
    selection.setPosition(table->cellAt(1, 0).firstPosition(), QTextCursor::KeepAnchor);
    edit.setTextCursor(selection);
    QVERIFY(edit.textCursor().hasComplexSelection());
    auto* up = edit.findChild<QAction*>(QStringLiteral("tableBarMoveRowsUp"));
    auto* down = edit.findChild<QAction*>(QStringLiteral("tableBarMoveRowsDown"));
    QVERIFY(up && down && !up->isEnabled() && down->isEnabled());
    down->trigger();
    const auto moved = view.currentMarkdown();
    QVERIFY2(same(moved, head + third + first + second + tail), qPrintable(moved));
    QCOMPARE(table->format().headerRowCount(), 1);
    const auto movedImage = table->cellAt(2, 2).firstCursorPosition().block().begin().fragment();
    QVERIFY(movedImage.charFormat().isImageFormat());
    QCOMPARE(movedImage.charFormat().property(editor::kLinkTitleProperty).toString(),
             QStringLiteral("Logo"));
    const auto cells = editor::tableCells(edit.textCursor());
    QCOMPARE(cells.row, 2);
    QCOMPARE(cells.rows, 2);
    QCOMPARE(cells.columns, 3);
    QVERIFY(edit.textCursor().position() < edit.textCursor().anchor());
    QVERIFY(up->isEnabled() && !down->isEnabled());
    QVERIFY(!edit.changeTable(editor::TableCommand::MoveRowsDown));
    QCOMPARE(view.currentMarkdown(), moved);
    edit.undo();
    QCOMPARE(view.currentMarkdown(), original);
    edit.redo();
    QCOMPARE(view.currentMarkdown(), moved);
  }

  void movesColumnsWithAlignmentWidthsAndTheCaret() {
    ui::Editor edit;
    ui::ViewSwitch view;
    prepare(edit, view,
            QStringLiteral("| A | B | C |\n|:---|:---:|---:|\n| abc | [label](note.md \"Details\") "
                           "| one<br>two |\n"
                           "| 12 | *italic* | |\n"));
    auto* table = firstTable(edit.document());
    QVERIFY(table);
    auto format = table->format();
    const QList<QTextLength> widths{QTextLength(QTextLength::PercentageLength, 20),
                                    QTextLength(QTextLength::PercentageLength, 30),
                                    QTextLength(QTextLength::PercentageLength, 50)};
    format.setColumnWidthConstraints(widths);
    table->setFormat(format);
    edit.document()->clearUndoRedoStacks();
    const auto original = view.currentMarkdown();
    select(edit, table, 0, 1, 2, 2);
    auto* left = edit.findChild<QAction*>(QStringLiteral("tableBarMoveColumnsLeft"));
    auto* right = edit.findChild<QAction*>(QStringLiteral("tableBarMoveColumnsRight"));
    QVERIFY(left && right && left->isEnabled() && !right->isEnabled());
    left->trigger();
    const auto moved = view.currentMarkdown();
    QVERIFY2(same(moved, QStringLiteral("| B | C | A |\n|:---:|---:|:---|\n"
                                        "| [label](note.md \"Details\") | one<br>two | abc |\n"
                                        "| *italic* | | 12 |\n")),
             qPrintable(moved));
    QCOMPARE(table->format().columnWidthConstraints(),
             (QList<QTextLength>{widths.at(1), widths.at(2), widths.at(0)}));
    const auto cells = editor::tableCells(edit.textCursor());
    QCOMPARE(cells.column, 0);
    QCOMPARE(cells.columns, 2);
    edit.undo();
    QCOMPARE(view.currentMarkdown(), original);
    QCOMPARE(table->format().columnWidthConstraints(), widths);

    const auto cell = table->cellAt(1, 0);
    auto caret = cell.firstCursorPosition();
    caret.setPosition(cell.firstPosition() + 2);
    caret.setPosition(cell.firstPosition() + 1, QTextCursor::KeepAnchor);
    edit.setTextCursor(caret);
    QVERIFY(edit.changeTable(editor::TableCommand::MoveColumnsRight));
    QCOMPARE(editor::tableCells(edit.textCursor()).column, 1);
    QCOMPARE(edit.textCursor().selectedText(), QStringLiteral("b"));
    QVERIFY(edit.textCursor().position() < edit.textCursor().anchor());
    edit.undo();
    QCOMPARE(view.currentMarkdown(), original);
  }

  void spreadsheetPasteCreatesTableAndPreservesLiteralCells() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, QStringLiteral("Before TAIL."));
    auto cursor = editor.textCursor();
    cursor.setPosition(7);
    editor.setTextCursor(cursor);
    const auto original = view.currentMarkdown();
    paste(editor, QStringLiteral("Name\tValue\tNote\r\n**literal**\t$x$\t\"line one\nline "
                                 "two\"\r\npath\\one\tA|B\t`code`\r\n"));
    auto* table = editor.textCursor().currentTable();
    QVERIFY(table);
    QCOMPARE(table->rows(), 3);
    QCOMPARE(table->columns(), 3);
    QCOMPARE(plain(table->cellAt(1, 0)), QStringLiteral("**literal**"));
    QCOMPARE(plain(table->cellAt(1, 1)), QStringLiteral("$x$"));
    QCOMPARE(plain(table->cellAt(1, 2)), QStringLiteral("line one\nline two"));
    QVERIFY(editor.toPlainText().startsWith("Before "));
    QVERIFY(editor.toPlainText().endsWith("TAIL."));
    const auto saved = view.currentMarkdown();
    QTextDocument reloaded;
    QVERIFY(editor::loadMarkdown(saved, &reloaded, nullptr));
    auto* round = firstTable(&reloaded);
    QVERIFY(round);
    for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 3; ++col)
        QCOMPARE(plain(round->cellAt(row, col)), plain(table->cellAt(row, col)));
    QVERIFY2(same(saved, editor::serializeDocument(&reloaded)), qPrintable(saved));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    editor.redo();
    QVERIFY(editor.toPlainText().contains("**literal**"));
  }
  void pastingExpandsInPlaceAndKeepsAlignmentAndNeighbors() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, fixture);
    auto* table = firstTable(editor.document());
    QVERIFY(table);
    choose(editor, table, 1, 1);
    const auto original = view.currentMarkdown();
    paste(editor, QStringLiteral("10\t\"a\tb\"\n20\t\"a\"\"b\"\n30\tlast\n"));
    QCOMPARE(table->rows(), 4);
    QCOMPARE(table->columns(), 3);
    QCOMPARE(plain(table->cellAt(1, 1)), QStringLiteral("10"));
    QCOMPARE(plain(table->cellAt(1, 2)), QStringLiteral("a\tb"));
    QCOMPARE(plain(table->cellAt(2, 2)), QStringLiteral("a\"b"));
    QCOMPARE(plain(table->cellAt(3, 2)), QStringLiteral("last"));
    for (int row = 0; row < 4; ++row)
      QCOMPARE(table->cellAt(row, 1).firstCursorPosition().blockFormat().intProperty(
                   editor::kTableAlignmentProperty),
               3);
    QVERIFY(table->cellAt(0, 2).firstCursorPosition().block().charFormat().boolProperty(
        editor::kPresentationBoldProperty));
    QVERIFY(view.currentMarkdown().startsWith("Before."));
    QVERIFY(view.currentMarkdown().endsWith("TAIL.\n\n"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    editor.redo();
    QCOMPARE(firstTable(editor.document())->rows(), 4);
  }
  void rectangularCopyCutFillAndDeleteRetainStructure() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, fixture);
    auto* table = firstTable(editor.document());
    select(editor, table, 1, 0, 2, 1);
    QVERIFY(editor.textCursor().hasComplexSelection());
    const auto original = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("one\t1\ntwo\t2"));
    QCOMPARE(view.currentMarkdown(), original);
    paste(editor, QStringLiteral("same **text**"));
    for (int row = 1; row < 3; ++row)
      for (int col = 0; col < 2; ++col)
        QCOMPARE(plain(table->cellAt(row, col)), QStringLiteral("same **text**"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    table = firstTable(editor.document());
    select(editor, table, 1, 0, 2, 1);
    QTest::keyClick(&editor, Qt::Key_X, Qt::ControlModifier);
    QCOMPARE(table->rows(), 3);
    QCOMPARE(table->columns(), 2);
    QCOMPARE(plain(table->cellAt(1, 0)), QString());
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    select(editor, table, 1, 0, 2, 1);
    QTest::keyClick(&editor, Qt::Key_Delete);
    QCOMPARE(table->rows(), 3);
    QCOMPARE(plain(table->cellAt(2, 1)), QString());
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
  }
  void rangeCommandsSelectTheCurrentRowCellAndTable() {
    using Range = editor::SelectionRange;
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, fixture);
    auto* table = firstTable(editor.document());
    QVERIFY(table);
    const auto original = view.currentMarkdown();
    choose(editor, table, 1, 1);
    editor.selectRange(Range::LineOrSentence);
    QVERIFY(editor.textCursor().hasComplexSelection());
    const auto cells = editor::tableCells(editor.textCursor());
    QCOMPARE(cells.row, 1);
    QCOMPARE(cells.rows, 1);
    QCOMPARE(cells.column, 0);
    QCOMPARE(cells.columns, 2);
    editor.copy();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("one\t1"));
    choose(editor, table, 2, 0);
    editor.selectRange(Range::StyledScope);
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("two"));
    editor.selectRange(Range::Paragraph);
    const auto selected = editor::selectedMarkdown(editor.textCursor());
    QVERIFY(selected.startsWith(QStringLiteral("| Name | Value |")));
    QVERIFY(selected.contains(QStringLiteral("| **one** | 1 |")));
    QVERIFY(selected.contains(QStringLiteral("| two | 2 |")));
    QVERIFY(!selected.contains(QStringLiteral("Before.")));
    QVERIFY(!selected.contains(QStringLiteral("TAIL.")));
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(!editor.document()->isUndoAvailable());
  }

  void deleteRangesPreserveTableLimitsAndUndoStructure() {
    using Range = editor::SelectionRange;
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view,
            "Before.\n\n| Key | Value |\n|---|---|\n| one | 1 |\n| two | 2 |\n| three | 3 "
            "|\n\nAfter.\n");
    auto* table = firstTable(editor.document());
    QVERIFY(table);
    const auto original = view.currentMarkdown();
    QSignalSpy edits(editor.document(), &QTextDocument::contentsChanged);
    choose(editor, table, 0, 0);
    QVERIFY(!editor.deleteRange(Range::LineOrSentence));
    QCOMPARE(edits.count(), 0);
    QVERIFY(!editor.document()->isUndoAvailable());

    select(editor, table, 1, 0, 2, 1);
    QVERIFY(editor.deleteRange(Range::LineOrSentence));
    QCOMPARE(table->rows(), 2);
    QCOMPARE(plain(table->cellAt(1, 0)), QStringLiteral("three"));
    const auto reduced = view.currentMarkdown();
    choose(editor, table, 1, 0);
    QVERIFY(!editor.deleteRange(Range::LineOrSentence));
    QCOMPARE(view.currentMarkdown(), reduced);
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);

    table = firstTable(editor.document());
    choose(editor, table, 1, 0);
    QVERIFY(editor.deleteRange(Range::StyledScope));
    QCOMPARE(plain(table->cellAt(1, 0)), QString());
    QCOMPARE(plain(table->cellAt(1, 1)), QStringLiteral("1"));
    QCOMPARE(table->rows(), 4);
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);

    table = firstTable(editor.document());
    choose(editor, table, 1, 0);
    QVERIFY(editor.deleteRange(Range::Paragraph));
    QVERIFY(!firstTable(editor.document()));
    QVERIFY(view.currentMarkdown().contains(QStringLiteral("Before.")));
    QVERIFY(view.currentMarkdown().contains(QStringLiteral("After.")));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
  }

  void explicitHtmlCopyKeepsOnlyTheSelectedTableCells() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view,
            "Before.\n\n| Name | Value | Outside |\n|---|---|---|\n"
            "| **one** | a<br>b | excluded |\n| two | 2 | excluded |\n\nAfter.\n");
    auto* table = firstTable(editor.document());
    QVERIFY(table);
    select(editor, table, 0, 0, 2, 1);
    QVERIFY(editor.textCursor().hasComplexSelection());
    const auto original = view.currentMarkdown();
    editor.copySelection(ui::Editor::CopyFormat::WithoutTheme);
    const auto* mime = QApplication::clipboard()->mimeData();
    const auto html = mime->html();
    QVERIFY(html.contains(QStringLiteral("<table>")));
    QVERIFY(html.contains(QStringLiteral("<th>Name</th>")));
    QVERIFY(html.contains(QStringLiteral("<strong>one</strong>")));
    QVERIFY(html.contains(QStringLiteral("a<br")));
    QVERIFY(!html.contains(QStringLiteral("excluded")));
    QVERIFY(!html.contains(QStringLiteral("Outside")));
    QCOMPARE(mime->text(), QStringLiteral("Name\tValue\none\t\"a\nb\"\ntwo\t2"));
    QCOMPARE(view.currentMarkdown(), original);
  }
  void toolbarCommandsProtectHeaderAndAlignSelectedColumns() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, fixture);
    auto* table = firstTable(editor.document());
    choose(editor, table, 0, 0);
    auto* bar = editor.findChild<QFrame*>(QStringLiteral("tableBar"));
    QVERIFY(bar && bar->isVisible());
    QVERIFY(bar->geometry().bottom() < editor.viewport()->geometry().top());
    auto* removeRow = bar->findChild<QAction*>(QStringLiteral("tableBarDeleteRow"));
    QVERIFY(removeRow && !removeRow->isEnabled());
    QVERIFY(!editor.changeTable(editor::TableCommand::DeleteRow));
    const auto original = view.currentMarkdown();
    select(editor, table, 1, 0, 1, 1);
    bar->findChild<QAction*>(QStringLiteral("tableBarAlignCenter"))->trigger();
    for (int row = 0; row < 3; ++row)
      for (int col = 0; col < 2; ++col)
        QCOMPARE(table->cellAt(row, col).firstCursorPosition().blockFormat().intProperty(
                     editor::kTableAlignmentProperty),
                 2);
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    choose(editor, table, 1, 1);
    bar->findChild<QAction*>(QStringLiteral("tableBarRowBelow"))->trigger();
    QCOMPARE(table->rows(), 4);
    QCOMPARE(table->cellAt(2, 1).firstCursorPosition().blockFormat().intProperty(
                 editor::kTableAlignmentProperty),
             3);
    QVERIFY(!table->cellAt(2, 1).firstCursorPosition().block().charFormat().boolProperty(
        editor::kPresentationBoldProperty));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    choose(editor, table, 1, 0);
    bar->findChild<QAction*>(QStringLiteral("tableBarColumnAfter"))->trigger();
    QCOMPARE(table->columns(), 3);
    QVERIFY(table->cellAt(0, 1).firstCursorPosition().block().charFormat().boolProperty(
        editor::kPresentationBoldProperty));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
  }
  void tabNavigationAddsOneStyledRowAndExitsBeforeHeading() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, fixture);
    auto* table = firstTable(editor.document());
    choose(editor, table, 1, 0);
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(table->cellAt(editor.textCursor()).column(), 1);
    QTest::keyClick(&editor, Qt::Key_Backtab);
    QCOMPARE(table->cellAt(editor.textCursor()).column(), 0);
    choose(editor, table, 2, 1);
    const auto original = view.currentMarkdown();
    QTest::keyClick(&editor, Qt::Key_Tab);
    QCOMPARE(table->rows(), 4);
    QCOMPARE(table->cellAt(editor.textCursor()).row(), 3);
    QCOMPARE(editor.textCursor().blockFormat().intProperty(editor::kTableAlignmentProperty), 1);
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    choose(editor, table, 1, 0);
    QTest::keyClick(&editor, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(!editor.textCursor().currentTable());
    QVERIFY(editor.textCursor().position() > table->lastPosition());
    QCOMPARE(view.currentMarkdown(), original);
    prepare(editor, view, QStringLiteral("| A | B |\n|---|---|\n| one | two |\n\n## Following\n"));
    table = firstTable(editor.document());
    choose(editor, table, 1, 0);
    const auto before = view.currentMarkdown();
    QVERIFY(editor.exitTable());
    QVERIFY(!editor.textCursor().currentTable());
    QVERIFY(view.currentMarkdown().contains("## Following"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
  }
  void copyingWholeTableAndSourceSwitchPreserveRichContents() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view,
            QStringLiteral("Before\n\n| **Name** | Link |\n|:---|---:|\n| *one* | "
                           "[site](https://example.com) |\n\nTAIL\n"));
    auto* table = firstTable(editor.document());
    choose(editor, table, 1, 1);
    const auto original = view.currentMarkdown();
    const auto undo = editor.document()->availableUndoSteps();
    QVERIFY(editor.copyTable());
    const auto copied = QApplication::clipboard()->text();
    QVERIFY(copied.startsWith("| **Name**"));
    QVERIFY(copied.contains("[site](https://example.com)"));
    QVERIFY(!copied.contains("Before") && !copied.contains("TAIL"));
    QVERIFY(editor.copyTable(true));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Name\tLink\none\tsite"));
    editor.setDarkTheme(true);
    editor.resize(800, 600);
    QCOMPARE(editor.document()->availableUndoSteps(), undo);
    QCOMPARE(view.currentMarkdown(), original);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QVERIFY(!editor.findChild<QFrame*>(QStringLiteral("tableBar"))->isVisible());
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QCOMPARE(view.currentMarkdown(), original);
  }
  void malformedOversizedAndMixedClipboardDataAreHandledSafely() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, fixture);
    auto* table = firstTable(editor.document());
    choose(editor, table, 1, 0);
    const auto original = view.currentMarkdown();
    QSignalSpy failures(&editor, &ui::Editor::pasteRejected);
    paste(editor, QStringLiteral("one\t\"unfinished"));
    QCOMPARE(failures.size(), 1);
    QCOMPARE(view.currentMarkdown(), original);
    paste(editor, QStringLiteral("one\t") + QStringLiteral("x\t").repeated(41));
    QCOMPARE(failures.size(), 2);
    QCOMPARE(view.currentMarkdown(), original);
    auto* mime = new QMimeData;
    mime->setText(QStringLiteral("a\tb\nc\td"));
    mime->setHtml(QStringLiteral("<h2>Web title</h2>"));
    QImage image(10, 10, QImage::Format_RGB32);
    image.fill(Qt::red);
    mime->setImageData(image);
    QApplication::clipboard()->setMimeData(mime);
    editor.paste();
    QCOMPARE(plain(table->cellAt(1, 0)), QStringLiteral("a"));
    QCOMPARE(plain(table->cellAt(2, 1)), QStringLiteral("d"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    prepare(editor, view, QStringLiteral("```text\ncode\n```"));
    paste(editor, QStringLiteral("one\ttwo\nthree\tfour"));
    QVERIFY(!firstTable(editor.document()));
    QVERIFY(view.currentMarkdown().contains("one\ttwo"));
  }
  void selectedColumnDeletionKeepsMinimumShapeAndOneUndo() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view,
            QStringLiteral("| A | B | C |\n|---|---|---|\n| a | b | c |\n| d | e | f |\n"));
    auto* table = firstTable(editor.document());
    const auto original = view.currentMarkdown();
    select(editor, table, 1, 1, 2, 2);
    QVERIFY(editor.changeTable(editor::TableCommand::DeleteColumn));
    QCOMPARE(table->columns(), 1);
    QCOMPARE(plain(table->cellAt(2, 0)), QStringLiteral("d"));
    QVERIFY(!editor.changeTable(editor::TableCommand::DeleteColumn));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), original);
    table = firstTable(editor.document());
    select(editor, table, 1, 0, 2, 2);
    QVERIFY(!editor.changeTable(editor::TableCommand::DeleteRow));
    QCOMPARE(view.currentMarkdown(), original);
  }
  void literalSpreadsheetCellsRoundTripAndExport() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view);
    paste(editor, QStringLiteral("Name\tValue\n**stars**\t$x^2$\n<em>plain</em>\tA|B\n"));
    const auto markdown = view.currentMarkdown();
    const auto parsed = core::parseMarkdown(markdown.toStdString());
    QVERIFY(parsed.ok);
    QVERIFY(same(markdown, QString::fromStdString(core::serializeMarkdown(parsed.blocks))));
    const auto html = QString::fromStdString(core::toHtml(markdown.toStdString()));
    QVERIFY(html.contains("**stars**"));
    QVERIFY(!html.contains("<strong>stars</strong>"));
    QVERIFY(html.contains("&lt;em&gt;plain&lt;/em&gt;"));
    QTemporaryDir temp;
    QString error;
    QVERIFY2(platform::exportDocx(temp.filePath("table.docx"), markdown, &error),
             qPrintable(error));
    QFile word(temp.filePath("table.docx"));
    QVERIFY(word.open(QIODevice::ReadOnly));
    QVERIFY(word.readAll().contains("**stars**"));
    QVERIFY2(platform::exportPdf(temp.filePath("table.pdf"), editor.document(), &error),
             qPrintable(error));
  }
};
QTEST_MAIN(TableWorkflowTests)
#include "table_workflow_tests.moc"
