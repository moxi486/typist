#include "FolderSearchRunner.h"

#include <QMetaType>

namespace typist::platform {
FolderSearchRunner::FolderSearchRunner(QObject* parent) : QObject(parent) {
  qRegisterMetaType<QVector<SearchHit>>();
  qRegisterMetaType<SearchHit>();
}
FolderSearchRunner::~FolderSearchRunner() {
  if (!m_thread) return;
  m_thread->cancelled.store(true);
  m_thread->wait();
  delete m_thread;
}
void FolderSearchRunner::start(const SearchRequest& request) {
  if (m_thread) {
    m_pending = request;
    m_discard = true;
    m_thread->cancelled.store(true);
    return;
  }
  m_discard = false;
  m_cancelled = false;
  m_cancelPending = false;
  m_stats = {};
  m_thread = new ScanThread();
  m_thread->request = request;
  connect(m_thread, &QThread::finished, this, &FolderSearchRunner::reportFinished);
  m_thread->start();
}
void FolderSearchRunner::cancel() {
  if (m_pending) {
    m_discard = false;
    m_cancelPending = true;
  }
  m_pending.reset();
  if (m_thread) {
    m_cancelled = true;
    m_thread->cancelled.store(true);
  }
}
void FolderSearchRunner::discard() {
  cancel();
  m_discard = true;
}
bool FolderSearchRunner::isRunning() const { return m_thread != nullptr; }
void FolderSearchRunner::reportFinished() {
  auto* finished = m_thread;
  if (!finished) return;
  m_thread = nullptr;
  finished->deleteLater();
  if (m_pending) {
    const auto request = *m_pending;
    m_pending.reset();
    start(request);
    return;
  }
  if (m_discard) return;
  if (m_cancelPending) {
    m_stats = {};
    emit resultsReady({}, true, QString());
    return;
  }
  m_stats = finished->stats;
  emit resultsReady(finished->hits, finished->wasCancelled || m_cancelled, finished->error);
}
}  // namespace typist::platform
