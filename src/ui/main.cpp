#include "MainWindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[]) {
  QElapsedTimer startup;
  startup.start();

  QApplication application(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Typist"));
  QApplication::setOrganizationName(QStringLiteral("Typist"));

  const QStringList args = application.arguments();
  const bool benchStartup = args.contains(QStringLiteral("--bench-startup"));
  const bool benchRepaint = args.contains(QStringLiteral("--bench-repaint"));

  typist::MainWindow window;
  const QByteArray benchFile = qgetenv("TYPIST_BENCH_FILE");
  if (!benchFile.isEmpty()) window.openFile(QString::fromUtf8(benchFile));
  window.show();

  int repaintBudgetBreaches = 0;
  if (benchStartup || benchRepaint) {
    QTimer::singleShot(0, &application, [&startup, &application, &window, benchStartup, benchRepaint, &repaintBudgetBreaches] {
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
      application.quit();
    });
  }

  const int code = application.exec();
  if (benchRepaint && repaintBudgetBreaches > 0) return 1;
  return code;
}
