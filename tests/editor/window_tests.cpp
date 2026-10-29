#include "DocxWriter.h"
#include "Editor.h"
#include "EpubDialog.h"
#include "FindBar.h"
#include "HtmlImportDialog.h"
#include "HtmlImporter.h"
#include "LinkDialog.h"
#include "LinkNavigation.h"
#include "MainWindow.h"
#include "PdfExportDialog.h"
#include "PdfWriter.h"
#include "Settings.h"
#include "SpecialContent.h"
#include "TableEditing.h"
#include "Toc.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QStatusBar>
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

class LinkReceiver : public QObject {
  Q_OBJECT
 public:
  QUrl last;
 public slots:
  void capture(const QUrl& url) { last = url; }
};

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
  void linksNavigateFilesWithSaveProtectionAndHeadingJump() {
    QTemporaryDir directory;
    const auto original = directory.filePath("original.md");
    const auto target = directory.filePath(QStringLiteral("中文 note.md"));
    QFile file(original);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("# Original\n\nKeep this.\n");
    file.close();
    QFile next(target);
    QVERIFY(next.open(QIODevice::WriteOnly));
    next.write(QStringLiteral("# Next file\n\n## 中文 章节\n\nDestination.\n").toUtf8());
    next.close();
    typist::MainWindow window;
    window.openFile(original);
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(false);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(" unsaved");
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto undo = editor->document()->availableUndoSteps();
    const auto caret = editor->textCursor();
    bool cancelled = false;
    QTimer::singleShot(0, &window, [&] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(box);
      cancelled = true;
      box->button(QMessageBox::Cancel)->click();
    });
    window.followLink(QStringLiteral("中文%20note.md#中文-章节"));
    QVERIFY(cancelled);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), undo);
    QCOMPARE(editor->textCursor().position(), caret.position());
    QCOMPARE(editor->textCursor().anchor(), caret.anchor());
    QTimer::singleShot(0, &window, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(box);
      box->button(QMessageBox::Discard)->click();
    });
    window.followLink(QStringLiteral("中文%20note.md#中文-章节"));
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("中文 章节"));
    QVERIFY(editor->toPlainText().contains("Destination."));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("# Original\n\nKeep this.\n"));
    file.close();
    window.followLink("#missing");
    QVERIFY(window.statusBar()->currentMessage().contains("missing"));
    LinkReceiver receiver;
    QDesktopServices::setUrlHandler("https", &receiver, "capture");
    window.followLink("https://example.test/path");
    QCOMPARE(receiver.last, QUrl("https://example.test/path"));
    QDesktopServices::unsetUrlHandler("https");
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }
  void linkDialogLanguagePreservesRichTextAndUndo() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(typist::editor::loadMarkdown("[**Styled** and *italic*](https://old.test) tail\n",
                                         editor->document(), nullptr));
    editor->setTextCursor(editor->document()->find("Styled"));
    const auto before = typist::editor::serializeDocument(editor->document());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<typist::ui::LinkDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      QTimer::singleShot(1000, dialog, &QDialog::reject);
      QCOMPARE(dialog->windowTitle(), QStringLiteral("编辑链接"));
      QCOMPARE(dialog->findChild<QPushButton*>(QStringLiteral("cancelLink"))->text(),
               QStringLiteral("取消"));
      QCOMPARE(dialog->findChild<QLineEdit*>("linkLabel")->text(),
               QStringLiteral("Styled and italic"));
      dialog->findChild<QLineEdit*>("linkDestination")->setText("https://new.test/a b");
      dialog->accept();
    });
    window.findChild<QAction*>(QStringLiteral("format_link"))->trigger();
    const auto saved = typist::editor::serializeDocument(editor->document());
    QVERIFY2(saved.contains("https://new.test/a%20b"), qPrintable(saved));
    const auto parsed = typist::core::parseMarkdown(saved.toStdString());
    bool bold = false, italic = false;
    for (const auto& run : parsed.blocks.front().runs) {
      bold |= run.bold;
      italic |= run.italic;
    }
    QVERIFY(bold && italic);
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QTimer::singleShot(0, &window, [] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialog->reject();
    });
    window.findChild<QAction*>(QStringLiteral("format_link"))->trigger();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
  }
  void sourceViewLinkDialogKeepsMarkdownEditingAndUndo() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText("raw text");
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->setTextCursor(editor->document()->find("raw text"));
    const auto before = editor->toPlainText();
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<typist::ui::LinkDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      QTimer::singleShot(1000, dialog, &QDialog::reject);
      dialog->findChild<QLineEdit*>("linkLabel")->setText("Source **label**");
      dialog->findChild<QLineEdit*>("linkDestination")->setText("https://example.test/a b");
      dialog->accept();
    });
    window.findChild<QAction*>(QStringLiteral("format_link"))->trigger();
    QVERIFY(editor->toPlainText().contains("[Source **label**](https://example.test/a%20b)"));
    editor->undo();
    QCOMPARE(editor->toPlainText(), before);
    editor->redo();
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(editor->toPlainText().contains("Source label"));
    const auto linked = editor->document()->find("Source label");
    QCOMPARE(linked.charFormat().anchorHref(), QStringLiteral("https://example.test/a%20b"));
  }
  void linkWorkflowVisuals() {
    const auto output = QString::fromUtf8(qgetenv("TYPIST_LINK_VISUAL_DIR"));
    if (output.isEmpty()) QSKIP("Set TYPIST_LINK_VISUAL_DIR to capture link specimens");
    QVERIFY(QDir().mkpath(output));
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/link-writing.md"));
    window.resize(1180, 880);
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QTest::qWait(30);
    QVERIFY(window.grab().save(output + "/links-light.png"));
    const auto markdown = typist::editor::serializeDocument(editor->document());
    QFile html(output + "/links.html");
    QVERIFY(html.open(QIODevice::WriteOnly));
    html.write(typist::core::wrapHtmlDocument(typist::render::html(markdown).toStdString(), "Links")
                   .c_str());
    html.close();
    QString error;
    QVERIFY2(typist::platform::exportDocx(output + "/links.docx", markdown, &error),
             qPrintable(error));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(output + "/links-dark-zh.png"));
    editor->setTextCursor(editor->document()->find("linked note"));
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      QTimer::singleShot(1000, dialog, &QDialog::reject);
      auto* headings = dialog->findChild<QComboBox*>(QStringLiteral("linkHeading"));
      QVERIFY(headings);
      const auto sample = headings->grab().toImage();
      QVERIFY(sample.pixelColor(sample.width() / 2, sample.height() / 2).lightness() < 128);
      QVERIFY(dialog->grab().save(output + "/link-dialog-dark-zh.png"));
      dialog->reject();
    });
    window.findChild<QAction*>(QStringLiteral("format_link"))->trigger();
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

  void epubMetadataLanguageThemeAndCancel() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("dark"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    typist::ui::EpubDialog dialog(QStringLiteral("A book"), QStringLiteral("An author"),
                                  QStringLiteral("en"), &window);
    dialog.show();
    auto* title = dialog.findChild<QLineEdit*>(QStringLiteral("epubTitle"));
    auto* language = dialog.findChild<QLineEdit*>(QStringLiteral("epubLanguage"));
    auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("confirmEpub"));
    auto* cancel = dialog.findChild<QPushButton*>(QStringLiteral("cancelEpub"));
    QVERIFY(title && language && apply && cancel);
    QVERIFY(dialog.palette().color(QPalette::Window).lightness() < 100);
    QVERIFY(dialog.palette().color(QPalette::Text).lightness() > 160);
    title->clear();
    QVERIFY(!apply->isEnabled());
    title->setText(QStringLiteral("  我的书 & Ideas  "));
    language->setText(QStringLiteral("invalid language"));
    QVERIFY(!apply->isEnabled());
    language->setText(QStringLiteral("zh-CN"));
    QVERIFY(apply->isEnabled());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QTest::qWait(5);
    QCOMPARE(dialog.windowTitle(), QStringLiteral("导出 EPUB"));
    QCOMPARE(apply->text(), QStringLiteral("导出"));
    QCOMPARE(cancel->text(), QStringLiteral("取消"));
    QCOMPARE(dialog.bookTitle(), QStringLiteral("我的书 & Ideas"));
    QCOMPARE(dialog.author(), QStringLiteral("An author"));
    QCOMPARE(dialog.language(), QStringLiteral("zh-CN"));
    const auto output = qEnvironmentVariable("TYPIST_EPUB_OUTPUT");
    if (!output.isEmpty())
      QVERIFY(dialog.grab().save(output + QStringLiteral("/metadata-dark-zh.png")));
    cancel->click();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QTimer::singleShot(0, &window, [&] {
      auto* active = window.findChild<typist::ui::EpubDialog*>(QStringLiteral("epubDialog"));
      // The first dialog is hidden; the menu opens a new, visible metadata dialog.
      for (auto* child : window.findChildren<typist::ui::EpubDialog*>())
        if (child->isVisible()) active = child;
      if (active) active->reject();
    });
    auto* action = actionBySource(window, QStringLiteral("Export EPUB"));
    QVERIFY(action);
    action->trigger();
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    typist::ui::EpubDialog light(QStringLiteral("A book"), {}, QStringLiteral("en"), &window);
    light.show();
    QCOMPARE(light.windowTitle(), QStringLiteral("Export EPUB"));
    QVERIFY(light.palette().color(QPalette::Window).lightness() > 200);
    if (!output.isEmpty())
      QVERIFY(light.grab().save(output + QStringLiteral("/metadata-light-en.png")));
  }
  void epubExportUsesDraftAndSourceWithoutEditing() {
    QTemporaryDir directory;
    const auto inputPath = directory.filePath(QStringLiteral("draft.md"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write("Original **word**.\n\nTail.\n");
    input.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(inputPath);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Draft **snapshot**."));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto steps = editor->document()->availableUndoSteps();
    const auto path = directory.filePath(QStringLiteral("book.epub"));
    const typist::platform::EpubOptions options{QStringLiteral("A book"), {}, QStringLiteral("en")};
    QVERIFY(window.exportEpubForBench(path, options) > 400);
    QFile output(path);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("Draft <strong>snapshot</strong>"));
    output.close();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), steps);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Draft **snapshot**."));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->textCursor().insertText(QStringLiteral("Source "));
    const auto source = editor->toPlainText();
    const auto sourceSteps = editor->document()->availableUndoSteps();
    QVERIFY(window.exportEpubForBench(path, options) > 400);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("Source "));
    output.close();
    QCOMPARE(editor->toPlainText(), source);
    QCOMPARE(editor->document()->availableUndoSteps(), sourceSteps);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), QByteArray("Original **word**.\n\nTail.\n"));
  }

  void epubMenuExportsWithDefaultSuffixAndRemembersMetadata() {
    QTemporaryDir directory;
    QImage cover(100, 160, QImage::Format_RGB32);
    cover.fill(QColor("#315f99"));
    const auto coverPath = directory.filePath(QStringLiteral("cover.png"));
    QVERIFY(cover.save(coverPath));
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(typist::editor::loadMarkdown(QStringLiteral("# First\n\nOne.\n\n# Second\n\nTwo."),
                                         editor->document(), nullptr));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    bool metadataAccepted = false, destinationAccepted = false;
    QTimer automation;
    automation.setInterval(10);
    connect(&automation, &QTimer::timeout, &window, [&] {
      if (auto* dialog = qobject_cast<typist::ui::EpubDialog*>(QApplication::activeModalWidget())) {
        dialog->findChild<QLineEdit*>(QStringLiteral("epubTitle"))
            ->setText(QStringLiteral("Menu book"));
        dialog->findChild<QLineEdit*>(QStringLiteral("epubAuthor"))
            ->setText(QStringLiteral("Writer"));
        dialog->findChild<QLineEdit*>(QStringLiteral("epubLanguage"))
            ->setText(QStringLiteral("en"));
        dialog->findChild<QLineEdit*>(QStringLiteral("epubCover"))->setText(coverPath);
        dialog->findChild<QCheckBox*>(QStringLiteral("epubSplitChapters"))->setChecked(true);
        metadataAccepted = true;
        dialog->accept();
      } else if (auto* destination =
                     qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        auto* name = destination->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        if (!name) return;
        name->setText(directory.filePath(QStringLiteral("menu-book")));
        destinationAccepted = true;
        QMetaObject::invokeMethod(destination, "accept", Qt::DirectConnection);
      }
    });
    // Ensure a failed automation closes any modal dialog rather than hanging CI.
    QTimer safety;
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->reject();
    });
    safety.start(3000);
    automation.start();
    const bool invoked = QMetaObject::invokeMethod(&window, "exportEpub", Qt::DirectConnection);
    automation.stop();
    safety.stop();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    QVERIFY(invoked && metadataAccepted && destinationAccepted);
    QFile file(directory.filePath(QStringLiteral("menu-book.epub")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(bytes.contains("<dc:title>Menu book</dc:title>"));
    QVERIFY(bytes.contains("<dc:creator>Writer</dc:creator>"));
    QVERIFY(bytes.contains("OEBPS/cover.xhtml"));
    QVERIFY(bytes.contains("OEBPS/chapter-2.xhtml"));
    QCOMPARE(QSettings().value(QStringLiteral("export/epubAuthor")).toString(),
             QStringLiteral("Writer"));
    QCOMPARE(QSettings().value(QStringLiteral("export/epubLanguage")).toString(),
             QStringLiteral("en"));
    QVERIFY(QSettings().value(QStringLiteral("export/epubSplitChapters")).toBool());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    bool remembered = false;
    QTimer::singleShot(0, &window, [&] {
      if (auto* dialog = qobject_cast<typist::ui::EpubDialog*>(QApplication::activeModalWidget())) {
        remembered = dialog->splitChapters() && dialog->coverImage().isEmpty();
        dialog->reject();
      }
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "exportEpub", Qt::DirectConnection));
    QVERIFY(remembered);
  }
  void epubCoverBrowseValidationAndBilingualPreview() {
    QTemporaryDir directory;
    const auto coverPath = directory.filePath(QStringLiteral("封面.png"));
    QImage cover(600, 900, QImage::Format_RGB32);
    cover.fill(QColor("#315f99"));
    {
      QPainter painter(&cover);
      painter.setPen(Qt::white);
      QFont font = painter.font();
      font.setPointSize(32);
      painter.setFont(font);
      painter.drawText(cover.rect().adjusted(30, 30, -30, -30), Qt::AlignCenter,
                       QStringLiteral("Writing\nChapters"));
    }
    QVERIFY(cover.save(coverPath));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    const auto output = qEnvironmentVariable("TYPIST_CHAPTERS_OUTPUT");
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + language)->trigger();
      window
          .findChild<QAction*>(language == QLatin1String("en") ? QStringLiteral("appearance_light")
                                                               : QStringLiteral("appearance_dark"))
          ->trigger();
      typist::ui::EpubDialog dialog(QStringLiteral("Writing / 章节"), QStringLiteral("Author"),
                                    QStringLiteral("zh-CN"), &window);
      dialog.show();
      auto* input = dialog.findChild<QLineEdit*>(QStringLiteral("epubCover"));
      auto* confirm = dialog.findChild<QPushButton*>(QStringLiteral("confirmEpub"));
      input->setText(directory.filePath(QStringLiteral("missing.png")));
      QVERIFY(!confirm->isEnabled());
      input->setText(coverPath);
      QVERIFY(confirm->isEnabled());
      dialog.setSplitChapters(true);
      QVERIFY(dialog.splitChapters());
      QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("epubCoverPreview"))->isVisible());
      QCOMPARE(dialog.findChild<QCheckBox*>(QStringLiteral("epubSplitChapters"))->text(),
               language == QLatin1String("en")
                   ? QStringLiteral("Split chapters at level-one headings")
                   : QStringLiteral("按一级标题拆分章节"));
      if (!output.isEmpty())
        QVERIFY(dialog.grab().save(output + QStringLiteral("/cover-dialog-") + language +
                                   QStringLiteral(".png")));
      dialog.findChild<QPushButton*>(QStringLiteral("clearEpubCover"))->click();
      QVERIFY(input->text().isEmpty());
      QVERIFY(confirm->isEnabled());
      const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
      QTimer automation, safety;
      automation.setInterval(10);
      connect(&automation, &QTimer::timeout, &dialog, [&] {
        if (auto* selector = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))
          if (auto* name = selector->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
            name->setText(coverPath);
            QMetaObject::invokeMethod(selector, "accept", Qt::DirectConnection);
          }
      });
      connect(&safety, &QTimer::timeout, &dialog, [] {
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
          modal->reject();
      });
      automation.start();
      safety.start(3000);
      dialog.findChild<QPushButton*>(QStringLiteral("browseEpubCover"))->click();
      automation.stop();
      safety.stop();
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
      QCOMPARE(dialog.coverImage(), coverPath);
      QVERIFY(confirm->isEnabled());
      dialog.findChild<QPushButton*>(QStringLiteral("cancelEpub"))->click();
    }
  }

  void htmlImportNewBufferSaveAndPreview() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("source.html"));
    QFile source(path);
    const QByteArray bytes =
        "<html><meta charset='utf-8'><h1>Imported title</h1><p>A <b>bold</b> "
        "paragraph.</p><pre>one\n  "
        "two</pre><table><tr><th>Name</th></tr><tr><td>Value</td></tr></table></html>";
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(bytes);
    source.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    QVERIFY(window.importHtmlFile(path));
    QVERIFY(window.windowTitle().contains('*'));
    QVERIFY(!window.windowTitle().contains(QStringLiteral("source.html")));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    const auto markdown = typist::editor::serializeDocument(editor->document());
    QVERIFY(markdown.contains(QStringLiteral("# Imported title")));
    QVERIFY(markdown.contains(QStringLiteral("**bold**")));
    QVERIFY(!editor->document()->isUndoAvailable());
    const auto parsed = typist::core::parseMarkdown(markdown.toStdString());
    const auto target = directory.filePath(QStringLiteral("saved.md"));
    QVERIFY(window.saveDocumentAs(target));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), bytes);
    source.close();
    QFile saved(target);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(typist::core::astEqual(
        parsed.blocks, typist::core::parseMarkdown(saved.readAll().toStdString()).blocks));
    QVERIFY(window.exportEpubForBench(directory.filePath(QStringLiteral("imported.epub")),
                                      {QStringLiteral("Imported"), {}, QStringLiteral("en")}) >
            400);
    QVERIFY(window.exportDocxForBench(directory.filePath(QStringLiteral("imported.docx"))) > 400);
    const auto output = qEnvironmentVariable("TYPIST_HTML_OUTPUT");
    for (const auto& preference : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + preference)->trigger();
      window
          .findChild<QAction*>(preference == QStringLiteral("en")
                                   ? QStringLiteral("appearance_light")
                                   : QStringLiteral("appearance_dark"))
          ->trigger();
      const auto result = typist::editor::importHtmlFile(path);
      typist::ui::HtmlImportDialog preview(result, &window);
      preview.show();
      QTest::qWait(10);
      QVERIFY(preview.findChild<QTextEdit*>(QStringLiteral("htmlImportPreview"))->isReadOnly());
      QCOMPARE(preview.windowTitle(), preference == QStringLiteral("en")
                                          ? QStringLiteral("Import HTML")
                                          : QStringLiteral("导入 HTML"));
      QCOMPARE(
          preview.findChild<QPushButton*>(QStringLiteral("confirmHtmlImport"))->text(),
          preference == QStringLiteral("en") ? QStringLiteral("Import") : QStringLiteral("导入"));
      if (!output.isEmpty())
        QVERIFY(preview.grab().save(output + QStringLiteral("/preview-") + preference +
                                    QStringLiteral(".png")));
      preview.findChild<QPushButton*>(QStringLiteral("cancelHtmlImport"))->click();
      QCOMPARE(preview.result(), int(QDialog::Rejected));
      QCOMPARE(typist::editor::serializeDocument(editor->document()), markdown);
    }
  }
  void htmlImportCancellationAndErrorsKeepDraft() {
    QTemporaryDir directory;
    QFile source(directory.filePath(QStringLiteral("source.html")));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("<h1>Replacement</h1>");
    source.close();
    QSettings().setValue(QStringLiteral("editor/autoSave"), false);
    typist::MainWindow window;
    window.show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Original **draft**."));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->setPlainText(QStringLiteral("Pending **snapshot**."));
    bool cancelClicked = false;
    QTimer::singleShot(0, &window, [&] {
      if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        cancelClicked = true;
        message->button(QMessageBox::Cancel)->click();
      }
    });
    QVERIFY(!window.importHtmlFile(source.fileName()));
    QVERIFY(cancelClicked);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Pending **snapshot**."));
    QVERIFY(!window.importHtmlFile(directory.filePath(QStringLiteral("missing.html"))));
    QCOMPARE(draft->toPlainText(), QStringLiteral("Pending **snapshot**."));
    window.setBenchMode(true);
  }
  void htmlClipboardPasteUndoAndLiteralContexts() {
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Before TAIL"));
    auto cursor = editor->textCursor();
    cursor.setPosition(7);
    editor->setTextCursor(cursor);
    const auto before = typist::editor::serializeDocument(editor->document());
    auto* mime = new QMimeData;
    mime->setHtml(QStringLiteral("<h2>Pasted</h2><p><b>Bold</b> and <i>italic</i>.</p>"));
    mime->setText(QStringLiteral("Pasted\nBold and italic."));
    QApplication::clipboard()->setMimeData(mime);
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    const auto pasted = typist::editor::serializeDocument(editor->document());
    QVERIFY(pasted.contains(QStringLiteral("## Pasted")));
    QVERIFY(pasted.contains(QStringLiteral("**Bold**")));
    QVERIFY(pasted.contains(QStringLiteral("TAIL")));
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    editor->redo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), pasted);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);
    const auto sourceBefore = editor->toPlainText();
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    QVERIFY(editor->toPlainText().mid(sourceBefore.size()).contains(QStringLiteral("## Pasted")));
    editor->undo();
    QCOMPARE(editor->toPlainText(), sourceBefore);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    cursor = editor->textCursor();
    cursor.setPosition(0);
    editor->setTextCursor(cursor);
    editor->formatParagraph(typist::editor::ParagraphStyle::Code);
    const auto codeBefore = typist::editor::serializeDocument(editor->document());
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    QVERIFY(!editor->textCursor().blockFormat().headingLevel());
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains(QStringLiteral("Pasted\nBold and italic.")));
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), codeBefore);
    QApplication::clipboard()->setText(QStringLiteral("No HTML"));
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), codeBefore);
  }
  void htmlPasteIntoParagraphDraftIsLiteral() {
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Original paragraph"));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->selectAll();
    auto* mime = new QMimeData;
    mime->setHtml(QStringLiteral("<script>Nothing readable</script>"));
    mime->setText(QStringLiteral("Literal <tag> & **Markdown**"));
    QApplication::clipboard()->setMimeData(mime);
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    QCOMPARE(draft->toPlainText(), QStringLiteral("Literal <tag> & **Markdown**"));
    QVERIFY(QMetaObject::invokeMethod(&window, "undo", Qt::DirectConnection));
    QCOMPARE(draft->toPlainText(), QStringLiteral("Original paragraph"));
  }
  void htmlImportMenuPreviewCancelAndAccept() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("menu.html"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<h1>Menu import</h1><p>A paragraph.</p>");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Keep this buffer"));
    const auto before = typist::editor::serializeDocument(editor->document());
    const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    bool selected = false, reviewed = false, accepting = false;
    QTimer automation, safety;
    automation.setInterval(10);
    connect(&automation, &QTimer::timeout, &window, [&] {
      if (auto* selector = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        // selectFile() deliberately leaves a focused filename field unchanged on
        // some Qt versions. Enter the absolute path in that field like a user would.
        auto* name = selector->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        if (!name) return;
        name->setText(path);
        selected = true;
        QMetaObject::invokeMethod(selector, "accept", Qt::DirectConnection);
      } else if (auto* preview = qobject_cast<typist::ui::HtmlImportDialog*>(
                     QApplication::activeModalWidget())) {
        reviewed = true;
        if (accepting)
          preview->accept();
        else
          preview->reject();
      }
    });
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->reject();
    });
    automation.start();
    safety.start(3000);
    auto* action = window.findChild<QAction*>(QStringLiteral("importHtml"));
    const bool actionFound = action;
    if (action) action->trigger();
    const bool cancelledUnchanged = typist::editor::serializeDocument(editor->document()) == before;
    const bool cancelledReviewed = selected && reviewed;
    selected = reviewed = false;
    accepting = true;
    if (action) action->trigger();
    automation.stop();
    safety.stop();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    QVERIFY(actionFound && cancelledReviewed && selected && reviewed && cancelledUnchanged);
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains(QStringLiteral("# Menu import")));
  }

  void pdfSetupPreviewThemeLanguageAndCancel() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    QTextDocument document;
    QString source = QStringLiteral("# Export preview\n\n");
    for (int i = 0; i < 100; ++i)
      source += QStringLiteral("Paragraph %1 / 一段可阅读的中文与英文。\n\n").arg(i + 1);
    QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
    const auto before = typist::editor::serializeDocument(&document);
    typist::platform::PdfPageOptions initial;
    initial.title = QStringLiteral("Document");
    initial.header = QStringLiteral("{title}");
    initial.footer = QStringLiteral("Page #");
    const auto output = qEnvironmentVariable("TYPIST_PDF_OUTPUT");
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + language)->trigger();
      window
          .findChild<QAction*>(language == QLatin1String("en") ? QStringLiteral("appearance_light")
                                                               : QStringLiteral("appearance_dark"))
          ->trigger();
      typist::ui::PdfExportDialog dialog(&document, initial, &window);
      dialog.show();
      auto* exportButton = dialog.findChild<QPushButton*>(QStringLiteral("confirmPdfExport"));
      QTRY_VERIFY(exportButton->isEnabled());
      auto* next = dialog.findChild<QPushButton*>(QStringLiteral("pdfNextPage"));
      auto* previous = dialog.findChild<QPushButton*>(QStringLiteral("pdfPreviousPage"));
      QVERIFY(next->isEnabled());
      QVERIFY(!previous->isEnabled());
      next->click();
      QVERIFY(previous->isEnabled());
      previous->click();
      auto* orientation = dialog.findChild<QComboBox*>(QStringLiteral("pdfOrientation"));
      orientation->setCurrentIndex(1);
      dialog.findChild<QComboBox*>(QStringLiteral("pdfPaper"))->setCurrentIndex(1);
      dialog.findChild<QDoubleSpinBox*>(QStringLiteral("pdfMargin"))->setValue(12.5);
      dialog.findChild<QLineEdit*>(QStringLiteral("pdfHeader"))
          ->setText(QStringLiteral("Custom #"));
      QTRY_VERIFY(exportButton->isEnabled());
      const auto options = dialog.options();
      QVERIFY(options.landscape);
      QCOMPARE(options.paper, QStringLiteral("letter"));
      QCOMPARE(options.marginMm, 12.5);
      QCOMPARE(options.header, QStringLiteral("Custom #"));
      QCOMPARE(dialog.windowTitle(), language == QLatin1String("en") ? QStringLiteral("Export PDF")
                                                                     : QStringLiteral("导出 PDF"));
      QCOMPARE(exportButton->text(),
               language == QLatin1String("en") ? QStringLiteral("Export") : QStringLiteral("导出"));
      if (!output.isEmpty())
        QVERIFY(dialog.grab().save(output + QStringLiteral("/setup-") + language +
                                   QStringLiteral(".png")));
      dialog.findChild<QPushButton*>(QStringLiteral("cancelPdfExport"))->click();
      QCOMPARE(dialog.result(), int(QDialog::Rejected));
      QCOMPARE(typist::editor::serializeDocument(&document), before);
    }
  }
  void pdfMenuPersistsOnlyAfterSuccessfulSave() {
    QTemporaryDir directory;
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("A document to export."));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto old = typist::platform::pdfOptions(QStringLiteral("Old"));
    QTimer::singleShot(0, &window, [&] {
      if (auto* dialog =
              qobject_cast<typist::ui::PdfExportDialog*>(QApplication::activeModalWidget())) {
        dialog->findChild<QComboBox*>(QStringLiteral("pdfPaper"))->setCurrentIndex(1);
        dialog->reject();
      }
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "exportPdf", Qt::DirectConnection));
    QCOMPARE(typist::platform::pdfOptions(QStringLiteral("Old")).paper, old.paper);
    const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    bool configured = false, setupAccepted = false, fileAccepted = false, cancelDestination = true;
    QTimer automation, safety;
    automation.setInterval(10);
    connect(&automation, &QTimer::timeout, &window, [&] {
      if (auto* dialog =
              qobject_cast<typist::ui::PdfExportDialog*>(QApplication::activeModalWidget())) {
        if (!configured) {
          dialog->findChild<QComboBox*>(QStringLiteral("pdfPaper"))->setCurrentIndex(1);
          dialog->findChild<QComboBox*>(QStringLiteral("pdfOrientation"))->setCurrentIndex(1);
          dialog->findChild<QDoubleSpinBox*>(QStringLiteral("pdfMargin"))->setValue(12.5);
          dialog->findChild<QLineEdit*>(QStringLiteral("pdfHeader"))
              ->setText(QStringLiteral("Header #"));
          dialog->findChild<QCheckBox*>(QStringLiteral("pdfRepeatHeader"))->setChecked(false);
          configured = true;
        }
        auto* button = dialog->findChild<QPushButton*>(QStringLiteral("confirmPdfExport"));
        if (button->isEnabled()) {
          setupAccepted = true;
          button->click();
        }
      } else if (auto* selector = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        if (cancelDestination) {
          fileAccepted = true;
          selector->reject();
          return;
        }
        if (auto* name = selector->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
          name->setText(directory.filePath(QStringLiteral("menu-pdf")));
          fileAccepted = true;
          QMetaObject::invokeMethod(selector, "accept", Qt::DirectConnection);
        }
      }
    });
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->reject();
    });
    automation.start();
    safety.start(3000);
    const bool cancelled = QMetaObject::invokeMethod(&window, "exportPdf", Qt::DirectConnection) &&
                           setupAccepted && fileAccepted;
    const auto afterCancel = typist::platform::pdfOptions(QStringLiteral("Old"));
    cancelDestination = false;
    configured = setupAccepted = fileAccepted = false;
    const bool invoked = QMetaObject::invokeMethod(&window, "exportPdf", Qt::DirectConnection);
    automation.stop();
    safety.stop();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    QVERIFY(invoked && setupAccepted && fileAccepted);
    QVERIFY(cancelled);
    QCOMPARE(afterCancel.header, old.header);
    QCOMPARE(afterCancel.paper, old.paper);
    QFile pdf(directory.filePath(QStringLiteral("menu-pdf.pdf")));
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.readAll().startsWith("%PDF-"));
    const auto saved = typist::platform::pdfOptions(QStringLiteral("New"));
    QVERIFY(saved.landscape);
    QCOMPARE(saved.paper, QStringLiteral("letter"));
    QCOMPARE(saved.marginMm, 12.5);
    QCOMPARE(saved.header, QStringLiteral("Header #"));
    QVERIFY(!saved.repeatTableHeader);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    typist::platform::setPdfOptions(old);
  }
  void pdfUsesParagraphSnapshotWithoutApplying() {
    QTemporaryDir directory;
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Original paragraph"));
    const auto before = typist::editor::serializeDocument(editor->document());
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->setPlainText(QStringLiteral("# Draft export snapshot"));
    const auto undo = editor->document()->availableUndoSteps();
    QVERIFY(window.exportPdfForBench(directory.filePath(QStringLiteral("draft.pdf")), {}) > 400);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), undo);
    QCOMPARE(draft->toPlainText(), QStringLiteral("# Draft export snapshot"));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->insertPlainText(QStringLiteral("Source "));
    const auto source = editor->toPlainText();
    QVERIFY(window.exportPdfForBench(directory.filePath(QStringLiteral("source.pdf")), {}) > 400);
    QCOMPARE(editor->toPlainText(), source);
  }

 private:
  QTemporaryDir m_settings;
};

QTEST_MAIN(WindowTests)
#include "window_tests.moc"
