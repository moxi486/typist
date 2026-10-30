#pragma once

#include "FolderLocations.h"
#include "QuickOpen.h"

#include <QDialog>
#include <QThread>

class QLineEdit;
class QListWidget;
class QLabel;
class QPushButton;

namespace typist::ui {

class FileIndexThread : public QThread {
 public:
  QString root;
  std::atomic<bool> cancelled{false};
  platform::FileIndex result;

 protected:
  void run() override { result = platform::indexMarkdownFiles(root, cancelled); }
};

class QuickOpenDialog : public QDialog {
  Q_OBJECT
 public:
  QuickOpenDialog(const QString& root, const QStringList& recent, QWidget* parent = nullptr,
                  const platform::FolderLocations& locations = {});
  ~QuickOpenDialog() override;
  QString selectedPath() const;
  bool selectedIsFolder() const;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void changeEvent(QEvent* event) override;

 private:
  void updateResults();
  void retranslate();
  void openSelected();
  QString m_root;
  QStringList m_recent;
  QStringList m_paths;
  platform::FolderLocations m_locations;
  FileIndexThread m_index;
  bool m_scanning = true;
  QLineEdit* m_query = nullptr;
  QListWidget* m_list = nullptr;
  QLabel* m_status = nullptr;
  QLabel* m_hint = nullptr;
  QPushButton* m_open = nullptr;
  QPushButton* m_cancel = nullptr;
};
}  // namespace typist::ui
