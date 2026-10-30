#pragma once

#include "FolderSearchRunner.h"

#include <QWidget>

class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

namespace typist::ui {
class WorkspaceSearchPanel : public QWidget {
  Q_OBJECT
 public:
  explicit WorkspaceSearchPanel(QWidget* parent = nullptr);
  void setRoot(const QString& root);
  QString rootPath() const { return m_root; }
  void openFocused(const QString& seed = QString());
  void stop();

 signals:
  void hitActivated(typist::platform::SearchHit hit);

 protected:
  void changeEvent(QEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void search();
  void invalidate();
  void showResults(const QVector<platform::SearchHit>& hits, bool cancelled, const QString& error);
  void retranslate();
  void updateStatus();
  void setBusy(bool busy);
  void activateItem(QTreeWidgetItem* item);
  QString m_root;
  platform::FolderSearchRunner m_runner;
  QTimer* m_debounce = nullptr;
  QLineEdit* m_query = nullptr;
  QCheckBox* m_case = nullptr;
  QCheckBox* m_words = nullptr;
  QCheckBox* m_regex = nullptr;
  QPushButton* m_search = nullptr;
  QPushButton* m_cancel = nullptr;
  QTreeWidget* m_results = nullptr;
  QLabel* m_scope = nullptr;
  QLabel* m_status = nullptr;
  QLabel* m_hint = nullptr;
  QVector<platform::SearchHit> m_hits;
  bool m_busy = false;
  bool m_done = false;
  bool m_cancelled = false;
  QString m_error;
};
}  // namespace typist::ui
