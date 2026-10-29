#include "FindBar.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace typist::ui {

FindBar::FindBar(QWidget* parent) : QWidget(parent) {
  m_edit = new QLineEdit(this);
  m_edit->setPlaceholderText(QStringLiteral("Find in document"));
  m_edit->setAccessibleName(QStringLiteral("Find pattern"));
  m_edit->setClearButtonEnabled(true);
  m_case = new QCheckBox(QStringLiteral("Match case"), this);
  m_case->setAccessibleName(QStringLiteral("Match case"));
  m_regex = new QCheckBox(QStringLiteral("Regex"), this);
  m_regex->setAccessibleName(QStringLiteral("Regular expression search"));
  m_prev = new QPushButton(QStringLiteral("Prev"), this);
  m_next = new QPushButton(QStringLiteral("Next"), this);
  m_count = new QLabel(QStringLiteral("0 matches"), this);

  m_edit->setObjectName(QStringLiteral("findPattern"));
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  auto* findRow = new QWidget(this);
  auto* layout = new QHBoxLayout(findRow);
  layout->setContentsMargins(8, 4, 8, 4);
  layout->addWidget(m_edit);
  layout->addWidget(m_case);
  layout->addWidget(m_regex);
  layout->addWidget(m_prev);
  layout->addWidget(m_next);
  layout->addWidget(m_count);
  outer->addWidget(findRow);
  m_replaceRow = new QWidget(this);
  auto* replaceLayout = new QHBoxLayout(m_replaceRow);
  replaceLayout->setContentsMargins(8, 4, 8, 8);
  m_replacement = new QLineEdit(this);
  m_replacement->setObjectName(QStringLiteral("replaceText"));
  m_replaceOne = new QPushButton(this);
  m_replaceOne->setObjectName(QStringLiteral("replaceOne"));
  m_replaceAll = new QPushButton(this);
  m_replaceAll->setObjectName(QStringLiteral("replaceAll"));
  replaceLayout->addWidget(m_replacement);
  replaceLayout->addWidget(m_replaceOne);
  replaceLayout->addWidget(m_replaceAll);
  outer->addWidget(m_replaceRow);
  m_replaceRow->hide();
  connect(m_replaceOne, &QPushButton::clicked, this, &FindBar::replaceOne);
  connect(m_replaceAll, &QPushButton::clicked, this, &FindBar::replaceAll);
  connect(m_replacement, &QLineEdit::returnPressed, this, &FindBar::replaceOne);
  m_replacement->installEventFilter(this);

  connect(m_edit, &QLineEdit::textChanged, this, &FindBar::patternChanged);
  connect(m_edit, &QLineEdit::returnPressed, this, &FindBar::findNext);
  connect(m_case, &QCheckBox::toggled, this, &FindBar::patternChanged);
  connect(m_regex, &QCheckBox::toggled, this, &FindBar::patternChanged);
  connect(m_next, &QPushButton::clicked, this, &FindBar::findNext);
  connect(m_prev, &QPushButton::clicked, this, &FindBar::findPrevious);
  m_edit->installEventFilter(this);
  retranslateUi();
  setVisible(false);
}

QString FindBar::pattern() const { return m_edit->text(); }

bool FindBar::caseSensitive() const { return m_case->isChecked(); }

bool FindBar::regex() const { return m_regex->isChecked(); }

void FindBar::setMatchCount(int count) {
  m_matchCount = count;
  m_count->setText(count == 0 ? tr("No matches") : tr("%1 matches").arg(count));
}

void FindBar::retranslateUi() {
  m_edit->setPlaceholderText(tr("Find in document"));
  m_edit->setAccessibleName(tr("Find pattern"));
  m_case->setText(tr("Match case"));
  m_case->setAccessibleName(tr("Match case"));
  m_regex->setText(tr("Regex"));
  m_regex->setAccessibleName(tr("Regular expression search"));
  m_prev->setText(tr("Prev"));
  m_next->setText(tr("Next"));
  m_replacement->setPlaceholderText(tr("Replace with"));
  m_replacement->setAccessibleName(tr("Replace with"));
  m_replaceOne->setText(tr("Replace"));
  m_replaceAll->setText(tr("Replace All"));
  setMatchCount(m_matchCount);
}

void FindBar::setPattern(const QString& pattern) { m_edit->setText(pattern); }

void FindBar::openFocused() {
  setVisible(true);
  m_edit->setFocus();
  m_edit->selectAll();
}

QString FindBar::replacement() const { return m_replacement->text(); }

void FindBar::openReplace() {
  m_replaceRow->show();
  openFocused();
}

void FindBar::closeBar() {
  setVisible(false);
  m_replaceRow->hide();
  m_edit->clear();
}

bool FindBar::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == m_edit || watched == m_replacement) && event->type() == QEvent::KeyPress) {
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
