#include "CodeBlockBar.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QTimer>

namespace typist::ui {
namespace {
class LanguageBox : public QComboBox {
 public:
  explicit LanguageBox(QWidget* parent) : QComboBox(parent) {}

 protected:
  void paintEvent(QPaintEvent* event) override {
    QComboBox::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(property("dark").toBool() ? QColor("#d8e0eb") : QColor("#536175"), 1.3));
    const int x = width() - 11, y = height() / 2;
    painter.drawLine(x - 3, y - 1, x, y + 2);
    painter.drawLine(x, y + 2, x + 3, y - 1);
  }
};
}  // namespace
CodeBlockBar::CodeBlockBar(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("codeBlockBar"));
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(4, 1, 4, 1);
  layout->setSpacing(4);
  m_language = new LanguageBox(this);
  m_language->setObjectName(QStringLiteral("codeBlockLanguage"));
  m_language->setEditable(true);
  m_language->setCompleter(nullptr);
  m_language->setInsertPolicy(QComboBox::NoInsert);
  m_language->setFixedWidth(128);
  m_language->setMaxVisibleItems(12);
  m_language->addItem(QString(), QString());
  for (const auto& id :
       QStringLiteral(
           "cpp c python javascript typescript json bash powershell java cs go rust swift kotlin "
           "sql html css yaml toml xml markdown latex mermaid")
           .split(' '))
    m_language->addItem(id, id);
  m_language->lineEdit()->setMaxLength(64);
  m_language->lineEdit()->setValidator(new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral("[A-Za-z0-9_+.#-]*")), m_language));
  m_language->lineEdit()->installEventFilter(this);
  m_language->installEventFilter(this);
  layout->addWidget(m_language);
  m_copy = new QPushButton(this);
  m_copy->setObjectName(QStringLiteral("copyCodeBlock"));
  m_copy->setFocusPolicy(Qt::NoFocus);
  layout->addWidget(m_copy);
  m_exit = new QPushButton(this);
  m_exit->setObjectName(QStringLiteral("exitCodeBlock"));
  m_exit->setFocusPolicy(Qt::NoFocus);
  layout->addWidget(m_exit);
  connect(m_language, &QComboBox::activated, this,
          [this](int index) { emit languageRequested(m_language->itemData(index).toString()); });
  connect(m_copy, &QPushButton::clicked, this, &CodeBlockBar::copyRequested);
  connect(m_exit, &QPushButton::clicked, this, &CodeBlockBar::exitRequested);
  retranslate();
  setDark(false);
  hide();
}
void CodeBlockBar::setLanguage(const QString& language) {
  if (m_current != language) {
    m_copied = false;
    m_copy->setText(tr("Copy code"));
  }
  m_current = language;
  if (m_language->hasFocus() || m_language->lineEdit()->hasFocus() ||
      m_language->view()->isVisible())
    return;
  const QSignalBlocker blocker(m_language);
  const int index = m_language->findData(language);
  if (index >= 0)
    m_language->setCurrentIndex(index);
  else
    m_language->setEditText(language);
}
void CodeBlockBar::setDark(bool dark) {
  m_language->setProperty("dark", dark);
  setStyleSheet(
      QStringLiteral(
          "QFrame#codeBlockBar{background:%1;border:1px solid %2;border-radius:5px;}"
          "QComboBox,QPushButton{color:%3;background:transparent;border:0;border-radius:3px;"
          "padding:3px 7px;font-size:9pt;}"
          "QComboBox:hover,QPushButton:hover{background:%4;}"
          "QComboBox QLineEdit{background:transparent;color:%3;border:0;padding:0;}"
          "QComboBox QAbstractItemView{background:%1;color:%3;border:1px solid %2;"
          "selection-background-color:%4;selection-color:%3;}"
          "QComboBox::drop-down{border:0;width:16px;}"
          "QComboBox::down-arrow{image:none;width:0;height:0;}")
          .arg(dark ? "#282e36" : "#ffffff", dark ? "#4d5968" : "#dce1e8",
               dark ? "#d8e0eb" : "#536175", dark ? "#3b4d65" : "#e8f1fc"));
}
void CodeBlockBar::showCopied() {
  m_copied = true;
  m_copy->setText(tr("Copied"));
  QTimer::singleShot(1400, this, [this] {
    m_copied = false;
    m_copy->setText(tr("Copy code"));
  });
}
void CodeBlockBar::retranslate() {
  m_language->setItemText(0, tr("Plain text"));
  m_language->setAccessibleName(tr("Code language"));
  m_language->setToolTip(tr("Select a language, or type an identifier and press Enter"));
  m_copy->setText(m_copied ? tr("Copied") : tr("Copy code"));
  m_copy->setToolTip(tr("Copy this code block without Markdown fences"));
  m_exit->setText(tr("Exit code"));
  m_exit->setToolTip(tr("Continue in a normal paragraph · Ctrl+Enter"));
}
void CodeBlockBar::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange && m_language) retranslate();
  QFrame::changeEvent(event);
}
bool CodeBlockBar::eventFilter(QObject* object, QEvent* event) {
  if ((object == m_language || object == m_language->lineEdit()) &&
      (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
    const auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      event->accept();
      if (event->type() == QEvent::KeyPress) {
        const QString input = m_language->currentText().trimmed();
        emit languageRequested(input == tr("Plain text") ? QString() : input);
      }
      return true;
    }
    if (key->key() == Qt::Key_Escape) {
      event->accept();
      if (event->type() == QEvent::KeyPress) {
        m_language->clearFocus();
        m_language->lineEdit()->clearFocus();
        setLanguage(m_current);
        emit returnToEditor();
      }
      return true;
    }
  }
  return QFrame::eventFilter(object, event);
}
}  // namespace typist::ui
