#pragma once

#include <QObject>
#include <functional>

class QTimer;

namespace typist::ui {

// Debounced flush: edits restart the window, a burst of typing costs one write.
class AutoSave : public QObject {
  Q_OBJECT

 public:
  explicit AutoSave(QObject* parent = nullptr);

  void setInterval(int milliseconds);
  int interval() const;
  void setCallback(std::function<void()> callback);
  void notifyChanged();
  void flushNow();
  bool isPending() const;

 private:
  QTimer* m_timer = nullptr;
  std::function<void()> m_callback;
};

}  // namespace typist::ui
