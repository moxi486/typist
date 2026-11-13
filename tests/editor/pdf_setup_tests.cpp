#include "DocxWriter.h"
#include "PdfWriter.h"
#include "Settings.h"
#include "editor_document.h"

#include <QFile>
#include <QPageSize>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QXmlStreamReader>
#include <QtTest>
#include <limits>

namespace {
QByteArray read(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
int pageCount(const QByteArray& pdf) {
  const QRegularExpression page(QStringLiteral("/Type\\s*/Page\\b"));
  auto matches = page.globalMatch(QString::fromLatin1(pdf));
  int count = 0;
  while (matches.hasNext()) {
    matches.next();
    ++count;
  }
  return count;
}
QString specimen() {
  QString out = QStringLiteral(
      "# 分页 / Pages\n\n[TOC]\n\nA paragraph with **bold**, `code` and $E=mc^2$.\n\n");
  out += QStringLiteral("| Name | Value |\n| --- | ---: |\n");
  for (int i = 0; i < 100; ++i) out += QStringLiteral("| Table row %1 | %1 |\n").arg(i + 1);
  out += QStringLiteral("\n## A code block\n\n```cpp\nint value = 42;\nreturn value;\n```\n\n");
  for (int i = 0; i < 30; ++i)
    out +=
        QStringLiteral("Paragraph %1 has a few words to fill the page. 中文分页保留完整文字。\n\n")
            .arg(i + 1);
  return out;
}
}  // namespace
class PdfSetupTests : public QObject {
  Q_OBJECT
 private slots:
  void previewMatchesExport_data() {
    QTest::addColumn<QString>("paper");
    QTest::addColumn<bool>("landscape");
    QTest::addColumn<int>("paperId");
    QTest::newRow("a4-portrait") << QStringLiteral("a4") << false << int(QPageSize::A4);
    QTest::newRow("a4-landscape") << QStringLiteral("a4") << true << int(QPageSize::A4);
    QTest::newRow("letter-portrait") << QStringLiteral("letter") << false << int(QPageSize::Letter);
    QTest::newRow("letter-landscape") << QStringLiteral("letter") << true << int(QPageSize::Letter);
    QTest::newRow("a3-portrait") << QStringLiteral("a3") << false << int(QPageSize::A3);
    QTest::newRow("a5-landscape") << QStringLiteral("a5") << true << int(QPageSize::A5);
    QTest::newRow("legal-portrait") << QStringLiteral("legal") << false << int(QPageSize::Legal);
    QTest::newRow("custom-portrait") << QStringLiteral("custom") << false << int(QPageSize::Custom);
    QTest::newRow("custom-landscape") << QStringLiteral("custom") << true << int(QPageSize::Custom);
  }
  void previewMatchesExport() {
    QFETCH(QString, paper);
    QFETCH(bool, landscape);
    QFETCH(int, paperId);
    QTextDocument document;
    QString error;
    QVERIFY(typist::editor::loadMarkdown(specimen(), &document, &error));
    document.setUndoRedoEnabled(true);
    QTextCursor(&document).insertText(QStringLiteral("Edited "));
    const auto before = typist::editor::serializeDocument(&document);
    const auto undo = document.availableUndoSteps();
    typist::platform::PdfPageOptions options;
    options.paper = paper;
    options.landscape = landscape;
    options.customSizeMm = QSizeF(173.5, 244.2);
    options.header = QStringLiteral("{title} - #");
    options.footer = QStringLiteral("Page #");
    options.title = QStringLiteral("Preview / 预览");
    options.marginMm = 18;
    int count = 0;
    const auto preview = typist::platform::pdfPagePreview(&document, options, 0, &count, &error);
    QVERIFY2(!preview.isNull(), qPrintable(error));
    QVERIFY(count >= 2);
    const auto expectedPaper =
        paperId == QPageSize::Custom
            ? QPageSize(options.customSizeMm, QPageSize::Millimeter, {}, QPageSize::ExactMatch)
            : QPageSize(QPageSize::PageSizeId(paperId));
    auto expectedSize = expectedPaper.sizePixels(96);
    auto expectedPoints = expectedPaper.sizePoints();
    if (landscape) {
      expectedSize.transpose();
      expectedPoints.transpose();
    }
    QCOMPARE(preview.size(), expectedSize);
    QCOMPARE(preview.pixelColor(0, 0), QColor(Qt::white));
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    QVERIFY2(typist::platform::exportPdf(path, &document, options, &error), qPrintable(error));
    const auto bytes = read(path);
    QCOMPARE(pageCount(bytes), count);
    const QRegularExpression box(QStringLiteral(
        R"(/MediaBox\s*\[\s*0(?:\.0+)?\s+0(?:\.0+)?\s+(\d+(?:\.\d+)?)\s+(\d+(?:\.\d+)?)\s*\])"));
    const auto mediaBox = box.match(QString::fromLatin1(bytes));
    QVERIFY(mediaBox.hasMatch());
    QVERIFY(qAbs(mediaBox.captured(1).toDouble() - expectedPoints.width()) <= 1.0);
    QVERIFY(qAbs(mediaBox.captured(2).toDouble() - expectedPoints.height()) <= 1.0);
    const auto last =
        typist::platform::pdfPagePreview(&document, options, count - 1, nullptr, &error);
    const auto clamped =
        typist::platform::pdfPagePreview(&document, options, count + 10, nullptr, &error);
    QCOMPARE(last, clamped);
    QVERIFY(last != preview);
    QCOMPARE(typist::editor::serializeDocument(&document), before);
    QCOMPARE(document.availableUndoSteps(), undo);
    const auto output = qEnvironmentVariable("TYPIST_PDF_OUTPUT");
    if (!output.isEmpty()) {
      const auto stem =
          paper + (landscape ? QStringLiteral("-landscape") : QStringLiteral("-portrait"));
      QFile copy(output + '/' + stem + QStringLiteral(".pdf"));
      QVERIFY(copy.open(QIODevice::WriteOnly));
      QCOMPARE(copy.write(bytes), bytes.size());
      QVERIFY(preview.save(output + '/' + stem + QStringLiteral("-preview.png")));
      QVERIFY(last.save(output + '/' + stem + QStringLiteral("-last-preview.png")));
    }
  }

  void templatesUseTheFinalPageCount() {
    QTextDocument document;
    QVERIFY(typist::editor::loadMarkdown(
        QStringLiteral("# Chapter One\n\nFirst page.\n\n# Chapter Two\n\nSecond page.\n"),
        &document, nullptr));
    typist::platform::PdfPageOptions options;
    options.paper = QStringLiteral("custom");
    options.customSizeMm = QSizeF(173.5, 244.2);
    options.title = QStringLiteral("PDF # ${pageCount}");
    options.header = QStringLiteral("Edition ${title}");
    options.footer = QStringLiteral("Page ${pageNo} / ${pageCount} [${totalPages}, #]");
    options.breakBetweenTopHeadings = true;
    int count = 0;
    QString error;
    const auto preview = typist::platform::pdfPagePreview(&document, options, 1, &count, &error);
    QVERIFY2(!preview.isNull(), qPrintable(error));
    QCOMPARE(count, 2);
    auto expected = options;
    expected.header = QStringLiteral("Edition {title}");
    expected.footer = QStringLiteral("Page 2 / 2 [2, 2]");
    QCOMPARE(preview, typist::platform::pdfPagePreview(&document, expected, 1, nullptr, nullptr));

    QTemporaryDir directory;
    const auto output = qEnvironmentVariable("TYPIST_PDF_OUTPUT", directory.path());
    const auto path = output + QStringLiteral("/page-count-template.pdf");
    QVERIFY2(typist::platform::exportPdf(path, &document, options, &error), qPrintable(error));
    QCOMPARE(pageCount(read(path)), count);
  }
  void documentPropertiesInPreviewAndExport() {
    QTextDocument document;
    QVERIFY(typist::editor::loadMarkdown(QStringLiteral("# Chapter One\n\n正文内容。\n"), &document,
                                         nullptr));
    typist::platform::PdfPageOptions options;
    options.title = QStringLiteral("Fallback");
    options.metadataTitle = QStringLiteral("  Report <2026>  ");
    options.author = QStringLiteral("李明 & Mei");
    options.creator = QStringLiteral("Example Studio");
    options.subject = QStringLiteral("主题 <sample>");
    options.keywords = QStringLiteral("编辑, PDF & metadata");
    options.header = QStringLiteral("${title} / ${author}");
    options.footer = QStringLiteral("Page ${pageNo} / ${pageCount}");
    QString error;
    const auto preview = typist::platform::pdfPagePreview(&document, options, 0, nullptr, &error);
    QVERIFY2(!preview.isNull(), qPrintable(error));
    auto literal = options;
    literal.header = QStringLiteral("Report <2026> / 李明 & Mei");
    literal.footer = QStringLiteral("Page 1 / 1");
    QCOMPARE(preview, typist::platform::pdfPagePreview(&document, literal, 0, nullptr, nullptr));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto output = qEnvironmentVariable("TYPIST_PDF_OUTPUT", directory.path());
    const auto path = output + QStringLiteral("/document-properties.pdf");
    QVERIFY2(typist::platform::exportPdf(path, &document, options, &error), qPrintable(error));
    const auto pdf = read(path);
    QCOMPARE(pageCount(pdf), 1);
    const auto start = pdf.indexOf("<?xpacket");
    const auto end = pdf.indexOf("\nendstream", start);
    QVERIFY(start >= 0 && end > start);
    QXmlStreamReader xml(pdf.mid(start, end - start));
    QString title, author, subject, creator, keywords;
    while (!xml.atEnd()) {
      xml.readNext();
      if (!xml.isStartElement()) continue;
      const auto name = xml.qualifiedName().toString();
      if (name == QLatin1String("dc:title"))
        title = xml.readElementText(QXmlStreamReader::IncludeChildElements);
      else if (name == QLatin1String("dc:creator"))
        author = xml.readElementText(QXmlStreamReader::IncludeChildElements);
      else if (name == QLatin1String("dc:description"))
        subject = xml.readElementText(QXmlStreamReader::IncludeChildElements);
      else if (name == QLatin1String("xmp:CreatorTool"))
        creator = xml.readElementText();
      else if (name == QLatin1String("pdf:Keywords"))
        keywords = xml.readElementText();
    }
    QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
    QCOMPARE(title, options.metadataTitle.trimmed());
    QCOMPARE(author, options.author);
    QCOMPARE(subject, options.subject);
    QCOMPARE(creator, options.creator);
    QCOMPARE(keywords, options.keywords);

    options.subject.append(QChar(1));
    QVERIFY(!typist::platform::exportPdf(path, &document, options, &error));
    QCOMPARE(read(path), pdf);
  }

  void previewDrawsUnderline() {
    QTextDocument plain, marked;
    QVERIFY(typist::editor::loadMarkdown(QStringLiteral("Same words\n"), &plain, nullptr));
    QVERIFY(typist::editor::loadMarkdown(QStringLiteral("<u>Same words</u>\n"), &marked, nullptr));
    QCOMPARE(plain.toPlainText(), marked.toPlainText());
    const typist::platform::PdfPageOptions options;
    const auto before = typist::platform::pdfPagePreview(&plain, options, 0, nullptr, nullptr);
    const auto after = typist::platform::pdfPagePreview(&marked, options, 0, nullptr, nullptr);
    QVERIFY(!before.isNull() && !after.isNull());
    QVERIFY(before != after);
  }

  void sectionBreaksAreSharedByPreviewAndExport_data() {
    QTest::addColumn<QString>("markdown");
    QTest::addColumn<int>("sectionPages");
    QTest::newRow("chapters-and-footnotes")
        << QStringLiteral(
               "Opening paragraph.\n\n# Chapter One\n\nA paragraph with a note[^one].\n\n"
               "## A subsection\n\n> # A quoted heading\n\n"
               "# Chapter Two\n\nThe second chapter has another note[^two].\n\n"
               "[^one]: First note content.\n\n[^two]: Second note content.\n")
        << 3;
    QTest::newRow("consecutive-headings")
        << QStringLiteral("# First heading\n\n# Second heading\n\n# Third heading\n\nText.\n") << 3;
    QTest::newRow("footnotes-without-headings")
        << QStringLiteral("One reference[^note].\n\n[^note]: Note content.\n") << 2;
    QTest::newRow("footnotes-only")
        << QStringLiteral("[^one]: First note.\n\n[^two]: Second note.\n") << 1;
  }

  void sectionBreaksAreSharedByPreviewAndExport() {
    QFETCH(QString, markdown);
    QFETCH(int, sectionPages);
    QTextDocument document;
    QString error;
    QVERIFY2(typist::editor::loadMarkdown(markdown, &document, &error), qPrintable(error));
    const auto before = typist::editor::serializeDocument(&document);
    const auto undo = document.availableUndoSteps();
    typist::platform::PdfPageOptions options;
    options.header = QStringLiteral("Chapter layout");
    options.footer = QStringLiteral("Page #");
    int pages = 0;
    QVERIFY2(!typist::platform::pdfPagePreview(&document, options, 0, &pages, &error).isNull(),
             qPrintable(error));
    QCOMPARE(pages, 1);

    options.breakBetweenTopHeadings = true;
    QVERIFY2(!typist::platform::pdfPagePreview(&document, options, sectionPages - 1, &pages, &error)
                  .isNull(),
             qPrintable(error));
    QCOMPARE(pages, sectionPages);
    QTemporaryDir directory;
    const auto output = qEnvironmentVariable("TYPIST_PDF_OUTPUT", directory.path());
    const auto path =
        output + '/' + QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral(".pdf");
    QVERIFY2(typist::platform::exportPdf(path, &document, options, &error), qPrintable(error));
    QCOMPARE(pageCount(read(path)), pages);
    QCOMPARE(typist::editor::serializeDocument(&document), before);
    QCOMPARE(document.availableUndoSteps(), undo);
  }

  void manualBreaksStartHeadingsAndTablesWithoutBlankPages() {
    const auto marker = QString::fromLatin1(typist::core::kPageBreakMarkdown) + "\n\n";
    const auto markdown = marker + "# First chapter\n\nBefore the break.\n\n" + marker + marker +
                          "# Second chapter\n\nAfter the break.\n\n" + marker +
                          "| Appendix | Status |\n| --- | --- |\n| Detail | Complete |\n\n" +
                          marker;
    QTextDocument document;
    QString error;
    QVERIFY2(typist::editor::loadMarkdown(markdown, &document, &error), qPrintable(error));
    const auto before = typist::editor::serializeDocument(&document);
    const auto parsed = typist::core::parseMarkdown(markdown.toStdString());
    const auto saved = typist::core::parseMarkdown(before.toStdString());
    QVERIFY(typist::core::astEqual(parsed.blocks, saved.blocks));
    QTemporaryDir directory;
    const auto output = qEnvironmentVariable("TYPIST_PDF_OUTPUT", directory.path());
    for (const bool headings : {false, true}) {
      typist::platform::PdfPageOptions options;
      options.breakBetweenTopHeadings = headings;
      options.header = QStringLiteral("Manual page breaks");
      options.footer = QStringLiteral("Page #");
      int pages = 0;
      QVERIFY2(!typist::platform::pdfPagePreview(&document, options, 0, &pages, &error).isNull(),
               qPrintable(error));
      QCOMPARE(pages, 3);
      const auto path = output + (headings ? "/manual-and-heading.pdf" : "/manual-breaks.pdf");
      QVERIFY2(typist::platform::exportPdf(path, &document, options, &error), qPrintable(error));
      QCOMPARE(pageCount(read(path)), pages);
    }
    QCOMPARE(typist::editor::serializeDocument(&document), before);
    const auto docx = directory.filePath(QStringLiteral("manual-breaks.docx"));
    QVERIFY2(typist::platform::exportDocx(docx, before, &error), qPrintable(error));
    QCOMPARE(read(docx).count("<w:br w:type=\"page\"/>"), 2);
  }

  void invalidExportPreservesTarget() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("existing.pdf"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("existing PDF");
    file.close();
    QTextDocument document;
    document.setPlainText(QStringLiteral("Text"));
    QString error;
    typist::platform::PdfPageOptions options;
    for (const auto margin : {-1., 500., std::numeric_limits<double>::quiet_NaN()}) {
      options.marginMm = margin;
      QVERIFY(!typist::platform::exportPdf(path, &document, options, &error));
      int pages = 9;
      QVERIFY(typist::platform::pdfPagePreview(&document, options, 0, &pages, &error).isNull());
      QCOMPARE(pages, 0);
      QCOMPARE(read(path), QByteArray("existing PDF"));
    }
    options.paper = QStringLiteral("custom");
    options.marginMm = 20;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    for (const auto& size : {QSizeF(49, 200), QSizeF(200, 1001), QSizeF(nan, 200)}) {
      options.customSizeMm = size;
      QVERIFY(!typist::platform::exportPdf(path, &document, options, &error));
      int pages = 9;
      QVERIFY(typist::platform::pdfPagePreview(&document, options, 0, &pages, &error).isNull());
      QCOMPARE(pages, 0);
      QCOMPARE(read(path), QByteArray("existing PDF"));
    }
    options.customSizeMm = QSizeF(50, 50);
    options.header = QStringLiteral("Header");
    options.footer = QStringLiteral("Footer");
    QVERIFY(!typist::platform::exportPdf(path, &document, options, &error));
    QVERIFY(typist::platform::pdfPagePreview(&document, options, 0, nullptr, &error).isNull());
    QCOMPARE(read(path), QByteArray("existing PDF"));
    QVERIFY(!typist::platform::exportPdf(path, nullptr, &error));
    QCOMPARE(read(path), QByteArray("existing PDF"));
    QVERIFY(!typist::platform::exportPdf(directory.filePath(QStringLiteral("missing/document.pdf")),
                                         &document, &error));
  }
  void settingsDoNotPersistDocumentTitle() {
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QVERIFY(!typist::platform::pdfOptions(QString()).breakBetweenTopHeadings);
    typist::platform::PdfPageOptions options;
    options.paper = QStringLiteral("custom");
    options.customSizeMm = QSizeF(173.5, 244.2);
    options.marginMm = 12.5;
    options.landscape = true;
    options.header = QStringLiteral("{title}");
    options.footer = QStringLiteral("#");
    options.repeatTableHeader = false;
    options.breakBetweenTopHeadings = true;
    options.title = QStringLiteral("Old title");
    typist::platform::setPdfOptions(options);
    const auto saved = typist::platform::pdfOptions(QStringLiteral("New title"));
    QCOMPARE(saved.paper, options.paper);
    QCOMPARE(saved.customSizeMm, options.customSizeMm);
    QCOMPARE(saved.marginMm, 12.5);
    QVERIFY(saved.landscape);
    QCOMPARE(saved.header, options.header);
    QCOMPARE(saved.footer, options.footer);
    QVERIFY(!saved.repeatTableHeader);
    QVERIFY(saved.breakBetweenTopHeadings);
    QCOMPARE(saved.title, QStringLiteral("New title"));
  }
};
QTEST_MAIN(PdfSetupTests)
#include "pdf_setup_tests.moc"
