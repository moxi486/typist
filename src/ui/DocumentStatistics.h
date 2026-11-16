#pragma once

#include <QTimer>
#include <QToolButton>
#include <array>
#include <functional>

class QAction;

namespace typist::ui {

struct StatisticsInput {
  QString markdown;
  bool selection = false;
};

class StatisticsJob;

class DocumentStatisticsButton : public QToolButton {
  Q_OBJECT

 public:
  explicit DocumentStatisticsButton(std::function<StatisticsInput()> snapshot, QWidget* parent);
  ~DocumentStatisticsButton() override;
  void schedule();
  void refreshPreferences();

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void startAnalysis();
  void updateLabels();

  std::function<StatisticsInput()> m_snapshot;
  QTimer m_debounce;
  StatisticsJob* m_job = nullptr;
  quint64 m_revision = 0;
  quint64 m_presentedRevision = 0;
  std::array<qsizetype, 4> m_counts{};
  std::array<QAction*, 4> m_units{};
  QAction* m_scope = nullptr;
  QAction* m_readingTime = nullptr;
  int m_unit = 0;
  int m_speed = 200;
  bool m_selection = false;
};

}  // namespace typist::ui
