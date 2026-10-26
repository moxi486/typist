#include "AutoSave.h"

#include <QTimer>

namespace typist::ui {

AutoSave::AutoSave(QObject* parent) : QObject(parent) {
  m_timer = new QTimer(this);
  m_timer->setSingleShot(true);
  m_timer->setInterval(2000);
  connect(m_timer, &QTimer::timeout, this, [this] {
    if (m_callback) m_callback();
  });
}

void AutoSave::setInterval(int milliseconds) { m_timer->setInterval(milliseconds); }

int AutoSave::interval() const { return m_timer->interval(); }

void AutoSave::setCallback(std::function<void()> callback) { m_callback = std::move(callback); }

void AutoSave::notifyChanged() { m_timer->start(); }

void AutoSave::flushNow() {
  m_timer->stop();
  if (m_callback) m_callback();
}

bool AutoSave::isPending() const { return m_timer->isActive(); }

}  // namespace typist::ui
