#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace typist::ui {

// Inline find bar: pattern field, case toggle, match count and next/previous step.
class FindBar : public QWidget {
  Q_OBJECT

 public:
  explicit FindBar(QWidget* parent = nullptr);

  QString pattern() const;
  bool caseSensitive() const;
  bool regex() const;
  void setMatchCount(int count);
  void openFocused();
  void closeBar();
  void setPattern(const QString& pattern);

 signals:
  void findNext();
  void findPrevious();
  void patternChanged();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  QLineEdit* m_edit = nullptr;
  QCheckBox* m_case = nullptr;
  QCheckBox* m_regex = nullptr;
  QLabel* m_count = nullptr;
  QPushButton* m_next = nullptr;
  QPushButton* m_prev = nullptr;
};

}  // namespace typist::ui
