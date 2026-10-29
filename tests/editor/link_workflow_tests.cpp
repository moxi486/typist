#include "DocxWriter.h"
#include "Editor.h"
#include "HeadingLinks.h"
#include "LinkDialog.h"
#include "LinkEditing.h"
#include "LinkNavigation.h"
#include "SpecialContent.h"
#include "ViewSwitch.h"
#include "markdown.h"

#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>
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
QTextCursor find(ui::Editor& editor, const QString& text) { return editor.document()->find(text); }
QPoint point(ui::Editor& editor, const QString& text) {
  auto cursor = find(editor, text);
  cursor.setPosition(cursor.selectionStart() + 1);
  return editor.cursorRect(cursor).center();
}
void context(ui::Editor& editor, const QPoint& point, const QString& action) {
  QTimer::singleShot(0, &editor, [&] {
    auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(menu);
    auto* item = menu->findChild<QAction*>(action);
    QVERIFY(item);
    item->trigger();
    menu->close();
  });
  QContextMenuEvent event(QContextMenuEvent::Mouse, point, editor.viewport()->mapToGlobal(point));
  QApplication::sendEvent(editor.viewport(), &event);
}
void roundTrip(const QString& source) {
  const auto a = core::parseMarkdown(source.toStdString());
  const auto b = core::parseMarkdown(core::serializeMarkdown(a.blocks));
  QVERIFY(a.ok && b.ok && core::astEqual(a.blocks, b.blocks));
  QTextDocument document;
  QVERIFY(editor::loadMarkdown(source, &document, nullptr));
  const auto c = core::parseMarkdown(editor::serializeDocument(&document).toStdString());
  QVERIFY2(c.ok && core::astEqual(a.blocks, c.blocks),
           qPrintable(editor::serializeDocument(&document)));
}
}  // namespace
class LinkWorkflowTests : public QObject {
  Q_OBJECT
 private slots:
  void htmlTocTracksCanonicalIdsWithoutCascadingReplacements() {
    const auto html = render::html("[TOC]\n\n# heading-2\n\n# Alpha\n\n> # Quoted\n\n## Alpha\n");
    QVERIFY(html.contains("href=\"#heading-2\">heading-2"));
    QVERIFY(html.contains("href=\"#alpha\">Alpha"));
    QVERIFY(html.contains("id=\"alpha-1\""));
    QVERIFY(html.contains("href=\"#alpha-1\">Alpha"));
    QVERIFY(html.contains("id=\"_typist:quote:heading-3\""));
  }
  void editingAndRemovingLinksRetainsRichLabelAndOneUndo() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, "Before [**bold** and *italic*](https://old.test) after\n");
    auto cursor = find(editor, "bold");
    cursor.clearSelection();
    cursor = editor::linkSelection(cursor);
    QCOMPARE(cursor.selectedText(), QStringLiteral("bold and italic"));
    const auto before = view.currentMarkdown();
    QVERIFY(editor::replaceLink(cursor, "https://new.test/a b(1)", cursor.selectedText()));
    QVERIFY2(view.currentMarkdown().contains("https://new.test/a%20b%281%29"),
             qPrintable(view.currentMarkdown()));
    const auto styled = core::parseMarkdown(view.currentMarkdown().toStdString());
    bool bold = false, italic = false;
    for (const auto& run : styled.blocks.front().runs) {
      if (run.text == "bold") bold = run.bold;
      if (run.text == "italic") italic = run.italic;
    }
    QVERIFY(bold && italic);
    roundTrip(view.currentMarkdown());
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
    cursor = find(editor, "italic");
    QVERIFY(editor::removeLink(cursor));
    QVERIFY(view.currentMarkdown().contains("Before **bold** and *italic* after"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
  }
  void normalizedPathsRoundTripWithSpacesUnicodeAndParentheses() {
    const auto target = editor::normalizedLink(QStringLiteral("notes/中文 draft(1).md#标题"));
    QVERIFY(target.contains("%20") && target.contains("%28") && target.contains("%29"));
    QCOMPARE(editor::normalizedLink(target), target);
    roundTrip(QStringLiteral("[A **styled** link](%1)\n").arg(target));
  }
  void headingIdsHandleDuplicatesChineseAndCollisions() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view,
            QStringLiteral(
                "# **Hello**, World!\n\n## Hello, World!\n\n## hello-world-1\n\n## 中文 标题\n"));
    const auto links = ui::documentHeadingLinks(editor.document());
    QCOMPARE(links.size(), 4);
    QCOMPARE(links[0].id, QStringLiteral("hello-world"));
    QCOMPARE(links[1].id, QStringLiteral("hello-world-1"));
    QCOMPARE(links[2].id, QStringLiteral("hello-world-1-1"));
    QCOMPARE(links[3].id, QStringLiteral("中文-标题"));
    const auto before = view.currentMarkdown();
    QVERIFY(editor.jumpToHeading("hello-world-1"));
    QCOMPARE(editor.textCursor().block().position(), links[1].position);
    QVERIFY(editor.copyHeadingLink());
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("#hello-world-1"));
    QVERIFY(editor.jumpToHeading(QString::fromLatin1(QUrl::toPercentEncoding("中文-标题"))));
    QVERIFY(!editor.jumpToHeading("missing"));
    QCOMPARE(view.currentMarkdown(), before);
    QVERIFY(!editor.document()->isUndoAvailable());
  }
  void controlClickFollowsWhilePlainClickAndDragKeepEditing() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, "[Follow me](https://example.test) and plain\n");
    QSignalSpy follows(&editor, &ui::Editor::linkActivated);
    const auto at = point(editor, "Follow");
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::NoModifier, at);
    QCOMPARE(follows.size(), 0);
    QTest::mouseClick(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at);
    QCOMPARE(follows.size(), 1);
    QCOMPARE(follows[0][0].toString(), QStringLiteral("https://example.test"));
    QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at);
    QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::ControlModifier, at + QPoint(90, 0));
    QCOMPARE(follows.size(), 1);
    QVERIFY(view.switchView(nullptr));
    view.showActiveIn(&editor);
    QVERIFY(!editor.openCurrentLink());
  }
  void contextCopyEditRemoveAndHeadingCopy() {
    ui::Editor editor;
    ui::ViewSwitch view;
    prepare(editor, view, "# A heading\n\n[**A link**](https://example.test) tail\n");
    const auto before = view.currentMarkdown();
    context(editor, point(editor, "A link"), "link_context_copy");
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("https://example.test"));
    QSignalSpy edits(&editor, &ui::Editor::editLinkRequested);
    context(editor, point(editor, "A link"), "link_context_edit");
    QCOMPARE(edits.size(), 1);
    QCOMPARE(editor.textCursor().selectedText(), QStringLiteral("A link"));
    context(editor, point(editor, "A link"), "link_context_remove");
    QVERIFY(view.currentMarkdown().contains("**A link** tail"));
    editor.undo();
    QCOMPARE(view.currentMarkdown(), before);
    context(editor, point(editor, "A heading"), "heading_context_copy");
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("#a-heading"));
  }
  void dialogOffersHeadingPickerAndValidatesEmptyTarget() {
    ui::LinkDialog dialog("label", {}, {{"first", "First", 0}, {"first-1", "First again", 10}}, {});
    auto* apply = dialog.findChild<QPushButton*>("applyLink");
    QVERIFY(!apply->isEnabled());
    dialog.findChild<QComboBox*>("linkHeading")->setCurrentIndex(2);
    QCOMPARE(dialog.destination(), QStringLiteral("#first-1"));
    QCOMPARE(dialog.label(), QStringLiteral("label"));
    QVERIFY(apply->isEnabled());
  }
  void relativeFileTargetsResolveFromDocumentFolder() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("中文 note(1).md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("# Target\n");
    file.close();
    const auto base = QUrl::fromLocalFile(directory.path() + '/');
    const auto relative = editor::normalizedLink(QStringLiteral("中文 note(1).md#Target"));
    auto resolved = ui::resolveLink(relative, base);
    QCOMPARE(resolved.kind, ui::LinkDestination::Markdown);
    QCOMPARE(resolved.path, path);
    QCOMPARE(resolved.fragment, QStringLiteral("Target"));
    resolved = ui::resolveLink(editor::normalizedLink(QStringLiteral("中文 note(1)")), base);
    QCOMPARE(resolved.kind, ui::LinkDestination::Markdown);
    QCOMPARE(ui::resolveLink(QUrl::fromLocalFile(path).toString(), {}).path, path);
    QCOMPARE(ui::resolveLink(path, {}).path, path);
    QCOMPARE(ui::resolveLink("missing.md", base).kind, ui::LinkDestination::Invalid);
    QCOMPARE(ui::resolveLink("relative.md", {}).kind, ui::LinkDestination::Invalid);
    QCOMPARE(ui::resolveLink("javascript:alert(1)", base).kind, ui::LinkDestination::Invalid);
    QCOMPARE(ui::resolveLink("#target", {}).kind, ui::LinkDestination::Heading);
  }
  void htmlAndWordUseMatchingHeadingDestinations() {
    const auto source = QStringLiteral(
        "[Jump](#hello-world-1) [外部](https://example.test)\n\n# Hello World\n\n## Hello "
        "World\n\n## 中文 标题\n");
    const auto html = render::html(source);
    QVERIFY2(html.contains("id=\"hello-world\""), qPrintable(html));
    QVERIFY(html.contains("id=\"hello-world-1\""));
    QVERIFY(html.contains(QStringLiteral("id=\"中文-标题\"")));
    QVERIFY(html.contains("href=\"#hello-world-1\""));
    QTemporaryDir directory;
    QString error;
    QVERIFY2(platform::exportDocx(directory.filePath("links.docx"), source, &error),
             qPrintable(error));
    QFile file(directory.filePath("links.docx"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(bytes.contains("<w:hyperlink w:anchor=\"heading_2\">"));
    QVERIFY(bytes.contains("w:name=\"heading_2\""));
    QVERIFY(!bytes.contains("Target=\"#hello-world-1\""));
    QVERIFY(bytes.contains("Target=\"https://example.test\""));
    roundTrip(source);
  }
};
QTEST_MAIN(LinkWorkflowTests)
#include "link_workflow_tests.moc"
