#include "FolderLocationsMenu.h"

#include <QAction>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFontMetrics>
#include <QStyle>

namespace typist::ui {
FolderLocationsMenu::FolderLocationsMenu(QWidget* parent, const QString& settingsFile)
    : QMenu(parent), m_store(settingsFile) {
  setObjectName(QStringLiteral("folderLocationsMenu"));
  setToolTipsVisible(true);
  menuAction()->setProperty("folderLocationAction", true);
  setTitle(tr("Recent and Pinned Folders"));
  connect(this, &QMenu::aboutToShow, this, &FolderLocationsMenu::refresh);
}
void FolderLocationsMenu::setRoot(const QString& path) { m_root = path; }
void FolderLocationsMenu::recordFolder(const QString& path) { m_store.record(path); }

void FolderLocationsMenu::refresh() {
  clear();
  setTitle(tr("Recent and Pinned Folders"));
  const auto locations = m_store.read();
#ifdef Q_OS_WIN
  constexpr auto pathCase = Qt::CaseInsensitive;
#else
  constexpr auto pathCase = Qt::CaseSensitive;
#endif
  const auto root = !m_root.isEmpty() && QFileInfo(m_root).isDir()
                        ? QFileInfo(m_root).canonicalFilePath()
                        : QString();
  const auto add = [this](const QString& text, const char* name) {
    auto* action = addAction(text);
    action->setObjectName(QString::fromLatin1(name));
    action->setProperty("folderLocationAction", true);
    return action;
  };
  connect(add(tr("Open Folder…"), "browseFolder"), &QAction::triggered, this,
          &FolderLocationsMenu::browseRequested);
  const bool pinned = locations.pinned.contains(root, pathCase);
  auto* pin =
      add(pinned ? tr("Unpin Current Folder") : tr("Pin Current Folder"), "pinCurrentFolder");
  pin->setEnabled(!root.isEmpty());
  connect(pin, &QAction::triggered, this, [this, root, pinned] {
    if (!m_store.setPinned(root, !pinned)) emit pinLimitReached();
  });
  const auto directoryIcon = style()->standardIcon(QStyle::SP_DirIcon);
  const auto addFolder = [this, &add, &directoryIcon](const QString& path, int index) {
    QString name = QDir(path).dirName();
    if (name.isEmpty()) name = QDir::toNativeSeparators(path);
    const auto label = name + QStringLiteral(" — ") + QDir::toNativeSeparators(path);
    auto* action =
        add(fontMetrics().elidedText(label, Qt::ElideMiddle, 560).replace('&', QLatin1String("&&")),
            "folderLocation");
    action->setObjectName(QStringLiteral("folderLocation_%1").arg(index));
    action->setData(path);
    action->setToolTip(QDir::toNativeSeparators(path));
    action->setIcon(directoryIcon);
    connect(action, &QAction::triggered, this, [this, path] { emit folderActivated(path); });
  };
  int index = 0;
  if (!locations.pinned.isEmpty()) {
    add(tr("Pinned Folders"), "pinnedFoldersHeading")->setEnabled(false);
    for (const auto& path : locations.pinned) addFolder(path, index++);
  }
  QStringList recent;
  for (const auto& path : locations.recent)
    if (!locations.pinned.contains(path, pathCase)) recent.append(path);
  if (!recent.isEmpty()) {
    add(tr("Recent Folders"), "recentFoldersHeading")->setEnabled(false);
    for (const auto& path : recent) addFolder(path, index++);
  }
  if (index == 0) add(tr("No recent folders"), "noRecentFolders")->setEnabled(false);
  addSeparator();
  auto* forget = add(tr("Remove Current Folder from Recent"), "forgetCurrentFolder");
  forget->setEnabled(locations.recent.contains(root, pathCase));
  connect(forget, &QAction::triggered, this, [this, root] { m_store.forgetRecent(root); });
  auto* clearRecent = add(tr("Clear Recent Folders (Keep Pinned)"), "clearRecentFolders");
  clearRecent->setEnabled(!locations.recent.isEmpty());
  connect(clearRecent, &QAction::triggered, this, [this] { m_store.clearRecent(); });
}
void FolderLocationsMenu::changeEvent(QEvent* event) {
  if (event->type() == QEvent::LanguageChange) {
    setTitle(tr("Recent and Pinned Folders"));
    if (isVisible()) refresh();
  }
  QMenu::changeEvent(event);
}
}  // namespace typist::ui
