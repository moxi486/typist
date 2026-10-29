#include "ParagraphSourcePanel.h"

#include "SyntaxHighlighter.h"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace typist::ui {
ParagraphSourcePanel::ParagraphSourcePanel(QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("paragraphSourcePanel"));
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(12, 8, 12, 10);
  layout->setSpacing(6);
  auto* header = new QHBoxLayout;
  auto* title = new QLabel(tr("Paragraph Markdown"), this);
  title->setObjectName(QStringLiteral("paragraphSourceTitle"));
  header->addWidget(title);
  header->addStretch();
  auto* apply = new QPushButton(tr("Apply"), this);
  apply->setObjectName(QStringLiteral("applyParagraphSource"));
  auto* cancel = new QPushButton(tr("Cancel"), this);
  cancel->setObjectName(QStringLiteral("cancelParagraphSource"));
  header->addWidget(apply);
  header->addWidget(cancel);
  layout->addLayout(header);
  m_input = new QPlainTextEdit(this);
  m_input->setObjectName(QStringLiteral("paragraphSourceInput"));
  m_input->setAccessibleName(tr("Paragraph Markdown"));
  m_input->setFrameShape(QFrame::NoFrame);
  m_input->setTabStopDistance(32);
  QFont font;
  font.setFamilies({"Consolas", "Menlo", "monospace"});
  font.setPointSizeF(11);
  m_input->setFont(font);
  m_input->setPlaceholderText(tr("Ctrl+Enter to apply · Esc to cancel"));
  m_input->installEventFilter(this);
  layout->addWidget(m_input, 1);
  auto* hint = new QLabel(tr("Ctrl+Enter to apply · Esc to cancel"), this);
  hint->setObjectName(QStringLiteral("paragraphSourceHint"));
  layout->addWidget(hint);
  auto* shadow = new QGraphicsDropShadowEffect(this);
  shadow->setBlurRadius(16);
  shadow->setOffset(0, 3);
  shadow->setColor(QColor(0, 0, 0, 45));
  setGraphicsEffect(shadow);
  connect(m_input, &QPlainTextEdit::textChanged, this, &ParagraphSourcePanel::draftChanged);
  connect(m_input, &QPlainTextEdit::undoAvailable, this, &ParagraphSourcePanel::draftChanged);
  connect(m_input, &QPlainTextEdit::redoAvailable, this, &ParagraphSourcePanel::draftChanged);
  connect(apply, &QPushButton::clicked, this, &ParagraphSourcePanel::applyRequested);
  connect(cancel, &QPushButton::clicked, this, &ParagraphSourcePanel::cancelRequested);
  setDark(false);
}
void ParagraphSourcePanel::setSource(const QString& source) {
  m_input->setPlainText(source);
  m_input->document()->clearUndoRedoStacks();
}
QString ParagraphSourcePanel::source() const { return m_input->toPlainText(); }
void ParagraphSourcePanel::setDark(bool dark) {
  setStyleSheet(
      QStringLiteral(
          "QFrame#paragraphSourcePanel{background:%1;border:1px solid %2;border-radius:7px;}"
          "QPlainTextEdit{background:%1;color:%3;border:0;selection-background-color:%4;}"
          "QLabel{color:%3;border:0;background:transparent;} "
          "QLabel#paragraphSourceHint{color:%5;font-size:10px;}"
          "QPushButton{color:%3;background:transparent;border:0;padding:4px 8px;} "
          "QPushButton:hover{background:%4;border-radius:4px;}")
          .arg(dark ? "#282e36" : "#ffffff", dark ? "#596575" : "#b4c5db",
               dark ? "#dde3ec" : "#303946", dark ? "#3b4d65" : "#e8f1fc",
               dark ? "#9aaac0" : "#7b8494"));
  render::installSyntaxHighlighter(m_input->document(), true, dark);
}
void ParagraphSourcePanel::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange && m_input) {
    if (auto* title = findChild<QLabel*>(QStringLiteral("paragraphSourceTitle")))
      title->setText(tr("Paragraph Markdown"));
    if (auto* hint = findChild<QLabel*>(QStringLiteral("paragraphSourceHint")))
      hint->setText(tr("Ctrl+Enter to apply · Esc to cancel"));
    if (auto* apply = findChild<QPushButton*>(QStringLiteral("applyParagraphSource")))
      apply->setText(tr("Apply"));
    if (auto* cancel = findChild<QPushButton*>(QStringLiteral("cancelParagraphSource")))
      cancel->setText(tr("Cancel"));
    m_input->setAccessibleName(tr("Paragraph Markdown"));
    m_input->setPlaceholderText(tr("Ctrl+Enter to apply · Esc to cancel"));
  }
  QFrame::changeEvent(event);
}
bool ParagraphSourcePanel::eventFilter(QObject* object, QEvent* event) {
  if (object == m_input &&
      (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride)) {
    auto* key = static_cast<QKeyEvent*>(event);
    const bool apply = (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
                       (key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier));
    if (apply || key->key() == Qt::Key_Escape) {
      event->accept();
      if (event->type() == QEvent::KeyPress) {
        if (apply)
          emit applyRequested();
        else
          emit cancelRequested();
      }
      return true;
    }
  }
  if (object == m_input && event->type() == QEvent::FocusOut) {
    const auto reason = static_cast<QFocusEvent*>(event)->reason();
    if (reason != Qt::PopupFocusReason && reason != Qt::MenuBarFocusReason) {
      QTimer::singleShot(0, this, [this] {
        auto* focus = QApplication::focusWidget();
        if (isVisible() && focus && focus != this && !isAncestorOf(focus) &&
            !QApplication::activePopupWidget() && !QApplication::activeModalWidget())
          emit focusLeft();
      });
    }
  }
  return QFrame::eventFilter(object, event);
}
}  // namespace typist::ui
