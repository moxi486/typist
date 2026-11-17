#pragma once
#include "Editing.h"

#include <QFrame>
#include <QPair>
class QLabel;
class QAction;
namespace typist::editor {
struct TableCells;
}
namespace typist::ui {
class TableBar : public QFrame {
  Q_OBJECT
 public:
  explicit TableBar(QWidget* parent);
  void setTableState(const QTextCursor& cursor, const editor::TableCells& selection, int alignment);
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
  QList<QPair<QAction*, editor::TableCommand>> m_moves;
};
}  // namespace typist::ui
