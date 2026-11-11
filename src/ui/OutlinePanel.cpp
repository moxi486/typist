#include "OutlinePanel.h"

#include "Settings.h"
#include "Toc.h"

#include <QActionGroup>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTextBlock>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace typist::ui {

OutlinePanel::OutlinePanel(QTextEdit* editor, QWidget* parent)
    : QWidget(parent),
      m_editor(editor),
      m_filter(new QLineEdit(this)),
      m_tree(new QTreeWidget(this)),
      m_empty(new QLabel(this)),
      m_options(new QToolButton(this)),
      m_flat(new QAction(this)),
      m_hierarchy(new QAction(this)),
      m_reveal(new QAction(this)),
      m_collapsible(platform::outlineCollapsible()) {
  setObjectName(QStringLiteral("outlinePanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(10, 10, 10, 10);
  layout->setSpacing(8);
  auto* controls = new QHBoxLayout;
  m_filter->setObjectName(QStringLiteral("outlineFilter"));
  m_filter->setClearButtonEnabled(true);
  m_filter->installEventFilter(this);
  controls->addWidget(m_filter, 1);
  m_options->setObjectName(QStringLiteral("outlineOptions"));
  m_options->setText(QStringLiteral("⋯"));
  m_options->setPopupMode(QToolButton::InstantPopup);
  auto* menu = new QMenu(m_options);
  m_options->setMenu(menu);
  controls->addWidget(m_options);
  layout->addLayout(controls);

  m_flat->setObjectName(QStringLiteral("outlineFlat"));
  m_hierarchy->setObjectName(QStringLiteral("outlineCollapsible"));
  m_reveal->setObjectName(QStringLiteral("outlineReveal"));
  auto* modes = new QActionGroup(this);
  for (QAction* action : {m_flat, m_hierarchy}) {
    action->setCheckable(true);
    modes->addAction(action);
    menu->addAction(action);
  }
  (m_collapsible ? m_hierarchy : m_flat)->setChecked(true);
  menu->addSeparator();
  menu->addAction(m_reveal);
  connect(m_flat, &QAction::triggered, this, [this] { setCollapsible(false); });
  connect(m_hierarchy, &QAction::triggered, this, [this] { setCollapsible(true); });
  connect(m_reveal, &QAction::triggered, this, &OutlinePanel::revealCurrent);
  connect(menu, &QMenu::aboutToShow, this,
          [this] { m_reveal->setEnabled(m_document && m_active >= 0); });

  m_tree->setObjectName(QStringLiteral("outlineHeadings"));
  m_tree->setHeaderHidden(true);
  m_tree->setUniformRowHeights(true);
  m_tree->setIndentation(14);
  m_tree->setExpandsOnDoubleClick(false);
  m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
  m_tree->installEventFilter(this);
  connect(
      m_tree, &QTreeWidget::customContextMenuRequested, this,
      [this, menu](const QPoint& point) { menu->popup(m_tree->viewport()->mapToGlobal(point)); });
  connect(m_tree, &QTreeWidget::itemClicked, this, &OutlinePanel::activate);
  connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
    if (m_filter->text().trimmed().isEmpty())
      m_headings[item->data(0, Qt::UserRole).toInt()].collapsed = false;
    markActive(false);
  });
  connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
    if (m_filter->text().trimmed().isEmpty())
      m_headings[item->data(0, Qt::UserRole).toInt()].collapsed = true;
    markActive(false);
  });
  layout->addWidget(m_tree, 1);
  m_empty->setObjectName(QStringLiteral("outlineEmpty"));
  m_empty->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
  m_empty->setWordWrap(true);
  layout->addWidget(m_empty, 1);

  m_refresh.setSingleShot(true);
  m_refresh.setInterval(150);
  connect(&m_refresh, &QTimer::timeout, this, [this] { refresh(); });
  connect(m_filter, &QLineEdit::textChanged, this, &OutlinePanel::renderTree);
  connect(m_editor, &QTextEdit::cursorPositionChanged, this, [this] { followEditor(false); });
  connect(m_editor->verticalScrollBar(), &QScrollBar::valueChanged, this,
          [this] { followEditor(true); });
  retranslate();
}

void OutlinePanel::setDocument(QTextDocument* document, bool reset) {
  if (!reset && document == m_document) return;
  m_refresh.stop();
  if (m_document) disconnect(m_document, nullptr, this, nullptr);
  m_document = document;
  if (reset) {
    m_headings.clear();
    const QSignalBlocker blocker(m_filter);
    m_filter->clear();
  }
  m_active = -1;
  m_firstMatch = -1;
  m_followScroll = false;
  if (m_document) {
    connect(m_document, &QTextDocument::contentsChanged, this, [this] {
      if (isVisible()) m_refresh.start();
    });
  }
  m_filter->setEnabled(m_document != nullptr);
  if (isVisible()) refresh(true);
}

void OutlinePanel::refresh(bool rebuild) {
  m_refresh.stop();
  if (!isVisible()) return;
  if (!m_document) {
    renderTree();
    return;
  }
  const auto entries = outlineFromDocument(m_document, 0);
  const bool same = entries.size() == m_headings.size() &&
                    std::equal(entries.cbegin(), entries.cend(), m_headings.cbegin(),
                               [](const TocEntry& entry, const Heading& heading) {
                                 return entry.level == heading.level &&
                                        entry.text == heading.text &&
                                        entry.position == heading.cursor.block().position();
                               });
  if (!same) {
    // Live cursors keep collapsed branches attached to their headings when text shifts.
    QSet<int> collapsed;
    for (const Heading& heading : m_headings) {
      const auto block = heading.cursor.block();
      if (heading.collapsed && block.isValid() &&
          block.blockFormat().headingLevel() == heading.level)
        collapsed.insert(block.position());
    }
    m_headings.clear();
    m_headings.reserve(entries.size());
    for (const auto& entry : entries) {
      int parent = static_cast<int>(m_headings.size()) - 1;
      while (parent >= 0 && m_headings[parent].level >= entry.level)
        parent = m_headings[parent].parent;
      QTextCursor cursor(m_document);
      cursor.setPosition(entry.position);
      m_headings.append(
          {cursor, entry.text, entry.level, parent, collapsed.contains(entry.position)});
    }
  }
  if (rebuild || !same) {
    m_active = -1;
    renderTree();
  }
  followEditor(m_followScroll);
}

void OutlinePanel::renderTree() {
  const QSignalBlocker blocker(m_tree);
  m_tree->setUpdatesEnabled(false);
  m_marked = nullptr;
  m_tree->clear();
  m_tree->setRootIsDecorated(m_collapsible);
  m_firstMatch = -1;
  const QString query = m_filter->text().trimmed();
  const bool filtering = !query.isEmpty();
  const auto count = static_cast<int>(m_headings.size());
  QVector<bool> visible(count, !filtering);
  if (m_document && filtering) {
    for (int i = count - 1; i >= 0; --i) {
      const auto& heading = m_headings[i];
      if (heading.text.contains(query, Qt::CaseInsensitive)) {
        visible[i] = true;
        m_firstMatch = i;
      }
      if (m_collapsible && visible[i] && heading.parent >= 0) visible[heading.parent] = true;
    }
  }
  for (int i = 0; i < count; ++i) {
    auto& heading = m_headings[i];
    heading.item = nullptr;
    if (!m_document || !visible[i]) continue;
    auto* parent = m_collapsible && heading.parent >= 0 ? m_headings[heading.parent].item : nullptr;
    heading.item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    const QString indent = m_collapsible ? QString() : QString(heading.level - 1, QChar(0x2003));
    heading.item->setText(0, indent + heading.text);
    heading.item->setToolTip(0, QStringLiteral("<qt>%1</qt>").arg(heading.text.toHtmlEscaped()));
    heading.item->setData(0, Qt::UserRole, i);
  }
  for (const auto& heading : m_headings)
    if (heading.item && heading.item->childCount() > 0)
      heading.item->setExpanded(filtering || !heading.collapsed);
  m_tree->setUpdatesEnabled(true);
  const bool empty = m_tree->topLevelItemCount() == 0;
  m_tree->setVisible(!empty);
  m_empty->setVisible(empty);
  retranslate();
  markActive(false);
}

void OutlinePanel::followEditor(bool scrolling) {
  if (!m_document || !isVisible() || m_editor->document() != m_document) return;
  m_followScroll = scrolling;
  const int position = scrolling ? m_editor->cursorForPosition(QPoint(1, 1)).position()
                                 : m_editor->textCursor().position();
  const auto next = std::upper_bound(
      m_headings.cbegin(), m_headings.cend(), position,
      [](int pos, const Heading& heading) { return pos < heading.cursor.block().position(); });
  const int active = static_cast<int>(next - m_headings.cbegin()) - 1;
  if (m_active == active) return;
  m_active = active;
  markActive(!m_tree->hasFocus() && !m_filter->hasFocus());
}

void OutlinePanel::markActive(bool scroll) {
  QTreeWidgetItem* item = nullptr;
  for (int index = m_active; index >= 0 && !item; index = m_headings[index].parent)
    item = m_headings[index].item;
  if (item) {
    for (auto* parent = item->parent(); parent; parent = parent->parent())
      if (!parent->isExpanded()) item = parent;
  }
  if (m_marked == item) return;
  if (m_marked) {
    m_marked->setFont(0, QFont());
    m_marked->setData(0, Qt::AccessibleDescriptionRole, QVariant());
  }
  m_marked = item;
  if (m_marked) {
    QFont font = m_tree->font();
    font.setBold(true);
    m_marked->setFont(0, font);
    m_marked->setData(0, Qt::AccessibleDescriptionRole, tr("Current section"));
    if (scroll) m_tree->scrollToItem(m_marked);
  }
}

void OutlinePanel::revealCurrent() {
  if (!m_document || m_active < 0) return;
  if (!m_headings[m_active].item) m_filter->clear();
  auto* item = m_headings[m_active].item;
  if (!item) return;
  for (auto* parent = item->parent(); parent; parent = parent->parent()) parent->setExpanded(true);
  markActive(false);
  m_tree->setCurrentItem(item);
  m_tree->scrollToItem(item);
}

void OutlinePanel::activate(QTreeWidgetItem* item) {
  if (!m_document || !item || m_editor->document() != m_document) return;
  const auto& heading = m_headings[item->data(0, Qt::UserRole).toInt()];
  const auto block = heading.cursor.block();
  if (!block.isValid() || block.blockFormat().headingLevel() != heading.level) return;
  m_editor->setTextCursor(QTextCursor(block));
  m_editor->ensureCursorVisible();
  followEditor(false);
}

void OutlinePanel::setCollapsible(bool enabled) {
  if (m_collapsible == enabled) return;
  m_collapsible = enabled;
  platform::setOutlineCollapsible(enabled);
  renderTree();
}

void OutlinePanel::retranslate() {
  m_filter->setPlaceholderText(tr("Filter headings…"));
  m_filter->setAccessibleName(tr("Filter headings"));
  m_tree->setAccessibleName(tr("Document headings"));
  m_options->setAccessibleName(tr("Outline Options"));
  m_options->setToolTip(tr("Outline Options"));
  m_flat->setText(tr("Flat Outline"));
  m_hierarchy->setText(tr("Collapsible Outline"));
  m_reveal->setText(tr("Highlight Current Heading"));
  m_empty->setText(!m_document          ? tr("Outline is available in rendered view")
                   : m_headings.empty() ? tr("No headings")
                                        : tr("No matching headings"));
  if (m_marked) m_marked->setData(0, Qt::AccessibleDescriptionRole, tr("Current section"));
}

void OutlinePanel::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslate();
  QWidget::changeEvent(event);
}

void OutlinePanel::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  refresh(true);
}

bool OutlinePanel::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    const int key = static_cast<QKeyEvent*>(event)->key();
    if (watched == m_filter && m_firstMatch >= 0) {
      auto* first = m_headings[m_firstMatch].item;
      if (key == Qt::Key_Down) {
        m_tree->setCurrentItem(first);
        m_tree->setFocus();
        return true;
      }
      if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        activate(first);
        return true;
      }
    }
    if (watched == m_tree && (key == Qt::Key_Return || key == Qt::Key_Enter)) {
      activate(m_tree->currentItem());
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace typist::ui
