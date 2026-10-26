#include "MainWindow.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QString>
#include <QTimer>

#include <cstdio>

int main(int argc, char* argv[]) {
  QElapsedTimer startup;
  startup.start();

  QApplication application(argc, argv);
  QApplication::setApplicationName(QStringLiteral("Typist"));
  QApplication::setOrganizationName(QStringLiteral("Typist"));

  const bool benchStartup = application.arguments().contains(QStringLiteral("--bench-startup"));

  typist::MainWindow window;
  window.show();

  if (benchStartup) {
    // Measured up to the first event-loop iteration, which is when the window is on screen.
    QTimer::singleShot(0, &application, [&startup, &application] {
      std::printf("startup_ms=%lld\n", static_cast<long long>(startup.elapsed()));
      application.quit();
    });
  }

  return application.exec();
}
