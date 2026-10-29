#include "QuickOpenDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace typist::ui {
QuickOpenDialog::QuickOpenDialog(const QString& root, const QStringList& recent, QWidget* parent)
    : QDialog(parent), m_root(QFileInfo(root).absoluteFilePath()) {
  if (parent) setPalette(parent->palette());
  setObjectName(QStringLiteral("quickOpenDialog"));
  resize(640, 520);
  setMinimumSize(420, 340);
  for (const auto& path : recent)
    if (QFileInfo(path).isFile()) m_recent.append(path);
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(24, 22, 24, 22);
  layout->setSpacing(12);
  auto* rootLabel = new QLabel(QDir::toNativeSeparators(m_root), this);
  rootLabel->setTextFormat(Qt::PlainText);
  rootLabel->setWordWrap(true);
  rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(rootLabel);
  m_query = new QLineEdit(this);
  m_query->setObjectName(QStringLiteral("quickOpenQuery"));
  m_query->setClearButtonEnabled(true);
  m_query->setMaxLength(256);
  m_query->installEventFilter(this);
  layout->addWidget(m_query);
  m_list = new QListWidget(this);
  m_list->setObjectName(QStringLiteral("quickOpenResults"));
  m_list->setTextElideMode(Qt::ElideMiddle);
  m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_list->setUniformItemSizes(true);
  m_list->installEventFilter(this);
  layout->addWidget(m_list, 1);
  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("quickOpenStatus"));
  m_status->setWordWrap(true);
  layout->addWidget(m_status);
  m_hint = new QLabel(this);
  layout->addWidget(m_hint);
  auto* buttons = new QDialogButtonBox(this);
  m_open = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
  m_cancel = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
  layout->addWidget(buttons);
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  const auto base = dark ? QStringLiteral("#252a31") : QStringLiteral("#ffffff");
  const auto text = dark ? QStringLiteral("#e5e9f0") : QStringLiteral("#243044");
  const auto border = dark ? QStringLiteral("#414956") : QStringLiteral("#dce2eb");
  setStyleSheet(QStringLiteral("QLineEdit,QListWidget{background:%1;color:%2;border:1px solid %3;"
                               "border-radius:7px;} QLineEdit{padding:10px;font-size:15px;}"
                               "QListWidget::item{padding:10px 12px;border-radius:4px;}"
                               "QListWidget::item:selected{background:#3d72bb;color:white;}"
                               "QPushButton{padding:7px 18px;}")
                    .arg(base, text, border));
  auto* debounce = new QTimer(this);
  debounce->setSingleShot(true);
  debounce->setInterval(40);
  connect(m_query, &QLineEdit::textChanged, debounce, qOverload<>(&QTimer::start));
  connect(debounce, &QTimer::timeout, this, &QuickOpenDialog::updateResults);
  connect(buttons, &QDialogButtonBox::accepted, this, &QuickOpenDialog::openSelected);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_list, &QListWidget::itemDoubleClicked, this, &QuickOpenDialog::openSelected);
  connect(m_list, &QListWidget::currentRowChanged, this,
          [this](int row) { m_open->setEnabled(row >= 0); });
  connect(&m_index, &QThread::finished, this, [this] {
    m_scanning = false;
    m_paths = m_index.result.paths;
    updateResults();
  });
  m_index.root = m_root;
  retranslate();
  m_query->setFocus();
  m_index.start();
}

QuickOpenDialog::~QuickOpenDialog() {
  m_index.cancelled.store(true, std::memory_order_relaxed);
  m_index.wait();
}

QString QuickOpenDialog::selectedPath() const {
  const auto* item = m_list->currentItem();
  return item ? item->data(Qt::UserRole).toString() : QString();
}

void QuickOpenDialog::openSelected() {
  // Resolve the latest text even when Enter precedes the debounce timeout.
  updateResults();
  if (!selectedPath().isEmpty()) accept();
}

void QuickOpenDialog::updateResults() {
  const auto selected = selectedPath();
  m_list->clear();
  const auto matches = platform::quickOpenMatches(m_paths, m_recent, m_root, m_query->text());
  int row = 0;
  for (const auto& path : matches) {
    const auto folder = QFileInfo(QDir(m_root).relativeFilePath(path)).path();
    QString detail =
        QDir::toNativeSeparators(folder == QLatin1String(".") ? QDir(m_root).dirName() : folder);
    if (m_recent.contains(path)) detail = tr("Recent") + QStringLiteral(" · ") + detail;
    // Paths are plain item text, including filenames containing HTML-like characters.
    auto* item = new QListWidgetItem(QFileInfo(path).fileName() + '\n' + detail, m_list);
    item->setData(Qt::UserRole, path);
    item->setToolTip(QDir::toNativeSeparators(path));
    if (path == selected) row = m_list->count() - 1;
  }
  if (m_list->count()) m_list->setCurrentRow(row);
  m_open->setEnabled(m_list->count() > 0);
  QString status;
  if (m_scanning)
    status = tr("Scanning folder… Recent files are available now.");
  else if (!m_index.result.error.isEmpty())
    status = tr("Folder unavailable. Recent files are still available.");
  else if (m_index.result.limited)
    status = tr("Folder limit reached. Open a smaller folder to find more files.");
  else
    status = matches.isEmpty() ? tr("No matching files")
                               : tr("%1 files shown (up to 100)").arg(matches.size());
  m_status->setText(status);
}

bool QuickOpenDialog::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == m_query || watched == m_list) && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
      openSelected();
      return true;
    }
    if (watched == m_query) {
      int step = 0;
      if (key->key() == Qt::Key_Down) step = 1;
      if (key->key() == Qt::Key_Up) step = -1;
      if (key->key() == Qt::Key_PageDown) step = 5;
      if (key->key() == Qt::Key_PageUp) step = -5;
      if (step) {
        // Refresh for keys pressed before the debounce timer fires.
        updateResults();
        if (m_list->count())
          m_list->setCurrentRow(qBound(0, m_list->currentRow() + step, m_list->count() - 1));
        return true;
      }
    }
  }
  return QDialog::eventFilter(watched, event);
}

void QuickOpenDialog::retranslate() {
  setWindowTitle(tr("Open Quickly"));
  m_query->setPlaceholderText(tr("Search file name or path…"));
  m_query->setAccessibleName(tr("Search file name or path"));
  m_list->setAccessibleName(tr("Matching files"));
  m_hint->setText(tr("↑ ↓  Select     Enter  Open     Esc  Cancel"));
  m_open->setText(tr("Open"));
  m_cancel->setText(tr("Cancel"));
  updateResults();
}

void QuickOpenDialog::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) retranslate();
  QDialog::changeEvent(event);
}
}  // namespace typist::ui
