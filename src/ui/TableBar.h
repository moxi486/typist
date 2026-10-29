#pragma once
#include "Editing.h"

#include <QFrame>
class QLabel;
class QAction;
namespace typist::ui {
class TableBar : public QFrame {
  Q_OBJECT
 public:
  explicit TableBar(QWidget* parent);
  void setTableState(int rows, int columns, int row, int column, int selectedRows,
                     int selectedColumns, int alignment);
  void setDark(bool dark);
 signals:
  void operationRequested(editor::TableCommand command);
  void copyRequested(bool spreadsheet);
  void exitRequested();

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void retranslate();
  QLabel* m_dimensions = nullptr;
  QList<QAction*> m_actions;
  QAction* m_deleteRow = nullptr;
  QAction* m_deleteColumn = nullptr;
  QList<QAction*> m_addRows, m_addColumns, m_alignments;
};
}  // namespace typist::ui
