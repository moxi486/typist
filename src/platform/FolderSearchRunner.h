#pragma once

#include "FileSearch.h"

#include <QObject>
#include <QThread>
#include <atomic>
#include <optional>

namespace typist::platform {

class ScanThread : public QThread {
 public:
  SearchRequest request;
  std::atomic<bool> cancelled{false};
  QVector<SearchHit> hits;
  bool wasCancelled = false;
  QString error;
  SearchStats stats;

 protected:
  void run() override {
    hits = searchFilesCancellable(request, &error, cancelled, &wasCancelled, &stats);
  }
};

// Runs the Markdown scan off the UI thread; a scan can be cancelled and returns partial hits.
class FolderSearchRunner : public QObject {
  Q_OBJECT

 public:
  explicit FolderSearchRunner(QObject* parent = nullptr);
  ~FolderSearchRunner() override;

  void start(const SearchRequest& request);
  void cancel();
  // Cancel and suppress results, for a changed query or closed panel.
  void discard();
  bool isRunning() const;
  const SearchStats& stats() const { return m_stats; }

 signals:
  void resultsReady(const QVector<typist::platform::SearchHit>& hits, bool cancelled,
                    const QString& error);

 private slots:
  void reportFinished();

 private:
  ScanThread* m_thread = nullptr;
  std::optional<SearchRequest> m_pending;
  bool m_discard = false;
  bool m_cancelled = false;
  bool m_cancelPending = false;
  SearchStats m_stats;
};

}  // namespace typist::platform
