#include "FindBar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace typist::ui {

FindBar::FindBar(QWidget* parent) : QWidget(parent) {
  m_edit = new QLineEdit(this);
  m_edit->setPlaceholderText(QStringLiteral("Find in document"));
  m_edit->setClearButtonEnabled(true);
  m_case = new QCheckBox(QStringLiteral("Match case"), this);
  m_prev = new QPushButton(QStringLiteral("Prev"), this);
  m_next = new QPushButton(QStringLiteral("Next"), this);
  m_count = new QLabel(QStringLiteral("0 matches"), this);

  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(8, 4, 8, 4);
  layout->addWidget(m_edit);
  layout->addWidget(m_case);
  layout->addWidget(m_prev);
  layout->addWidget(m_next);
  layout->addWidget(m_count);

  connect(m_edit, &QLineEdit::textChanged, this, &FindBar::patternChanged);
  connect(m_edit, &QLineEdit::returnPressed, this, &FindBar::findNext);
  connect(m_case, &QCheckBox::toggled, this, &FindBar::patternChanged);
  connect(m_next, &QPushButton::clicked, this, &FindBar::findNext);
  connect(m_prev, &QPushButton::clicked, this, &FindBar::findPrevious);
  m_edit->installEventFilter(this);
  setVisible(false);
}

QString FindBar::pattern() const { return m_edit->text(); }

bool FindBar::caseSensitive() const { return m_case->isChecked(); }

void FindBar::setMatchCount(int count) {
  m_count->setText(count == 0 ? QStringLiteral("No matches") : QStringLiteral("%1 matches").arg(count));
}

void FindBar::setPattern(const QString& pattern) {
  m_edit->setText(pattern);
}

void FindBar::openFocused() {
  setVisible(true);
  m_edit->setFocus();
  m_edit->selectAll();
}

void FindBar::closeBar() {
  setVisible(false);
}

bool FindBar::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_edit && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Escape) {
      closeBar();
      return true;
    }
    if (key->key() == Qt::Key_Return && (key->modifiers() & Qt::ShiftModifier)) {
      emit findPrevious();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace typist::ui
