#pragma once

#include <QObject>
#include <QThread>

#include <atomic>

#include "FileSearch.h"

namespace typist::platform {

class ScanThread : public QThread {
public:
  SearchRequest request;
  std::atomic<bool> cancelled{false};
  QVector<SearchHit> hits;
  bool wasCancelled = false;
  QString error;

protected:
  void run() override { hits = searchFilesCancellable(request, &error, cancelled, &wasCancelled); }
};

// Runs the Markdown scan off the UI thread; a scan can be cancelled and returns partial hits.
class FolderSearchRunner : public QObject {
  Q_OBJECT

public:
  explicit FolderSearchRunner(QObject* parent = nullptr);
  ~FolderSearchRunner() override;

  void start(const SearchRequest& request);
  void cancel();
  bool isRunning() const;

signals:
  void resultsReady(const QVector<typist::platform::SearchHit>& hits, bool cancelled, const QString& error);

private slots:
  void reportFinished();

private:
  ScanThread* m_thread = nullptr;
};

}  // namespace typist::platform
