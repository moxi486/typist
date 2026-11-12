#include "Localization.h"
#include "MainWindow.h"
#include "Settings.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
  QElapsedTimer startup;
  startup.start();

  QApplication application(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Typist"));
  QApplication::setOrganizationName(QStringLiteral("Typist"));
  QApplication::setStyle(QStringLiteral("Fusion"));
  QApplication::setWindowIcon(QIcon(QStringLiteral(":/packaging/linux/typist.png")));
  QFont interfaceFont = application.font();
  interfaceFont.setPointSize(10);
  application.setFont(interfaceFont);

  const QStringList args = application.arguments().mid(1);
  const int delimiter = args.indexOf(QStringLiteral("--"));
  const auto flags = delimiter < 0 ? args : args.mid(0, delimiter);
  const bool benchStartup = flags.contains(QStringLiteral("--bench-startup"));
  const bool benchRepaint = flags.contains(QStringLiteral("--bench-repaint"));
  const bool benchViewSwitch = flags.contains(QStringLiteral("--bench-view-switch"));
  const bool benchSearch = flags.contains(QStringLiteral("--bench-search"));
  const bool benchFolderSearch = flags.contains(QStringLiteral("--bench-file-search"));
  const bool benchTree = flags.contains(QStringLiteral("--bench-tree"));
  const bool benchDocx = flags.contains(QStringLiteral("--bench-docx"));
  const bool benchEpubChapters = flags.contains(QStringLiteral("--bench-epub-chapters"));
  const bool benchEpub = flags.contains(QStringLiteral("--bench-epub")) || benchEpubChapters;
  const bool benchHtmlImport = flags.contains(QStringLiteral("--bench-import-html"));
  const bool benchPdf = flags.contains(QStringLiteral("--bench-pdf"));

  const bool benchmark = benchStartup || benchRepaint || benchViewSwitch || benchSearch ||
                         benchFolderSearch || benchTree || benchDocx || benchEpub ||
                         benchHtmlImport || benchPdf;
  typist::MainWindow window;
  window.setBenchMode(benchmark);
  const auto target = typist::platform::startupTarget(
      args, benchmark, QString::fromUtf8(qgetenv("TYPIST_BENCH_FILE")));
  const bool opened = window.openStartupTarget(target);
  const bool htmlImported = target.importHtml && opened;
  window.show();

  int repaintBudgetBreaches = 0;
  bool undoKept = false;
  int searchMatches = 0;
  int folderHits = 0;
  qint64 folderScanMs = 0;
  int treeEntries = 0;
  int treeDirectories = 0;
  qint64 docxBytes = 0;
  qint64 epubBytes = 0;
  qint64 pdfBytes = 0;

  if (benchmark) {
    QTimer::singleShot(
        0, &application,
        [&startup, &application, &window, benchStartup, benchRepaint, benchViewSwitch, benchSearch,
         benchFolderSearch, benchTree, benchDocx, &repaintBudgetBreaches, &undoKept, &searchMatches,
         &folderHits, &folderScanMs, &treeEntries, &treeDirectories, &docxBytes, benchEpub,
         &epubBytes, benchHtmlImport, htmlImported, benchPdf, &pdfBytes, benchEpubChapters] {
          if (benchHtmlImport) std::printf("html_import_ok=%d\n", htmlImported ? 1 : 0);
          if (benchStartup) {
            std::printf("startup_ms=%lld\n", static_cast<long long>(startup.elapsed()));
          }
          if (benchRepaint) {
            std::printf("doc_blocks=%d\n", window.documentBlockCount());
            // Three keystrokes: the first paints a fresh layout, the rest paint an incremental one.
            for (int i = 0; i < 3; ++i) {
              const long long ms = static_cast<long long>(window.measureTypingRepaint());
              std::printf("repaint_ms=%lld\n", ms);
              // The budget is typing-to-repaint under 16ms, i.e. one frame at 60Hz.
              if (ms > 16) ++repaintBudgetBreaches;
            }
          }
          if (benchViewSwitch) {
            undoKept = window.viewSwitchKeepsUndoForBench();
            std::printf("view_undo_kept=%d\n", undoKept ? 1 : 0);
          }
          if (benchSearch) {
            const QString pattern = QString::fromUtf8(qgetenv("TYPIST_SEARCH_PATTERN"));
            searchMatches =
                window.searchMatchesForBench(pattern.isEmpty() ? QStringLiteral("item") : pattern);
            std::printf("search_matches=%d\n", searchMatches);
          }
          if (benchFolderSearch) {
            const QString root = QString::fromUtf8(qgetenv("TYPIST_SEARCH_ROOT"));
            const QString pattern = QString::fromUtf8(qgetenv("TYPIST_SEARCH_PATTERN"));
            folderScanMs = window.searchFolderForBench(
                root.isEmpty() ? QDir::homePath() : root,
                pattern.isEmpty() ? QStringLiteral("item") : pattern, &folderHits);
            std::printf("file_search_ms=%lld file_search_hits=%d\n",
                        static_cast<long long>(folderScanMs), folderHits);
          }
          if (benchTree) {
            const QString root = QString::fromUtf8(qgetenv("TYPIST_TREE_ROOT"));
            treeEntries =
                window.fileTreeEntriesForBench(root.isEmpty() ? QDir::currentPath() : root);
            treeDirectories = window.fileTreeDirectoriesForBench(root);
            std::printf("tree_entries=%d tree_dirs=%d\n", treeEntries, treeDirectories);
          }
          if (benchDocx) {
            const QString target = QDir::tempPath() + QStringLiteral("/typist_bench.docx");
            docxBytes = window.exportDocxForBench(target);
            QFile::remove(target);
            std::printf("docx_bytes=%lld\n", static_cast<long long>(docxBytes));
          }
          if (benchEpub) {
            QTemporaryDir directory;
            if (directory.isValid()) {
              typist::platform::EpubOptions options{
                  QStringLiteral("Typist"), {}, QStringLiteral("zh-CN")};
              options.splitChapters = benchEpubChapters;
              options.coverImage = QString::fromUtf8(qgetenv("TYPIST_EPUB_COVER"));
              epubBytes = window.exportEpubForBench(directory.filePath(QStringLiteral("book.epub")),
                                                    options);
            }
            std::printf("epub_bytes=%lld\n", static_cast<long long>(epubBytes));
          }
          if (benchPdf) {
            QTemporaryDir directory;
            if (directory.isValid())
              pdfBytes =
                  window.exportPdfForBench(directory.filePath(QStringLiteral("document.pdf")), {});
            std::printf("pdf_bytes=%lld\n", static_cast<long long>(pdfBytes));
          }
          application.quit();
        });
  }

  const int code = application.exec();
  if (benchRepaint && repaintBudgetBreaches > 0) return 1;
  if (benchViewSwitch && !undoKept) return 1;
  if (benchSearch && searchMatches == 0) return 1;
  if (benchFolderSearch && (folderHits == 0 || folderScanMs > 250)) return 1;
  if (benchTree && treeEntries == 0) return 1;
  if (benchDocx && docxBytes < 400) return 1;
  if (benchEpub && epubBytes < 400) return 1;
  if (benchHtmlImport && !htmlImported) return 1;
  if (benchPdf && pdfBytes < 400) return 1;
  return code;
}
