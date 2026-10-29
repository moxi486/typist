#include "LinkDialog.h"

#include "LinkEditing.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace typist::ui {
LinkDialog::LinkDialog(const QString& label, const QString& target,
                       const QList<HeadingLink>& headings, const QUrl& base, QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("linkDialog"));
  setMinimumWidth(480);
  auto* form = new QFormLayout(this);
  form->setContentsMargins(24, 22, 24, 22);
  form->setSpacing(14);
  m_labelCaption = new QLabel(this);
  m_targetCaption = new QLabel(this);
  m_label = new QLineEdit(label, this);
  m_label->setObjectName(QStringLiteral("linkLabel"));
  m_target = new QLineEdit(target, this);
  m_target->setObjectName(QStringLiteral("linkDestination"));
  form->addRow(m_labelCaption, m_label);
  auto* targetRow = new QHBoxLayout;
  targetRow->addWidget(m_target, 1);
  m_browse = new QPushButton(this);
  m_browse->setObjectName(QStringLiteral("browseLinkFile"));
  targetRow->addWidget(m_browse);
  form->addRow(m_targetCaption, targetRow);
  m_headings = new QComboBox(this);
  m_headings->setObjectName(QStringLiteral("linkHeading"));
  m_headings->addItem(QString());
  for (const auto& heading : headings) m_headings->addItem(heading.title, '#' + heading.id);
  m_headings->setEnabled(!headings.isEmpty());
  form->addRow(m_headings);
  m_hint = new QLabel(this);
  m_hint->setWordWrap(true);
  form->addRow(m_hint);
  m_buttons = new QDialogButtonBox(this);
  m_apply = m_buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
  m_cancel = m_buttons->addButton(QString(), QDialogButtonBox::RejectRole);
  m_apply->setObjectName(QStringLiteral("applyLink"));
  m_cancel->setObjectName(QStringLiteral("cancelLink"));
  setStyleSheet(QStringLiteral(
      "QPushButton#applyLink{background:#3d72bb;color:white;border:0;border-radius:5px;padding:8px "
      "20px;}"
      "QPushButton#applyLink:disabled{background:palette(mid);color:palette(text);}"
      "QComboBox{background:palette(base);color:palette(text);border:1px solid palette(mid);"
      "border-radius:5px;padding:7px;}QComboBox::drop-down{border:0;width:24px;}"
      "QComboBox QAbstractItemView{background:palette(base);color:palette(text);}"));
  form->addRow(m_buttons);
  connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_target, &QLineEdit::textChanged, this,
          [this] { m_apply->setEnabled(!destination().isEmpty()); });
  connect(m_headings, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index > 0) m_target->setText(m_headings->itemData(index).toString());
  });
  connect(m_browse, &QPushButton::clicked, this, [this, base] {
    const auto path =
        QFileDialog::getOpenFileName(this, tr("Choose Link Target"), base.toLocalFile());
    if (path.isEmpty()) return;
    QUrl target = QUrl::fromLocalFile(path);
    if (base.isLocalFile()) {
      target = QUrl();
      target.setPath(QDir(base.toLocalFile()).relativeFilePath(path));
    }
    m_target->setText(target.toString(QUrl::FullyEncoded));
  });
  retranslate();
  m_apply->setEnabled(!destination().isEmpty());
  m_target->setFocus();
  m_target->selectAll();
}
QString LinkDialog::destination() const { return editor::normalizedLink(m_target->text()); }
QString LinkDialog::label() const {
  return m_label->text().isEmpty() ? m_target->text().trimmed() : m_label->text();
}
void LinkDialog::retranslate() {
  setWindowTitle(tr("Edit Link"));
  m_labelCaption->setText(tr("Text to display"));
  m_targetCaption->setText(tr("Link destination"));
  m_target->setPlaceholderText(tr("https://, relative file path, or #heading"));
  m_browse->setText(tr("Browse..."));
  m_headings->setItemText(0, tr("Link to a heading..."));
  m_hint->setText(
      tr("Ctrl/Cmd+click follows a link. Relative paths use the current document folder."));
  m_apply->setText(tr("OK"));
  m_cancel->setText(tr("Cancel"));
}
void LinkDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslate();
  QDialog::changeEvent(event);
}
}  // namespace typist::ui
