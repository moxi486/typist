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
  void retranslateUi();
  QString replacement() const;
  void openReplace();

 signals:
  void findNext();
  void findPrevious();
  void patternChanged();
  void replaceOne();
  void replaceAll();

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  QLineEdit* m_edit = nullptr;
  QCheckBox* m_case = nullptr;
  QCheckBox* m_regex = nullptr;
  QLabel* m_count = nullptr;
  QPushButton* m_next = nullptr;
  QPushButton* m_prev = nullptr;
  int m_matchCount = 0;
  QLineEdit* m_replacement = nullptr;
  QPushButton* m_replaceOne = nullptr;
  QPushButton* m_replaceAll = nullptr;
  QWidget* m_replaceRow = nullptr;
};

}  // namespace typist::ui
