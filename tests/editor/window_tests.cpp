#include "DocxWriter.h"
#include "Editor.h"
#include "FindBar.h"
#include "MainWindow.h"
#include "PdfWriter.h"
#include "Settings.h"
#include "SpecialContent.h"
#include "TableEditing.h"
#include "Toc.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextList>
#include <QTextTable>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QXmlStreamReader>
#include <QtTest>

namespace {
QAction* actionBySource(QWidget& widget, const QString& source) {
  for (auto* action : widget.findChildren<QAction*>())
    if (action->property("translationSource").toString() == source) return action;
  return nullptr;
}
}  // namespace

class WindowTests : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    QApplication::setOrganizationName(QStringLiteral("Typist"));
    QApplication::setApplicationName(QStringLiteral("Typist"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/packaging/linux/typist.png")));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
  }

  void windowLanguageThemeAndUndo() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 820);
    window.show();
    QTest::qWait(30);
    QVERIFY(window.width() >= 760);
    QVERIFY(window.height() >= 540);
    QVERIFY(!window.windowIcon().isNull());
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QTextCursor cursor = editor->textCursor();
    cursor.insertText(QStringLiteral("中文 and English"));
    const QString before = editor->toPlainText();
    QVERIFY(editor->document()->isUndoAvailable());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(window.menuBar()->actions().first()->text(), QStringLiteral("文件"));
    QCOMPARE(editor->toPlainText(), before);
    QVERIFY(editor->document()->isUndoAvailable());
    QCOMPARE(typist::platform::languagePreference(), QStringLiteral("zh"));
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QVERIFY(window.menuBar()->palette().color(QPalette::WindowText).lightness() > 160);
    QVERIFY(window.menuBar()->palette().color(QPalette::ButtonText).lightness() > 160);
    QVERIFY(window.styleSheet().contains(QStringLiteral("QMenuBar::item:selected")));
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QCOMPARE(window.menuBar()->actions().first()->text(), QStringLiteral("File"));
    editor->undo();
    QCOMPARE(editor->toPlainText(), QString());
  }

  void typographyDoesNotChangeMarkdownSemantics() {
    const QString source = QStringLiteral(
        "# A **bold** heading\n\nA paragraph.\n\n"
        "## A second heading\n\n- plain item\n- **bold** item\n\n"
        "| Plain | **Bold** |\n|---|---|\n| one | two |\n");
    QTextDocument document;
    QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
    const auto original = typist::core::parseMarkdown(source.toStdString());
    const auto saved =
        typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
    QVERIFY(typist::core::astEqual(original.blocks, saved.blocks));
    QTextBlock heading = document.begin();
    QCOMPARE(heading.blockFormat().headingLevel(), 1);
    QVERIFY(heading.begin().fragment().charFormat().fontPointSize() >= 24);
    QVERIFY(heading.next().blockFormat().bottomMargin() >= 10);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
      if (block.textList()) QCOMPARE(block.blockFormat().headingLevel(), 0);
    QCOMPARE(typist::ui::outlineFromDocument(&document).size(), 2);
  }

  void wideWindowOpensReadmeAndResizes() {
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1280, 900);
    window.show();
    QTest::qWait(30);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/README.md"));
    QTest::qWait(50);
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    QVERIFY(editor && editor->toPlainText().startsWith(QStringLiteral("Typist")));
    const QString original = typist::editor::serializeDocument(editor->document());
    QVERIFY(window.findChildren<QToolBar*>().isEmpty());
    auto* sidebarButton = window.findChild<QToolButton*>(QStringLiteral("sidebarToggle"));
    auto* sourceButton = window.findChild<QToolButton*>(QStringLiteral("sourceToggle"));
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("sidebarTabs"));
    QVERIFY(sidebarButton && sourceButton && tabs);
    QTest::mouseClick(sidebarButton, Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(tabs->isVisible());
    tabs->setCurrentIndex(1);
    for (const QSize size :
         {QSize(960, 700), QSize(1440, 900), QSize(800, 650), QSize(1180, 820)}) {
      window.resize(size);
      QTest::qWait(20);
      editor->repaint();
      QVERIFY2(editor->viewport()->width() >= qMin(800, size.width() - 320),
               qPrintable(QStringLiteral("window=%1 editor=%2 viewport=%3")
                              .arg(window.width())
                              .arg(editor->width())
                              .arg(editor->viewport()->width())));
    }
    editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum());
    QTest::qWait(20);
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QTest::mouseClick(sourceButton, Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(editor->toPlainText().startsWith(QStringLiteral("# Typist")));
    QTest::mouseClick(sourceButton, Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(editor->toPlainText().startsWith(QStringLiteral("Typist")));
  }

  void editingActionsReplaceAndOutline() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1100, 800);
    window.show();
    auto* editor = static_cast<typist::ui::Editor*>(
        window.findChild<QTextEdit*>(QStringLiteral("writingEditor")));
    QVERIFY(editor);
    window.activateWindow();
    editor->setFocus();
    QTest::qWait(30);
    QTest::keyClicks(editor, "## Title");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "cat cat");
    QTest::keyClick(editor, Qt::Key_H, Qt::ControlModifier);
    auto* pattern = window.findChild<QLineEdit*>(QStringLiteral("findPattern"));
    auto* replacement = window.findChild<QLineEdit*>(QStringLiteral("replaceText"));
    QVERIFY(pattern && replacement && replacement->isVisible());
    pattern->setText(QStringLiteral("cat"));
    replacement->setText(QStringLiteral("dog"));
    window.findChild<QPushButton*>(QStringLiteral("replaceAll"))->click();
    QVERIFY(editor->toPlainText().contains(QStringLiteral("dog dog")));
    window.findChild<QAction*>(QStringLiteral("focusMode"))->trigger();
    window.findChild<QAction*>(QStringLiteral("typewriterMode"))->trigger();
    QVERIFY(editor->focusMode());
    QVERIFY(editor->typewriterMode());
    window.findChild<QAction*>(QStringLiteral("focusMode"))->trigger();
    window.findChild<QAction*>(QStringLiteral("typewriterMode"))->trigger();
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("heading_2"))->text(),
             QStringLiteral("2 级标题"));
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("format_bold"))->text(),
             QStringLiteral("加粗"));
    window.findChild<QAction*>(QStringLiteral("newDocument"))->trigger();
    QCOMPARE(editor->toPlainText(), QString());
    QVERIFY(!editor->document()->isUndoAvailable());
  }

  void cancelAndDiscardNeverOverwriteDocument() {
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/original.md");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("original\n");
    file.close();
    typist::MainWindow window;
    window.show();
    window.openFile(path);
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(false);
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral(" edited"));
    bool cancelClicked = false;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (dialog) {
        cancelClicked = true;
        dialog->button(QMessageBox::Cancel)->click();
      }
    });
    window.findChild<QAction*>(QStringLiteral("newDocument"))->trigger();
    QVERIFY(cancelClicked);
    QVERIFY(editor->toPlainText().contains(QStringLiteral("edited")));
    bool discardClicked = false;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (dialog) {
        discardClicked = true;
        dialog->button(QMessageBox::Discard)->click();
      }
    });
    window.close();
    QVERIFY(discardClicked);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("original\n"));
    file.close();
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }

  void reopeningCurrentFileFlushesBeforeReading() {
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/reopen.md");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("before\n");
    file.close();
    typist::MainWindow window;
    window.show();
    window.openFile(path);
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(true);
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral(" after"));
    window.openFile(path);
    QCOMPARE(editor->toPlainText(), QStringLiteral("before after"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("before after"));
    window.close();
  }

  void docxHasValidRunsAndReadableStyles() {
    const QString path = m_settings.path() + QStringLiteral("/layout.docx");
    QString error;
    QVERIFY(typist::platform::exportDocx(
        path,
        QStringLiteral("# A *styled* heading\n\n[Link](https://example.com)\n\n"
                       "- [x] Done\n\n> A **quote**\n\n```cpp\nfirst\nsecond\n```\n"),
        &error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray zip = file.readAll();
    const int start = zip.indexOf("<w:document ");
    const int end = zip.indexOf("</w:document>", start);
    QVERIFY(start > 0 && end > start);
    QXmlStreamReader xml(zip.mid(start, end + 13 - start));
    QStringList stack;
    bool sawBreak = false;
    while (!xml.atEnd()) {
      xml.readNext();
      if (xml.isStartElement()) {
        if (xml.name() == QStringLiteral("br")) {
          QCOMPARE(stack.last(), QStringLiteral("r"));
          sawBreak = true;
        }
        stack.append(xml.name().toString());
      } else if (xml.isEndElement()) {
        stack.removeLast();
      } else if (xml.isCharacters() && !xml.isWhitespace()) {
        QCOMPARE(stack.last(), QStringLiteral("t"));
      }
    }
    QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
    QVERIFY(sawBreak);
    QVERIFY(zip.contains("w:eastAsia=\"Microsoft YaHei\""));
    QVERIFY(zip.contains("w:line=\"360\""));
    QVERIFY(zip.contains("<w:keepNext/>"));
    QVERIFY(zip.contains("<w:widowControl/>"));
    QVERIFY(!zip.contains("&lt;w:r"));
  }

  void editingFeaturesVisuals() {
    const QString out = qEnvironmentVariable("TYPIST_VISUAL_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_VISUAL_OUTPUT to retain UI specimens.");
    QDir().mkpath(out);
    typist::platform::setLanguagePreference(QStringLiteral("zh"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    QSettings().setValue(QStringLiteral("editor/focusMode"), false);
    QSettings().setValue(QStringLiteral("editor/typewriterMode"), false);
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 820);
    window.show();
    window.activateWindow();
    auto* editor = static_cast<typist::ui::Editor*>(
        window.findChild<QTextEdit*>(QStringLiteral("writingEditor")));
    editor->setFocus();
    QTest::qWait(20);
    QTest::keyClicks(editor, "# Writing that works");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Write **bold ideas**, *quiet details*, and `inline code` directly.");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "## One continuous workflow");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "- [x] Markdown renders while you type");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Tables, replacements and undo work together");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "> Keep your focus on the next sentence.");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Search a word, replace it, or enter focus mode with F8.");
    editor->moveCursor(QTextCursor::Start);
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/editing-light-zh.png")));
    auto* paragraphMenu =
        qobject_cast<QMenu*>(window.findChild<QAction*>(QStringLiteral("heading_2"))->parent());
    QVERIFY(paragraphMenu);
    paragraphMenu->ensurePolished();
    paragraphMenu->adjustSize();
    QVERIFY(paragraphMenu->grab().save(out + QStringLiteral("/paragraph-menu-zh.png")));
    window.findChild<QAction*>(QStringLiteral("replace"))->trigger();
    window.findChild<QLineEdit*>(QStringLiteral("findPattern"))->setText(QStringLiteral("word"));
    window.findChild<QLineEdit*>(QStringLiteral("replaceText"))->setText(QStringLiteral("idea"));
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/editing-replace-zh.png")));
    window.findChild<typist::ui::FindBar*>()->closeBar();
    window.findChild<QAction*>(QStringLiteral("focusMode"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    editor->setFocus();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/editing-focus-dark.png")));
    window.findChild<QAction*>(QStringLiteral("focusMode"))->setChecked(false);
  }

  void exportVisualFixtures() {
    const QString out = qEnvironmentVariable("TYPIST_VISUAL_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_VISUAL_OUTPUT to retain export specimens.");
    QDir().mkpath(out);
    const QString source = QStringLiteral(
        "# A better place to write\n\nTypist · 专注写作，让想法自然成形。\n\n"
        "## Thoughtful typography / 清晰的排版\n\n"
        "Readable paragraphs, **strong ideas**, *small details*, and `inline code` make a document "
        "easy to follow. "
        "中文和 English 在同一段落中保持清晰、舒适的阅读节奏。\n\n"
        "- A comfortable line length and generous spacing\n"
        "- 中文界面与英文界面即时切换，保留正在编辑的内容\n"
        "- Export the same document to PDF and Word\n\n"
        "> Good writing starts with a little space to think.\n\n"
        "### A small checklist\n\n- [x] Find your focus\n- [ ] Write the next paragraph\n\n"
        "| Feature | Experience |\n| --- | --- |\n| Writing | Clean, comfortable and focused |\n"
        "| 中文支持 | 简体中文界面与中英混排 |\n| Export | PDF and DOCX with clear hierarchy |\n\n"
        "```cpp\n// Start with one idea.\n\nint main() {\n  return 0;\n}\n```\n");
    QFile input(out + QStringLiteral("/writing-sample.md"));
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(source.toUtf8());
    input.close();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 820);
    window.openFile(input.fileName());
    window.show();
    QTest::qWait(50);
    QVERIFY(window.grab().save(out + QStringLiteral("/interface-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/interface-dark-zh.png")));
    window.findChild<QToolButton*>(QStringLiteral("sidebarToggle"))->click();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/interface-sidebar-dark.png")));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/interface-sidebar-light.png")));
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/README.md"));
    QTest::qWait(40);
    QVERIFY(window.grab().save(out + QStringLiteral("/readme-wide-light.png")));
    QTextDocument document;
    QString error;
    QVERIFY(typist::editor::loadMarkdown(source, &document, &error));
    typist::platform::PdfPageOptions options;
    options.header = QStringLiteral("Typist / Writing sample");
    options.footer = QStringLiteral("#");
    QVERIFY2(typist::platform::exportPdf(out + QStringLiteral("/writing-sample.pdf"), &document,
                                         options, &error),
             qPrintable(error));
    QVERIFY2(
        typist::platform::exportDocx(out + QStringLiteral("/writing-sample.docx"), source, &error),
        qPrintable(error));
    QFile readme(QStringLiteral(TYPIST_SOURCE_DIR "/README.md"));
    QVERIFY(readme.open(QIODevice::ReadOnly));
    const QString longSource = QString::fromUtf8(readme.readAll());
    QVERIFY(typist::editor::loadMarkdown(longSource, &document, &error));
    QVERIFY(typist::platform::exportPdf(out + QStringLiteral("/readme.pdf"), &document, options,
                                        &error));
    QVERIFY(typist::platform::exportDocx(out + QStringLiteral("/readme.docx"), longSource, &error));
  }
  void advancedContentVisuals() {
    const QString out = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_ADVANCED_OUTPUT to retain advanced UI specimens.");
    QDir().mkpath(out);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 1000);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/advanced.md"));
    window.show();
    QTest::qWait(40);
    QVERIFY(window.grab().save(out + QStringLiteral("/advanced-window-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/advanced-window-dark-zh.png")));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    QTimer::singleShot(10, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (dialog) {
        dialog->grab().save(out + QStringLiteral("/advanced-math-dialog-zh.png"));
        dialog->reject();
      }
    });
    window.findChild<QAction*>(QStringLiteral("insert_math"))->trigger();
  }

  void paragraphSourceSaveAutosaveAndViewSwitch() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("paragraph.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Original **word**.\n\nTail.\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(path);
    window.show();
    QTest::qWait(10);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QVERIFY(editor->startParagraphSourceEdit());
    editor->finishParagraphSourceEdit(false);
    QVERIFY(!window.windowTitle().contains(QLatin1Char('*')));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* input = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(input);
    auto* title = editor->findChild<QLabel*>(QStringLiteral("paragraphSourceTitle"));
    QVERIFY(title);
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QTest::qWait(5);
    QCOMPARE(title->text(), QStringLiteral("Paragraph Markdown"));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QTest::qWait(5);
    QCOMPARE(title->text(), QStringLiteral("段落 Markdown"));
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(!window.windowTitle().contains(QLatin1Char('*')));
    input->setPlainText(QStringLiteral("Saved **snapshot**."));
    QVERIFY(QMetaObject::invokeMethod(&window, "save", Qt::DirectConnection));
    QVERIFY(!editor->document()->isUndoAvailable());
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains(QStringLiteral("Original **word**")));
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("Saved **snapshot**"));
    file.close();
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(true);
    input->setPlainText(QStringLiteral("Autosaved **snapshot**."));
    QVERIFY(QMetaObject::invokeMethod(&window, "autoSaveNow", Qt::DirectConnection));
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("Autosaved **snapshot**"));
    file.close();
    input->setPlainText(QStringLiteral("Final **snapshot**."));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(!editor->paragraphSourceActive());
    QVERIFY(editor->toPlainText().contains(QStringLiteral("Final **snapshot**")));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains(QStringLiteral("Final **snapshot**")));
  }
  void paragraphDraftSurvivesCancelledNavigation() {
    QSettings().setValue(QStringLiteral("editor/autoSave"), false);
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("protected.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Protected paragraph.\n");
    file.close();
    typist::MainWindow window;
    window.openFile(path);
    window.show();
    QTest::qWait(10);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* input = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    input->setPlainText(QStringLiteral("Pending **draft**."));
    QTimer::singleShot(10, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (box) box->button(QMessageBox::Cancel)->click();
    });
    window.findChild<QAction*>(QStringLiteral("newDocument"))->trigger();
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(input->toPlainText(), QStringLiteral("Pending **draft**."));
    QTimer::singleShot(10, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (box) box->button(QMessageBox::Discard)->click();
    });
    window.findChild<QAction*>(QStringLiteral("newDocument"))->trigger();
    QVERIFY(!editor->paragraphSourceActive());
    QVERIFY(editor->toPlainText().isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("Protected paragraph.\n"));
    file.close();
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }
  void syntaxAndParagraphVisuals() {
    const QString out = qEnvironmentVariable("TYPIST_SYNTAX_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_SYNTAX_OUTPUT to retain code and paragraph specimens.");
    QDir().mkpath(out);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    QSettings().setValue(QStringLiteral("editor/syntaxHighlight"), true);
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 950);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/syntax.md"));
    window.show();
    QTest::qWait(40);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QVERIFY(window.grab().save(out + QStringLiteral("/syntax-window-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/syntax-window-dark-zh.png")));
    editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(1)));
    QVERIFY(editor->startParagraphSourceEdit());
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/paragraph-source-dark-zh.png")));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    QTest::qWait(20);
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(window.grab().save(out + QStringLiteral("/paragraph-source-light-zh.png")));
    editor->finishParagraphSourceEdit(false);
  }
  void codeCommandsPasteLanguageAndSave() {
    QSettings().setValue(QStringLiteral("editor/autoSave"), false);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("code.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("```cpp\nint value;\n```\n\nBody.\n");
    file.close();
    typist::MainWindow window;
    window.openFile(path);
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->setFocus();
    editor->moveCursor(QTextCursor::Start);
    QTest::qWait(20);
    window.findChild<QAction*>(QStringLiteral("copyCode"))->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("int value;\n"));
    QVERIFY(!editor->document()->isUndoAvailable());
    auto* combo = editor->findChild<QComboBox*>(QStringLiteral("codeBlockLanguage"));
    QVERIFY(combo && combo->isVisible());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QTest::qWait(10);
    QCOMPARE(editor->findChild<QPushButton*>(QStringLiteral("copyCodeBlock"))->text(),
             QStringLiteral("已复制"));
    QCOMPARE(editor->findChild<QPushButton*>(QStringLiteral("exitCodeBlock"))->text(),
             QStringLiteral("退出代码块"));
    combo->setFocus();
    combo->lineEdit()->selectAll();
    QTest::keyClicks(combo->lineEdit(), "python");
    QTest::keyClick(combo->lineEdit(), Qt::Key_Return);
    QCOMPARE(editor->document()->blockCount(), 2);
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .startsWith("```python\nint value;\n```"));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QTest::qWait(10);
    QCOMPARE(editor->findChild<QPushButton*>(QStringLiteral("exitCodeBlock"))->text(),
             QStringLiteral("Exit code"));
    QApplication::clipboard()->setText(QStringLiteral("# Heading\n**literal**\n"));
    auto* paste = actionBySource(window, QStringLiteral("Paste Markdown"));
    QVERIFY(paste);
    paste->trigger();
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains("# Heading\n**literal**\nint value;"));
    for (auto block = editor->document()->begin(); block.next().isValid(); block = block.next())
      QCOMPARE(block.blockFormat().headingLevel(), 0);
    const auto snapshot = typist::editor::serializeDocument(editor->document());
    const auto save = actionBySource(window, QStringLiteral("Save"));
    QVERIFY(save);
    save->trigger();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), snapshot.toUtf8());
    file.close();
    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("Body."));
    QVERIFY(!editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), snapshot);
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }
  void codeToolbarVisuals() {
    const QString out = qEnvironmentVariable("TYPIST_CODE_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_CODE_OUTPUT to retain code toolbar specimens.");
    QDir().mkpath(out);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 850);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/code-writing.md"));
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto block = editor->document()->begin();
    while (block.isValid() && !block.blockFormat().hasProperty(typist::editor::kCodeBlockProperty))
      block = block.next();
    QVERIFY(block.isValid());
    editor->setTextCursor(QTextCursor(block));
    editor->setFocus();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/code-toolbar-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/code-toolbar-dark-zh.png")));
    window.resize(800, 650);
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/code-toolbar-compact-zh.png")));
  }
  void listStatusLanguageSaveAndSource() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("list.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("- [x] **Done**\n- [ ] Next\n\nTAIL\n");
    file.close();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(path);
    window.resize(1080, 800);
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    auto cursor = editor->textCursor();
    cursor.setPosition(0);
    cursor.setPosition(editor->document()->findBlockByNumber(1).position() + 2,
                       QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    const auto original = typist::editor::serializeDocument(editor->document());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("task_done"))->text(),
             QStringLiteral("标记为已完成"));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QVERIFY(!editor->document()->isUndoAvailable());
    window.findChild<QAction*>(QStringLiteral("task_done"))->trigger();
    const auto saved = typist::editor::serializeDocument(editor->document());
    QVERIFY(saved.contains("- [x] Next"));
    QVERIFY(saved.contains("**Done**"));
    QVERIFY(QMetaObject::invokeMethod(&window, "save", Qt::DirectConnection));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), saved.toUtf8());
    file.close();
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(!editor->changeTaskStatus(typist::editor::TaskStatus::Todo));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), saved);
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("task_done"))->text(),
             QStringLiteral("Mark as Complete"));
    editor->document()->setModified(false);
  }
  void listWorkflowVisuals() {
    const auto output = QString::fromUtf8(qgetenv("TYPIST_LIST_VISUAL_DIR"));
    if (output.isEmpty()) QSKIP("Set TYPIST_LIST_VISUAL_DIR to capture list specimens");
    QVERIFY(QDir().mkpath(output));
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/list-writing.md"));
    window.resize(1180, 940);
    window.show();
    window.activateWindow();
    QTest::qWait(50);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(window.grab().save(output + "/list-light.png"));
    QString error;
    QVERIFY2(typist::platform::exportPdf(output + "/list.pdf", editor->document(), &error),
             qPrintable(error));
    QVERIFY2(
        typist::platform::exportDocx(output + "/list.docx",
                                     typist::editor::serializeDocument(editor->document()), &error),
        qPrintable(error));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(30);
    QVERIFY(window.grab().save(output + "/list-dark-zh.png"));
    auto* menu =
        qobject_cast<QMenu*>(window.findChild<QAction*>(QStringLiteral("task_done"))->parent());
    QVERIFY(menu);
    menu->popup(window.mapToGlobal(QPoint(200, 75)));
    QTest::qWait(30);
    QVERIFY(menu->grab().save(output + "/list-task-menu-zh.png"));
    menu->close();
  }
  void tableToolbarLanguagePasteSaveAndSource() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("table.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Before\n\n| Name | Value |\n|---|---:|\n| one | 1 |\n| two | 2 |\n\nTAIL\n");
    file.close();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(path);
    window.resize(1000, 750);
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QTextTable* table = nullptr;
    for (auto block = editor->document()->begin(); block.isValid() && !table; block = block.next())
      table = QTextCursor(block).currentTable();
    QVERIFY(table);
    editor->setTextCursor(table->cellAt(1, 0).firstCursorPosition());
    editor->setFocus();
    QTest::qWait(10);
    auto* bar = editor->findChild<QFrame*>(QStringLiteral("tableBar"));
    QVERIFY(bar && bar->isVisible());
    auto* rows = bar->findChild<QToolButton*>(QStringLiteral("tableRowsMenu"));
    QVERIFY(rows);
    QCOMPARE(rows->text(), QStringLiteral("Rows"));
    const auto original = typist::editor::serializeDocument(editor->document());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(10);
    QCOMPARE(rows->text(), QStringLiteral("行"));
    QCOMPARE(bar->findChild<QAction*>(QStringLiteral("tableBarCopyMarkdown"))->text(),
             QStringLiteral("复制为 Markdown"));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QVERIFY(!editor->document()->isUndoAvailable());
    QApplication::clipboard()->setText(QStringLiteral("**literal**\t10\n中文\t20\n"));
    actionBySource(window, QStringLiteral("Paste Markdown"))->trigger();
    QCOMPARE(table->rows(), 3);
    QCOMPARE(table->columns(), 2);
    const auto snapshot = typist::editor::serializeDocument(editor->document());
    QVERIFY(snapshot.contains("\\*\\*literal\\*\\*"));
    QVERIFY(QMetaObject::invokeMethod(&window, "save", Qt::DirectConnection));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), snapshot.toUtf8());
    file.close();
    window.findChild<QAction*>(QStringLiteral("table_copy_markdown"))->trigger();
    QVERIFY(QApplication::clipboard()->text().startsWith("| Name"));
    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(!editor->textCursor().currentTable());
    QVERIFY(!editor->paragraphSourceActive());
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(!bar->isVisible());
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), snapshot);
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QTest::qWait(5);
    QCOMPARE(rows->text(), QStringLiteral("Rows"));
  }
  void tableWorkflowVisuals() {
    const auto out = qEnvironmentVariable("TYPIST_TABLE_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_TABLE_OUTPUT to retain table writing specimens.");
    QVERIFY(QDir().mkpath(out));
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 850);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/table-writing.md"));
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QTextTable* table = nullptr;
    for (auto block = editor->document()->begin(); block.isValid() && !table; block = block.next())
      table = QTextCursor(block).currentTable();
    QVERIFY(table);
    editor->setTextCursor(table->cellAt(1, 1).firstCursorPosition());
    editor->setFocus();
    QTest::qWait(25);
    QVERIFY(window.grab().save(out + QStringLiteral("/table-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(15);
    QVERIFY(window.grab().save(out + QStringLiteral("/table-dark-zh.png")));
    auto* rows = editor->findChild<QToolButton*>(QStringLiteral("tableRowsMenu"));
    rows->menu()->popup(rows->mapToGlobal(QPoint(0, rows->height())));
    QTRY_VERIFY(rows->menu()->isVisible());
    QTest::qWait(40);
    QVERIFY(rows->menu()->grab().save(out + QStringLiteral("/table-row-menu-dark-zh.png")));
    QVERIFY(window.grab().save(out + QStringLiteral("/table-menu-dark-zh.png")));
    rows->menu()->hide();
    window.resize(800, 650);
    QTest::qWait(15);
    QVERIFY(window.grab().save(out + QStringLiteral("/table-compact-zh.png")));
    const auto markdown = typist::editor::serializeDocument(editor->document());
    QString error;
    QFile html(out + QStringLiteral("/table.html"));
    QVERIFY(html.open(QIODevice::WriteOnly));
    html.write(QByteArray::fromStdString(typist::core::wrapHtmlDocument(
        typist::render::html(markdown).toStdString(), "Table writing")));
    html.close();
    QVERIFY2(
        typist::platform::exportPdf(out + QStringLiteral("/table.pdf"), editor->document(), &error),
        qPrintable(error));
    QVERIFY2(typist::platform::exportDocx(out + QStringLiteral("/table.docx"), markdown, &error),
             qPrintable(error));
  }

 private:
  QTemporaryDir m_settings;
};

QTEST_MAIN(WindowTests)
#include "window_tests.moc"
