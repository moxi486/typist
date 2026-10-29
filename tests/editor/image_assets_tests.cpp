#include "DocxWriter.h"
#include "Editor.h"
#include "ImageAssets.h"
#include "ImageResources.h"
#include "MainWindow.h"
#include "PdfWriter.h"
#include "Settings.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QClipboard>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextFragment>
#include <QtTest>

using namespace typist;
namespace {
QImage specimen(QColor color = QColor(42, 113, 180, 100)) {
  QImage image(64, 32, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}
QByteArray read(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return file.readAll();
}
bool write(const QString& path, const QByteArray& data) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QList<QTextCharFormat> images(QTextDocument* document) {
  QList<QTextCharFormat> result;
  for (auto block = document->begin(); block.isValid(); block = block.next())
    for (auto it = block.begin(); !it.atEnd(); ++it)
      if (it.fragment().charFormat().hasProperty(editor::kImageProperty))
        result.append(it.fragment().charFormat());
  return result;
}
ui::Editor* prepare(MainWindow& window, const QString& path = {}) {
  window.setBenchMode(true);
  if (!path.isEmpty()) window.openFile(path);
  window.resize(1000, 750);
  window.show();
  window.activateWindow();
  auto* editor = window.findChild<ui::Editor*>(QStringLiteral("writingEditor"));
  editor->setFocus();
  QTest::qWait(10);
  return editor;
}
void paste(ui::Editor* editor, const QImage& image) {
  QApplication::clipboard()->setImage(image);
  editor->paste();
}
}  // namespace
class ImageAssetsTests : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    QApplication::setOrganizationName(QStringLiteral("TypistImageTests"));
    QApplication::setApplicationName(QStringLiteral("TypistImageTests"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QStandardPaths::setTestModeEnabled(true);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    platform::setCopyImagesToAssets(true);
  }
  void init() {
    platform::setLanguagePreference(QStringLiteral("en"));
    platform::setThemePreference(QStringLiteral("light"));
  }
  void storesAlphaAndDeduplicatesWithoutOverwriting() {
    QTemporaryDir temp;
    const auto doc = temp.filePath(QStringLiteral("报告 (final) #1.md"));
    const auto image = specimen();
    const auto stored = platform::storeImage(image, doc);
    QVERIFY2(stored.ok(), qPrintable(stored.error));
    QVERIFY(stored.target.startsWith(
        QStringLiteral("%E6%8A%A5%E5%91%8A%20%28final%29%20%231.assets/")));
    QCOMPARE(QImage(stored.path).pixelColor(0, 0), image.pixelColor(0, 0));
    const auto repeated = platform::storeImage(image, doc);
    QCOMPARE(repeated.path, stored.path);
    QCOMPARE(QDir(QFileInfo(stored.path).absolutePath()).entryList(QDir::Files).size(), 1);
    QVERIFY(write(stored.path, "existing unrelated content"));
    QVERIFY(!platform::storeImage(image, doc).ok());
    QCOMPARE(read(stored.path), QByteArray("existing unrelated content"));
  }
  void importsOriginalBytesAndSupportsLinkPreference() {
    QTemporaryDir temp;
    const auto path = temp.filePath(QStringLiteral("原始 [a] (1).png"));
    QVERIFY(specimen().save(path));
    const auto doc = temp.filePath("note.md");
    const auto copied = platform::importImage(path, doc, true);
    QVERIFY2(copied.ok(), qPrintable(copied.error));
    QCOMPARE(read(copied.path), read(path));
    const auto linked = platform::importImage(path, doc, false);
    QCOMPARE(linked.path, path);
    QVERIFY(linked.target.contains("%20"));
    QCOMPARE(render::localImagePath(linked.target, QUrl::fromLocalFile(temp.path() + '/')), path);
    const auto markdown = platform::imageMarkdown(copied.target, QStringLiteral("a ] [ \\ b"));
    const auto parsed = core::parseMarkdown(markdown.toStdString());
    QVERIFY(parsed.ok && parsed.blocks.size() == 1 && parsed.blocks.front().runs.front().image);
    QCOMPARE(QString::fromStdString(parsed.blocks.front().runs.front().text),
             QStringLiteral("a ] [ \\ b"));
    QVERIFY(QFile::remove(path));
    QVERIFY(!render::imagePreview(copied.path).isNull());
    QVERIFY(!platform::importImage(path, doc, true).ok());
  }
  void findsOnlyRealImageDestinationsAndPreservesTitlesAndUnicode() {
    const QString source = QStringLiteral(
        "前缀 ![照片](a%20b.png \"caption\") 后缀\n\n"
        "`![literal](fake.png)`\n\n```md\n![fence](fake.png)\n```\n\n"
        "| Image |\n|---|\n| ![表](<a(b).png>) |\n\n![ref][r]\n\n[r]: reference.png\n");
    const auto utf8 = source.toUtf8();
    const auto found =
        core::imageSources(std::string_view(utf8.constData(), std::size_t(utf8.size())));
    QCOMPARE(found.size(), std::size_t(3));
    for (const auto& image : found) {
      if (image.target == "reference.png") {
        QCOMPARE(image.start, std::string::npos);
        continue;
      }
      QCOMPARE(utf8.mid(int(image.start), int(image.length)),
               QByteArray::fromStdString(image.target));
    }
  }
  void saveAsMovesRelativeAssetsAndLeavesCodeUntouched() {
    QTemporaryDir temp;
    const auto old = temp.filePath("first.md");
    const auto stored = platform::storeImage(specimen(), old);
    QVERIFY(stored.ok());
    const QString link = platform::imageMarkdown(stored.target, "photo");
    const QString source = QStringLiteral("中文 ") + link + QStringLiteral("\n\n```md\n") + link +
                           QStringLiteral("\n```\n");
    QVERIFY(QDir().mkpath(temp.filePath("elsewhere")));
    const auto moved = platform::relocateImages(source, old, temp.filePath("elsewhere/renamed.md"));
    QVERIFY2(moved.error.isEmpty(), qPrintable(moved.error));
    QCOMPARE(moved.targets.size(), 1);
    QVERIFY(moved.markdown.contains("```md\n" + link + "\n```"));
    QVERIFY(moved.markdown.startsWith(QStringLiteral("中文 ![photo](renamed.assets/")));
    const auto newPath = render::localImagePath(moved.targets.first(),
                                                QUrl::fromLocalFile(temp.filePath("elsewhere/")));
    QCOMPARE(read(newPath), read(stored.path));
    QVERIFY(QFileInfo(stored.path).isFile());
    const auto reference = platform::relocateImages(
        QStringLiteral("![p][r]\n\n[r]: ") + stored.target, old, temp.filePath("elsewhere/ref.md"));
    QVERIFY(!reference.error.isEmpty());
  }
  void multipleInlineImagesKeepTheirOwnTargetsAndNeighbors() {
    QTemporaryDir temp;
    const auto red = platform::storeImage(specimen(Qt::red), temp.filePath("note.md"));
    const auto blue = platform::storeImage(specimen(Qt::blue), temp.filePath("note.md"));
    const auto source = QStringLiteral("Before ") + platform::imageMarkdown(red.target, "red") +
                        ' ' + platform::imageMarkdown(blue.target, "blue") +
                        QStringLiteral(" after.");
    QTextDocument document;
    document.setBaseUrl(QUrl::fromLocalFile(temp.path() + '/'));
    QVERIFY(editor::loadMarkdown(source, &document, nullptr));
    const auto runs = images(&document);
    QCOMPARE(runs.size(), 2);
    QCOMPARE(runs[0].property(editor::kImageProperty).toString(), red.target);
    QCOMPARE(runs[1].property(editor::kImageProperty).toString(), blue.target);
    QVERIFY(document.toPlainText().startsWith("Before ") &&
            document.toPlainText().endsWith(" after."));
    const auto before = core::parseMarkdown(source.toStdString());
    const auto after = core::parseMarkdown(editor::serializeDocument(&document).toStdString());
    QVERIFY(core::astEqual(before.blocks, after.blocks));
  }
  void clipboardPasteIsOneUndoAndAssetsSurviveUndo() {
    QTemporaryDir temp;
    const auto path = temp.filePath("note.md");
    QVERIFY(write(path, "Before TAIL\n"));
    MainWindow window;
    auto* editor = prepare(window, path);
    const auto original = editor::serializeDocument(editor->document());
    QTextCursor cursor(editor->document());
    cursor.setPosition(7);
    editor->setTextCursor(cursor);
    paste(editor, specimen());
    const auto runs = images(editor->document());
    QCOMPARE(runs.size(), 1);
    QVERIFY(runs.first().isImageFormat());
    const auto asset = render::localImagePath(
        runs.first().property(editor::kImageProperty).toString(), editor->document()->baseUrl());
    QVERIFY(QFileInfo(asset).isFile());
    QVERIFY(editor->toPlainText().startsWith("Before ") && editor->toPlainText().endsWith("TAIL"));
    editor->undo();
    QCOMPARE(editor::serializeDocument(editor->document()), original);
    QVERIFY(QFileInfo(asset).isFile());
    editor->redo();
    QCOMPARE(images(editor->document()).size(), 1);
    QTest::keyClicks(editor, " and text");
    QCOMPARE(images(editor->document()).size(), 1);
    QVERIFY(editor::serializeDocument(editor->document()).contains(" and textTAIL"));
    QVERIFY(window.saveDocumentAs(temp.filePath("copy.md")));
    QVERIFY(read(temp.filePath("copy.md")).contains("copy.assets/"));
  }
  void unnamedSourceAndParagraphPasteMigrateOnSave() {
    QTemporaryDir temp;
    MainWindow window;
    auto* editor = prepare(window);
    paste(editor, specimen(QColor("#42a872")));
    auto target = images(editor->document()).first().property(editor::kImageProperty).toString();
    QVERIFY(QUrl(target).isLocalFile());
    const QString original = render::localImagePath(target);
    QVERIFY(window.saveDocumentAs(temp.filePath(QStringLiteral("已保存.md"))));
    QVERIFY(!read(temp.filePath(QStringLiteral("已保存.md"))).contains("file:///"));
    QVERIFY(read(temp.filePath(QStringLiteral("已保存.md"))).contains(".assets/"));
    QVERIFY(QFileInfo(original).isFile());
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral("\n\n"));
    paste(editor, specimen(Qt::yellow));
    QVERIFY(editor->toPlainText().contains("!["));
    QVERIFY(window.saveDocumentAs(temp.filePath("source.md")));
    QVERIFY(!read(temp.filePath("source.md")).contains("%E5%B7%B2%E4%BF%9D%E5%AD%98.assets"));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QCOMPARE(images(editor->document()).size(), 2);
    // A source panel keeps image paste as Markdown and includes it in draft snapshots.
    const auto body = temp.filePath("body.md");
    QVERIFY(write(body, "Paragraph.\n"));
    window.openFile(body);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* input = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QApplication::clipboard()->setImage(specimen(Qt::magenta));
    QTest::keyClick(input, Qt::Key_V, Qt::ControlModifier);
    QVERIFY(input->toPlainText().contains("body.assets/"));
    QVERIFY(editor->markdownWithParagraphDraft().contains("body.assets/"));
    editor->finishParagraphSourceEdit();
    QCOMPARE(images(editor->document()).size(), 1);
  }
  void codeAndImportFailuresDoNotChangeTheDocument() {
    QTemporaryDir temp;
    const auto path = temp.filePath("note.md");
    QVERIFY(write(path, "```cpp\nint a;\n```\n"));
    MainWindow window;
    auto* editor = prepare(window, path);
    const auto original = editor::serializeDocument(editor->document());
    paste(editor, specimen());
    QCOMPARE(editor::serializeDocument(editor->document()), original);
    QVERIFY(!QFileInfo(temp.filePath("note.assets")).exists());
    const auto body = temp.filePath("body.md");
    QVERIFY(write(body, "Original.\n"));
    QVERIFY(write(temp.filePath("body.assets"), "do not replace this file"));
    window.openFile(body);
    const auto before = editor::serializeDocument(editor->document());
    paste(editor, specimen());
    QCOMPARE(editor::serializeDocument(editor->document()), before);
    QVERIFY(window.statusBar()->currentMessage().contains("folder"));
    QCOMPARE(read(temp.filePath("body.assets")), QByteArray("do not replace this file"));
  }
  void fileUrlsAndDragDropInsertAtCaretAndCopyOnly() {
    QTemporaryDir temp;
    const auto file = temp.filePath(QStringLiteral("原始 [1].png"));
    QVERIFY(specimen(Qt::cyan).save(file));
    const auto path = temp.filePath("drop.md");
    QVERIFY(write(path, "First\n\nTAIL\n"));
    MainWindow window;
    auto* editor = prepare(window, path);
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(file)});
    const auto target = editor->cursorRect(QTextCursor(editor->document()->lastBlock())).center();
    QDragEnterEvent enter(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor->viewport(), &enter);
    QVERIFY(enter.isAccepted());
    QDropEvent drop(target, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(editor->viewport(), &drop);
    QVERIFY(drop.isAccepted());
    const auto markdown = editor::serializeDocument(editor->document());
    QCOMPARE(images(editor->document()).size(), 1);
    QVERIFY(markdown.indexOf("![") > markdown.indexOf("First"));
    QVERIFY(markdown.indexOf("![") < markdown.indexOf("TAIL"));
    QVERIFY(QFileInfo(file).isFile());
    QVERIFY(QFileInfo(temp.filePath("drop.assets")).isDir());
  }
  void htmlDocxAndPreviewResolveEncodedRelativeAndFileUrls() {
    QTemporaryDir temp;
    QImage large(2000, 1000, QImage::Format_RGB32);
    large.fill(Qt::blue);
    const auto path = temp.filePath(QStringLiteral("大 图 #1.png"));
    QVERIFY(large.save(path));
    const auto preview = render::imagePreview(path);
    QCOMPARE(preview.size(), QSize(640, 320));
    const auto imported = platform::importImage(path, temp.filePath("note.md"), false);
    const QString source = platform::imageMarkdown(imported.target, "photo");
    const auto base = QUrl::fromLocalFile(temp.path() + '/');
    const auto html =
        render::embedLocalImages(QString::fromStdString(core::toHtml(source.toStdString())) +
                                     "<img src=\"https://example.test/image.png\" />",
                                 base);
    QVERIFY(html.contains("data:image/png;base64,"));
    QVERIFY(html.contains("https://example.test/image.png"));
    QString error;
    QVERIFY2(platform::exportDocx(temp.filePath("image.docx"), source, &error, temp.path()),
             qPrintable(error));
    QVERIFY(read(temp.filePath("image.docx")).contains("word/media/image1.png"));
    QTextDocument document;
    document.setBaseUrl(base);
    const auto absolute =
        platform::imageMarkdown(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded), "file");
    QVERIFY(editor::loadMarkdown(absolute, &document, &error));
    QCOMPARE(images(&document).size(), 1);
    QVERIFY(images(&document).first().isImageFormat());
  }
  void imageVisualSpecimens() {
    const QString out = qEnvironmentVariable("TYPIST_IMAGE_OUTPUT");
    if (out.isEmpty())
      QSKIP("Set TYPIST_IMAGE_OUTPUT to retain native image and export specimens.");
    QVERIFY(QDir().mkpath(out));
    QImage picture(1200, 540, QImage::Format_RGB32);
    QPainter painter(&picture);
    QLinearGradient gradient(0, 0, 1200, 540);
    gradient.setColorAt(0, QColor("#d9eaff"));
    gradient.setColorAt(1, QColor("#edf8ed"));
    painter.fillRect(picture.rect(), gradient);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#ffffff"));
    painter.drawRoundedRect(QRectF(70, 65, 1060, 410), 25, 25);
    QFont font;
    font.setFamilies({"Segoe UI", "Microsoft YaHei"});
    font.setPointSize(31);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor("#314866"));
    painter.drawText(QRect(125, 115, 950, 90), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("A picture belongs with its words."));
    font.setPointSize(19);
    font.setBold(false);
    painter.setFont(font);
    painter.drawText(QRect(125, 225, 950, 90), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("图片与文字，一起保存。"));
    painter.setPen(QColor("#65809b"));
    font.setPointSize(14);
    painter.setFont(font);
    painter.drawText(QRect(125, 335, 950, 60), Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("Clipboard → document assets → portable exports"));
    painter.end();
    const auto path = QDir(out).filePath(QStringLiteral("图片示例.md"));
    QVERIFY(write(path, QStringLiteral("# Keep your ideas together\n\n粘贴截图，继续写作。Images "
                                       "stay beside your Markdown.\n\n")
                            .toUtf8()));
    platform::setLanguagePreference("en");
    platform::setThemePreference("light");
    MainWindow window;
    auto* editor = prepare(window, path);
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral("\n"));
    paste(editor, picture);
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(
        QStringLiteral("\n\nOne paste, one undo. Move the document and its assets together.\n"));
    const auto markdown = editor::serializeDocument(editor->document());
    QCOMPARE(images(editor->document()).size(), 1);
    QVERIFY(markdown.contains("One paste, one undo."));
    QTest::qWait(30);
    QVERIFY(window.grab().save(QDir(out).filePath("images-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(QDir(out).filePath("images-dark-zh.png")));
    QVERIFY(window.saveDocumentAs(path));
    const auto html =
        render::embedLocalImages(QString::fromStdString(core::toHtml(markdown.toStdString())),
                                 editor->document()->baseUrl());
    QVERIFY(write(QDir(out).filePath("images.html"),
                  QByteArray::fromStdString(core::wrapHtmlDocument(html.toStdString(), "Images"))));
    QString error;
    QVERIFY2(platform::exportPdf(QDir(out).filePath("images.pdf"), editor->document(), &error),
             qPrintable(error));
    QVERIFY2(platform::exportDocx(QDir(out).filePath("images.docx"), markdown, &error, out),
             qPrintable(error));
  }

 private:
  QTemporaryDir m_settings;
};
QTEST_MAIN(ImageAssetsTests)
#include "image_assets_tests.moc"
