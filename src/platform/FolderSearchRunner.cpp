#include "FolderSearchRunner.h"

#include <QMetaType>

namespace typist::platform {

FolderSearchRunner::FolderSearchRunner(QObject* parent) : QObject(parent) {
  // Results cross a thread boundary, so the hit list needs a registered metatype.
  qRegisterMetaType<QVector<SearchHit>>();
}

FolderSearchRunner::~FolderSearchRunner() {
  if (!m_thread) return;
  m_thread->cancelled.store(true);
  m_thread->wait(2000);
  delete m_thread;
  m_thread = nullptr;
}

void FolderSearchRunner::start(const SearchRequest& request) {
  if (m_thread) {
    m_thread->cancelled.store(true);
    m_thread->wait(2000);
    delete m_thread;
    m_thread = nullptr;
  }
  m_thread = new ScanThread();
  m_thread->request = request;
  connect(m_thread, &QThread::finished, this, &FolderSearchRunner::reportFinished);
  m_thread->start();
}

void FolderSearchRunner::cancel() {
  if (m_thread) m_thread->cancelled.store(true);
}

bool FolderSearchRunner::isRunning() const { return m_thread && m_thread->isRunning(); }

void FolderSearchRunner::reportFinished() {
  if (!m_thread) return;
  emit resultsReady(m_thread->hits, m_thread->wasCancelled, m_thread->error);
  m_thread->deleteLater();
  m_thread = nullptr;
}

}  // namespace typist::platform
