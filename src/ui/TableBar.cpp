#include "TableBar.h"

#include "TableEditing.h"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

namespace typist::ui {
TableBar::TableBar(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("tableBar"));
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(6, 1, 4, 1);
  layout->setSpacing(3);
  m_dimensions = new QLabel(this);
  m_dimensions->setObjectName(QStringLiteral("tableDimensions"));
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
  auto* rows = menuButton("tableRowsMenu", QT_TR_NOOP("Rows"));
  m_addRows.append(action(rows, "tableBarRowAbove", QT_TR_NOOP("Insert row above"),
                          editor::TableCommand::RowAbove));
  m_addRows.append(action(rows, "tableBarRowBelow", QT_TR_NOOP("Insert row below"),
                          editor::TableCommand::RowBelow));
  rows->addSeparator();
  m_deleteRow = action(rows, "tableBarDeleteRow", QT_TR_NOOP("Delete selected rows"),
                       editor::TableCommand::DeleteRow);
  auto* columns = menuButton("tableColumnsMenu", QT_TR_NOOP("Columns"));
  m_addColumns.append(action(columns, "tableBarColumnBefore", QT_TR_NOOP("Insert column left"),
                             editor::TableCommand::ColumnBefore));
  m_addColumns.append(action(columns, "tableBarColumnAfter", QT_TR_NOOP("Insert column right"),
                             editor::TableCommand::ColumnAfter));
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
void TableBar::setTableState(int rows, int columns, int row, int column, int selectedRows,
                             int selectedColumns, int alignment) {
  m_dimensions->setText(QStringLiteral("%1 × %2").arg(rows).arg(columns));
  m_dimensions->setToolTip(tr("Rows × columns, including the header"));
  m_dimensions->setAccessibleName(tr("%1 rows and %2 columns").arg(rows).arg(columns));
  m_deleteRow->setEnabled(row > 0 && rows - selectedRows >= 2);
  m_deleteColumn->setEnabled(columns - selectedColumns >= 1);
  for (auto* item : m_addRows) item->setEnabled(rows < editor::kMaxTableRows);
  for (auto* item : m_addColumns) item->setEnabled(columns < editor::kMaxTableColumns);
  for (int index = 0; index < m_alignments.size(); ++index)
    m_alignments[index]->setChecked(alignment == index + 1);
  setToolTip(tr("Current cell: row %1, column %2").arg(row + 1).arg(column + 1));
}
void TableBar::setDark(bool dark) {
  setStyleSheet(
      QStringLiteral(
          "QFrame#tableBar{background:%1;border:1px solid %2;border-radius:5px;}"
          "QLabel{background:transparent;color:%3;border:0;padding:3px 7px;font-size:9pt;}"
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
  for (auto* action : m_actions)
    action->setText(tr(qPrintable(action->property("tableTranslationSource").toString())));
  for (auto* button : findChildren<QToolButton*>())
    button->setText(tr(qPrintable(button->property("tableTranslationSource").toString())));
  if (auto* exit = findChild<QPushButton*>(QStringLiteral("exitTable"))) {
    exit->setText(tr("Exit table"));
    exit->setToolTip(tr("Continue in a normal paragraph · Ctrl+Enter"));
  }
}
void TableBar::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange && m_dimensions) retranslate();
  QFrame::changeEvent(event);
}
}  // namespace typist::ui
