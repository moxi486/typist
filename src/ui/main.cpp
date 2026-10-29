#include "Localization.h"
#include "MainWindow.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
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

  const QStringList args = application.arguments();
  const bool benchStartup = args.contains(QStringLiteral("--bench-startup"));
  const bool benchRepaint = args.contains(QStringLiteral("--bench-repaint"));
  const bool benchViewSwitch = args.contains(QStringLiteral("--bench-view-switch"));
  const bool benchSearch = args.contains(QStringLiteral("--bench-search"));
  const bool benchFolderSearch = args.contains(QStringLiteral("--bench-file-search"));
  const bool benchTree = args.contains(QStringLiteral("--bench-tree"));
  const bool benchDocx = args.contains(QStringLiteral("--bench-docx"));
  const bool benchEpub = args.contains(QStringLiteral("--bench-epub"));
  const bool benchHtmlImport = args.contains(QStringLiteral("--bench-import-html"));
  const bool benchPdf = args.contains(QStringLiteral("--bench-pdf"));

  typist::MainWindow window;
  window.setBenchMode(benchStartup || benchRepaint || benchViewSwitch || benchSearch ||
                      benchFolderSearch || benchTree || benchDocx || benchEpub || benchHtmlImport ||
                      benchPdf);
  bool htmlImported = false;
  const auto openInput = [&](const QString& path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
      htmlImported = window.importHtmlFile(path);
    else
      window.openFile(path);
  };
  const QByteArray benchFile = qgetenv("TYPIST_BENCH_FILE");
  if (!benchFile.isEmpty()) openInput(QString::fromUtf8(benchFile));
  if (benchFile.isEmpty()) {
    for (const QString& argument : args.mid(1)) {
      if (!argument.startsWith(QStringLiteral("--")) && QFileInfo(argument).isFile()) {
        openInput(argument);
        break;
      }
    }
  }
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

  if (benchStartup || benchRepaint || benchViewSwitch || benchSearch || benchFolderSearch ||
      benchTree || benchDocx || benchEpub || benchHtmlImport || benchPdf) {
    QTimer::singleShot(
        0, &application,
        [&startup, &application, &window, benchStartup, benchRepaint, benchViewSwitch, benchSearch,
         benchFolderSearch, benchTree, benchDocx, &repaintBudgetBreaches, &undoKept, &searchMatches,
         &folderHits, &folderScanMs, &treeEntries, &treeDirectories, &docxBytes, benchEpub,
         &epubBytes, benchHtmlImport, htmlImported, benchPdf, &pdfBytes] {
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
            if (directory.isValid())
              epubBytes = window.exportEpubForBench(
                  directory.filePath(QStringLiteral("book.epub")),
                  {QStringLiteral("Typist"), {}, QStringLiteral("zh-CN")});
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
