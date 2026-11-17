#include "TableBar.h"

#include "TableEditing.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTextTable>
#include <QToolButton>

namespace typist::ui {
TableBar::TableBar(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("tableBar"));
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(6, 1, 4, 1);
  layout->setSpacing(3);
  m_dimensions = new QPushButton(this);
  m_dimensions->setObjectName(QStringLiteral("tableDimensions"));
  m_dimensions->setFocusPolicy(Qt::NoFocus);
  m_dimensions->setCursor(Qt::PointingHandCursor);
  connect(m_dimensions, &QPushButton::clicked, this, &TableBar::resizeRequested);
  layout->addWidget(m_dimensions);
  const auto menuButton = [&](const char* id, const char* text) {
    auto* button = new QToolButton(this);
    button->setObjectName(QString::fromLatin1(id));
    button->setProperty("tableTranslationSource", QString::fromLatin1(text));
    button->setFocusPolicy(Qt::NoFocus);
    button->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(button);
    button->setMenu(menu);
    layout->addWidget(button);
    return menu;
  };
  const auto action = [&](QMenu* menu, const char* id, const char* text,
                          editor::TableCommand command) {
    auto* item = menu->addAction(QString());
    item->setObjectName(QString::fromLatin1(id));
    item->setProperty("tableTranslationSource", QString::fromLatin1(text));
    m_actions.append(item);
    connect(item, &QAction::triggered, this, [this, command] { emit operationRequested(command); });
    return item;
  };
  const auto move = [&](QMenu* menu, const char* id, const char* text,
                        editor::TableCommand command) {
    m_moves.append({action(menu, id, text, command), command});
  };
  auto* rows = menuButton("tableRowsMenu", QT_TR_NOOP("Rows"));
  m_addRows.append(action(rows, "tableBarRowAbove", QT_TR_NOOP("Insert row above"),
                          editor::TableCommand::RowAbove));
  m_addRows.append(action(rows, "tableBarRowBelow", QT_TR_NOOP("Insert row below"),
                          editor::TableCommand::RowBelow));
  rows->addSeparator();
  move(rows, "tableBarMoveRowsUp", QT_TR_NOOP("Move rows up"), editor::TableCommand::MoveRowsUp);
  move(rows, "tableBarMoveRowsDown", QT_TR_NOOP("Move rows down"),
       editor::TableCommand::MoveRowsDown);
  rows->addSeparator();
  m_deleteRow = action(rows, "tableBarDeleteRow", QT_TR_NOOP("Delete selected rows"),
                       editor::TableCommand::DeleteRow);
  auto* columns = menuButton("tableColumnsMenu", QT_TR_NOOP("Columns"));
  m_addColumns.append(action(columns, "tableBarColumnBefore", QT_TR_NOOP("Insert column left"),
                             editor::TableCommand::ColumnBefore));
  m_addColumns.append(action(columns, "tableBarColumnAfter", QT_TR_NOOP("Insert column right"),
                             editor::TableCommand::ColumnAfter));
  columns->addSeparator();
  move(columns, "tableBarMoveColumnsLeft", QT_TR_NOOP("Move columns left"),
       editor::TableCommand::MoveColumnsLeft);
  move(columns, "tableBarMoveColumnsRight", QT_TR_NOOP("Move columns right"),
       editor::TableCommand::MoveColumnsRight);
  columns->addSeparator();
  m_deleteColumn = action(columns, "tableBarDeleteColumn", QT_TR_NOOP("Delete selected columns"),
                          editor::TableCommand::DeleteColumn);
  auto* align = menuButton("tableAlignmentMenu", QT_TR_NOOP("Align"));
  m_alignments.append(action(align, "tableBarAlignLeft", QT_TR_NOOP("Align columns left"),
                             editor::TableCommand::AlignLeft));
  m_alignments.append(action(align, "tableBarAlignCenter", QT_TR_NOOP("Align columns center"),
                             editor::TableCommand::AlignCenter));
  m_alignments.append(action(align, "tableBarAlignRight", QT_TR_NOOP("Align columns right"),
                             editor::TableCommand::AlignRight));
  for (auto* item : m_alignments) item->setCheckable(true);
  auto* copy = menuButton("tableCopyMenu", QT_TR_NOOP("Copy"));
  for (const bool spreadsheet : {false, true}) {
    auto* item = copy->addAction(QString());
    item->setObjectName(spreadsheet ? "tableBarCopySpreadsheet" : "tableBarCopyMarkdown");
    item->setProperty("tableTranslationSource", spreadsheet ? QT_TR_NOOP("Copy for spreadsheet")
                                                            : QT_TR_NOOP("Copy as Markdown"));
    m_actions.append(item);
    connect(item, &QAction::triggered, this,
            [this, spreadsheet] { emit copyRequested(spreadsheet); });
  }
  auto* exit = new QPushButton(this);
  exit->setObjectName(QStringLiteral("exitTable"));
  exit->setFocusPolicy(Qt::NoFocus);
  layout->addWidget(exit);
  connect(exit, &QPushButton::clicked, this, &TableBar::exitRequested);
  retranslate();
  setDark(false);
  hide();
}
void TableBar::setTableState(const QTextCursor& cursor, const editor::TableCells& selection,
                             int alignment) {
  const auto* table = cursor.currentTable();
  const int rows = table->rows(), columns = table->columns();
  m_dimensions->setText(QStringLiteral("%1 × %2").arg(rows).arg(columns));
  m_dimensions->setAccessibleName(
      tr("Resize table (%1 rows and %2 columns)").arg(rows).arg(columns));
  m_deleteRow->setEnabled(selection.row > 0 && rows - selection.rows >= 2);
  m_deleteColumn->setEnabled(columns - selection.columns >= 1);
  for (auto* item : m_addRows) item->setEnabled(rows < editor::kMaxTableRows);
  for (auto* item : m_addColumns) item->setEnabled(columns < editor::kMaxTableColumns);
  for (const auto& [action, command] : m_moves)
    action->setEnabled(editor::canMoveTable(cursor, command));
  for (int index = 0; index < m_alignments.size(); ++index)
    m_alignments[index]->setChecked(alignment == index + 1);
  setToolTip(
      tr("Current cell: row %1, column %2").arg(selection.row + 1).arg(selection.column + 1));
}
void TableBar::setDark(bool dark) {
  setStyleSheet(
      QStringLiteral(
          "QFrame#tableBar{background:%1;border:1px solid %2;border-radius:5px;}"
          "QToolButton,QPushButton{background:transparent;color:%3;border:0;border-radius:3px;"
          "padding:3px 7px;font-size:9pt;}"
          "QToolButton:hover,QToolButton:pressed,QPushButton:hover{background:%4;}"
          "QToolButton::menu-indicator{width:8px;}"
          "QMenu{background:%1;color:%3;border:1px solid %2;padding:4px;}"
          "QMenu::item{padding:6px 24px 6px 10px;}QMenu::item:selected{background:%4;}"
          "QMenu::item:disabled{color:%5;}")
          .arg(dark ? "#282e36" : "#ffffff", dark ? "#4d5968" : "#dce1e8",
               dark ? "#d8e0eb" : "#536175", dark ? "#3b4d65" : "#e8f1fc",
               dark ? "#778493" : "#a1a8b2"));
}
void TableBar::retranslate() {
  m_dimensions->setToolTip(tr("Resize table"));
  for (auto* action : m_actions)
    action->setText(tr(qPrintable(action->property("tableTranslationSource").toString())));
  for (auto* button : findChildren<QToolButton*>())
    button->setText(tr(qPrintable(button->property("tableTranslationSource").toString())));
  if (auto* exit = findChild<QPushButton*>(QStringLiteral("exitTable"))) {
    exit->setText(tr("Exit table"));
    exit->setToolTip(tr("Continue in a normal paragraph · Ctrl+Enter"));
  }
}

QSize TableBar::chooseSize(QWidget* parent, QSize current) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("resizeTableDialog"));
  dialog.setWindowTitle(tr("Resize Table"));
  dialog.setPalette(parent->palette());
  QFormLayout layout(&dialog);
  QSpinBox rows, columns;
  rows.setObjectName(QStringLiteral("tableResizeRows"));
  rows.setRange(2, qMax(current.height(), editor::kMaxTableRows));
  rows.setValue(current.height());
  columns.setObjectName(QStringLiteral("tableResizeColumns"));
  columns.setRange(1, qMax(current.width(), editor::kMaxTableColumns));
  columns.setValue(current.width());
  layout.addRow(tr("Rows (including header)"), &rows);
  layout.addRow(tr("Columns"), &columns);

  QTableWidget grid(10, 6);
  grid.setObjectName(QStringLiteral("tableResizeGrid"));
  grid.setAccessibleName(tr("Table size grid"));
  grid.setSelectionMode(QAbstractItemView::NoSelection);
  grid.setEditTriggers(QAbstractItemView::NoEditTriggers);
  grid.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  grid.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  grid.horizontalHeader()->hide();
  grid.verticalHeader()->hide();
  grid.horizontalHeader()->setDefaultSectionSize(36);
  grid.verticalHeader()->setDefaultSectionSize(18);
  grid.setFixedSize(grid.horizontalHeader()->length() + 2 * grid.frameWidth(),
                    grid.verticalHeader()->length() + 2 * grid.frameWidth());
  for (int row = 0; row < grid.rowCount(); ++row)
    for (int column = 0; column < grid.columnCount(); ++column) {
      auto* cell = new QTableWidgetItem;
      const auto label = tr("%1 rows and %2 columns").arg(qMax(2, row + 1)).arg(column + 1);
      cell->setData(Qt::AccessibleTextRole, label);
      cell->setToolTip(label);
      grid.setItem(row, column, cell);
    }
  layout.addRow(&grid);
  const auto pick = [&](int row, int column) {
    rows.setValue(row + 1);
    columns.setValue(column + 1);
  };
  connect(&grid, &QTableWidget::cellClicked, &dialog, pick);
  connect(&grid, &QTableWidget::cellActivated, &dialog, pick);

  QLabel warning;
  warning.setObjectName(QStringLiteral("tableResizeWarning"));
  warning.setWordWrap(true);
  layout.addRow(&warning);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  auto* apply = buttons.button(QDialogButtonBox::Ok);
  apply->setText(tr("Resize"));
  buttons.button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  layout.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  const auto refresh = [&] {
    for (int row = 0; row < grid.rowCount(); ++row)
      for (int column = 0; column < grid.columnCount(); ++column)
        grid.item(row, column)
            ->setBackground(row < rows.value() && column < columns.value()
                                ? grid.palette().highlight()
                                : QBrush());
    const bool fewerRows = rows.value() < current.height();
    const bool fewerColumns = columns.value() < current.width();
    warning.setText(fewerRows && fewerColumns
                        ? tr("Bottom rows and rightmost columns will be removed.")
                    : fewerRows    ? tr("Bottom rows will be removed.")
                    : fewerColumns ? tr("Rightmost columns will be removed.")
                                   : QString());
    warning.setVisible(fewerRows || fewerColumns);
    apply->setEnabled(QSize(columns.value(), rows.value()) != current);
  };
  connect(&rows, &QSpinBox::valueChanged, &dialog, refresh);
  connect(&columns, &QSpinBox::valueChanged, &dialog, refresh);
  refresh();
  rows.selectAll();
  return dialog.exec() == QDialog::Accepted ? QSize(columns.value(), rows.value()) : QSize();
}

void TableBar::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange && m_dimensions) retranslate();
  QFrame::changeEvent(event);
}
}  // namespace typist::ui
