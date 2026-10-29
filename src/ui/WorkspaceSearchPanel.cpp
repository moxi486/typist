#include "WorkspaceSearchPanel.h"

#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace typist::ui {
WorkspaceSearchPanel::WorkspaceSearchPanel(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("workspaceSearchPanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 12, 12, 12);
  layout->setSpacing(10);
  m_scope = new QLabel(this);
  m_scope->setObjectName(QStringLiteral("workspaceSearchScope"));
  m_scope->setTextFormat(Qt::PlainText);
  m_scope->setWordWrap(true);
  layout->addWidget(m_scope);
  m_query = new QLineEdit(this);
  m_query->setObjectName(QStringLiteral("workspaceSearchQuery"));
  m_query->setMaxLength(512);
  m_query->setClearButtonEnabled(true);
  m_query->installEventFilter(this);
  layout->addWidget(m_query);
  m_case = new QCheckBox(this);
  m_case->setObjectName(QStringLiteral("workspaceSearchCase"));
  layout->addWidget(m_case);
  auto* buttons = new QHBoxLayout;
  m_search = new QPushButton(this);
  m_search->setObjectName(QStringLiteral("workspaceSearchStart"));
  m_cancel = new QPushButton(this);
  m_cancel->setObjectName(QStringLiteral("workspaceSearchCancel"));
  buttons->addWidget(m_search);
  buttons->addWidget(m_cancel);
  layout->addLayout(buttons);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("workspaceSearchStatus"));
  m_status->setWordWrap(true);
  layout->addWidget(m_status);
  m_results = new QTreeWidget(this);
  m_results->setObjectName(QStringLiteral("workspaceSearchResults"));
  m_results->setHeaderHidden(true);
  m_results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_results->setTextElideMode(Qt::ElideMiddle);
  m_results->setUniformRowHeights(true);
  m_results->setIndentation(12);
  layout->addWidget(m_results, 1);
  m_hint = new QLabel(this);
  m_hint->setWordWrap(true);
  layout->addWidget(m_hint);
  m_debounce = new QTimer(this);
  m_debounce->setSingleShot(true);
  m_debounce->setInterval(300);
  connect(m_query, &QLineEdit::textChanged, this, &WorkspaceSearchPanel::invalidate);
  connect(m_case, &QCheckBox::toggled, this, &WorkspaceSearchPanel::invalidate);
  connect(m_query, &QLineEdit::returnPressed, this, &WorkspaceSearchPanel::search);
  connect(m_search, &QPushButton::clicked, this, &WorkspaceSearchPanel::search);
  connect(m_cancel, &QPushButton::clicked, this, [this] {
    m_debounce->stop();
    m_runner.cancel();
  });
  connect(m_debounce, &QTimer::timeout, this, &WorkspaceSearchPanel::search);
  connect(&m_runner, &platform::FolderSearchRunner::resultsReady, this,
          &WorkspaceSearchPanel::showResults);
  connect(m_results, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
    const auto index = item->data(0, Qt::UserRole);
    if (!index.isValid()) {
      item->setExpanded(!item->isExpanded());
      return;
    }
    const int value = index.toInt();
    if (value >= 0 && value < m_hits.size()) emit hitActivated(m_hits.at(value));
  });
  retranslate();
  setBusy(false);
}

void WorkspaceSearchPanel::setRoot(const QString& root) {
  const auto path = root.isEmpty() ? QString() : QDir(root).absolutePath();
  if (path == m_root) return;
  m_root = path;
  stop();
  m_done = false;
  m_hits.clear();
  m_results->clear();
  updateStatus();
}
void WorkspaceSearchPanel::openFocused(const QString& seed) {
  if (!seed.isEmpty() && !seed.contains(QChar::ParagraphSeparator) && !seed.contains('\n'))
    m_query->setText(seed.left(512));
  m_query->setFocus();
  m_query->selectAll();
  if (!m_query->text().isEmpty()) search();
}
void WorkspaceSearchPanel::stop() {
  m_debounce->stop();
  m_runner.discard();
  if (m_busy) m_done = false;
  setBusy(false);
}
void WorkspaceSearchPanel::invalidate() {
  stop();
  m_done = false;
  m_hits.clear();
  m_results->clear();
  updateStatus();
  if (isVisible() && !m_query->text().isEmpty() && !m_root.isEmpty()) m_debounce->start();
}
void WorkspaceSearchPanel::search() {
  m_debounce->stop();
  if (m_query->text().isEmpty() || m_root.isEmpty()) return;
  platform::SearchRequest request;
  request.rootDir = m_root;
  request.pattern = m_query->text();
  request.caseSensitive = m_case->isChecked();
  m_hits.clear();
  m_results->clear();
  m_done = false;
  setBusy(true);
  m_runner.start(request);
}
void WorkspaceSearchPanel::showResults(const QVector<platform::SearchHit>& hits, bool cancelled,
                                       const QString& error) {
  m_hits = hits;
  m_cancelled = cancelled;
  m_error = error;
  m_done = true;
  m_results->clear();
  QMap<QString, QTreeWidgetItem*> groups;
  const QDir root(m_root);
  QStringList paths;
  for (const auto& hit : hits)
    if (!paths.contains(hit.path)) paths.append(hit.path);
  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) {
    auto* group = new QTreeWidgetItem(m_results);
    group->setText(0, QDir::toNativeSeparators(root.relativeFilePath(path)));
    group->setToolTip(0, QDir::toNativeSeparators(path));
    QFont font = group->font(0);
    font.setBold(true);
    group->setFont(0, font);
    groups.insert(path, group);
    group->setExpanded(true);
  }
  for (int i = 0; i < hits.size(); ++i) {
    const auto& hit = hits.at(i);
    auto* item = new QTreeWidgetItem(groups.value(hit.path));
    item->setText(0, QStringLiteral("%1: %2").arg(hit.line).arg(hit.preview));
    item->setToolTip(0, hit.preview);
    item->setData(0, Qt::UserRole, i);
  }
  setBusy(false);
}
void WorkspaceSearchPanel::setBusy(bool busy) {
  m_busy = busy;
  m_cancel->setEnabled(busy);
  m_search->setEnabled(!m_query->text().isEmpty() && !m_root.isEmpty());
  updateStatus();
}
void WorkspaceSearchPanel::updateStatus() {
  m_scope->setText(m_root.isEmpty() ? tr("No workspace selected")
                                    : tr("In %1").arg(QDir(m_root).dirName()));
  m_scope->setToolTip(QDir::toNativeSeparators(m_root));
  if (m_busy)
    m_status->setText(tr("Searching…"));
  else if (!m_done)
    m_status->setText(tr("Search saved Markdown files in this workspace."));
  else if (!m_error.isEmpty())
    m_status->setText(tr("Folder unavailable. Choose another workspace."));
  else {
    QString text =
        tr("Matching lines: %1 · Files: %2").arg(m_hits.size()).arg(m_results->topLevelItemCount());
    if (m_cancelled) text = tr("Search cancelled. ") + text;
    if (m_runner.stats().limited)
      text += '\n' + tr("Search limit reached. Narrow the query or workspace.");
    if (m_runner.stats().skippedFiles)
      text +=
          '\n' +
          tr("%1 large, binary or unreadable files skipped.").arg(m_runner.stats().skippedFiles);
    m_status->setText(text);
  }
}
void WorkspaceSearchPanel::retranslate() {
  m_query->setPlaceholderText(tr("Search in workspace…"));
  m_query->setAccessibleName(tr("Search in workspace"));
  m_case->setText(tr("Match case"));
  m_search->setText(tr("Search"));
  m_cancel->setText(tr("Cancel"));
  m_results->setAccessibleName(tr("Workspace search results"));
  m_hint->setText(
      tr("Enter or double-click a result to open its source line. Searches use saved files."));
  updateStatus();
}
void WorkspaceSearchPanel::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslate();
  QWidget::changeEvent(event);
}
void WorkspaceSearchPanel::hideEvent(QHideEvent* event) {
  stop();
  QWidget::hideEvent(event);
}
bool WorkspaceSearchPanel::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_query && event->type() == QEvent::KeyPress) {
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      stop();
      return true;
    }
    if (key->key() == Qt::Key_Down && m_results->topLevelItemCount()) {
      auto* group = m_results->topLevelItem(0);
      if (group->childCount()) {
        m_results->setCurrentItem(group->child(0));
        m_results->setFocus();
        return true;
      }
    }
  }
  return QWidget::eventFilter(watched, event);
}
}  // namespace typist::ui
