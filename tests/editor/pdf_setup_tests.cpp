#include "PdfWriter.h"
#include "Settings.h"
#include "editor_document.h"

#include <QFile>
#include <QPageSize>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextDocument>
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
    QTest::newRow("a4-portrait") << QStringLiteral("a4") << false;
    QTest::newRow("a4-landscape") << QStringLiteral("a4") << true;
    QTest::newRow("letter-portrait") << QStringLiteral("letter") << false;
    QTest::newRow("letter-landscape") << QStringLiteral("letter") << true;
  }
  void previewMatchesExport() {
    QFETCH(QString, paper);
    QFETCH(bool, landscape);
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
    options.header = QStringLiteral("{title} - #");
    options.footer = QStringLiteral("Page #");
    options.title = QStringLiteral("Preview / 预览");
    options.marginMm = 18;
    int count = 0;
    const auto preview = typist::platform::pdfPagePreview(&document, options, 0, &count, &error);
    QVERIFY2(!preview.isNull(), qPrintable(error));
    QVERIFY(count >= 3);
    auto expected =
        QPageSize(paper == QLatin1String("a4") ? QPageSize::A4 : QPageSize::Letter).sizePixels(96);
    if (landscape) expected.transpose();
    QCOMPARE(preview.size(), expected);
    QCOMPARE(preview.pixelColor(0, 0), QColor(Qt::white));
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("document.pdf"));
    QVERIFY2(typist::platform::exportPdf(path, &document, options, &error), qPrintable(error));
    QCOMPARE(pageCount(read(path)), count);
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
      const auto bytes = read(path);
      QCOMPARE(copy.write(bytes), bytes.size());
      QVERIFY(preview.save(output + '/' + stem + QStringLiteral("-preview.png")));
      QVERIFY(last.save(output + '/' + stem + QStringLiteral("-last-preview.png")));
    }
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
    QVERIFY(!typist::platform::exportPdf(path, nullptr, &error));
    QCOMPARE(read(path), QByteArray("existing PDF"));
    QVERIFY(!typist::platform::exportPdf(directory.filePath(QStringLiteral("missing/document.pdf")),
                                         &document, &error));
  }
  void settingsDoNotPersistDocumentTitle() {
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    typist::platform::PdfPageOptions options;
    options.paper = QStringLiteral("letter");
    options.marginMm = 12.5;
    options.landscape = true;
    options.header = QStringLiteral("{title}");
    options.footer = QStringLiteral("#");
    options.repeatTableHeader = false;
    options.title = QStringLiteral("Old title");
    typist::platform::setPdfOptions(options);
    const auto saved = typist::platform::pdfOptions(QStringLiteral("New title"));
    QCOMPARE(saved.paper, options.paper);
    QCOMPARE(saved.marginMm, 12.5);
    QVERIFY(saved.landscape);
    QCOMPARE(saved.header, options.header);
    QCOMPARE(saved.footer, options.footer);
    QVERIFY(!saved.repeatTableHeader);
    QCOMPARE(saved.title, QStringLiteral("New title"));
  }
};
QTEST_MAIN(PdfSetupTests)
#include "pdf_setup_tests.moc"
