#include "FileOps.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <shlobj.h>
#elif defined(Q_OS_MACOS)
#include <QProcess>
#elif defined(TYPIST_HAS_FILE_MANAGER_DBUS)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QStringList>
#endif

namespace typist::platform {

namespace {

QString withMarkdownSuffix(const QString& name) {
  return name.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive) ? name
                                                                   : name + QStringLiteral(".md");
}

bool validName(const QString& name, QString* error) {
  bool valid = !name.isEmpty() && name != QLatin1String(".") && name != QLatin1String("..") &&
               !name.endsWith('.') && !name.endsWith(' ');
  for (const auto ch : name)
    if (ch.unicode() < 32 || QStringLiteral("/\\:*?\"<>|").contains(ch)) valid = false;
  const auto base = name.section('.', 0, 0).toUpper();
  if (base == QLatin1String("CON") || base == QLatin1String("PRN") ||
      base == QLatin1String("AUX") || base == QLatin1String("NUL") ||
      (base.size() == 4 &&
       (base.startsWith(QLatin1String("COM")) || base.startsWith(QLatin1String("LPT"))) &&
       base.at(3) >= QLatin1Char('1') && base.at(3) <= QLatin1Char('9')))
    valid = false;
  if (!valid && error) *error = QStringLiteral("Invalid name");
  return valid;
}

bool availableDirectory(const QString& dir, QString* error) {
  const bool valid = !dir.isEmpty() && QFileInfo(dir).isDir();
  if (!valid && error) *error = QStringLiteral("Folder unavailable");
  return valid;
}

}  // namespace

bool createMarkdownFile(const QString& dir, const QString& name, QString* error) {
  if (error) error->clear();
  if (!availableDirectory(dir, error) || !validName(name.trimmed(), error)) return false;
  return createEmptyFile(QDir(dir).filePath(withMarkdownSuffix(name.trimmed())), error);
}

bool createEmptyFile(const QString& path, QString* error) {
  if (error) error->clear();
  const QFileInfo info(path);
  if (!availableDirectory(info.absolutePath(), error) || !validName(info.fileName(), error))
    return false;
  if (info.fileName().compare(QLatin1String(".md"), Qt::CaseInsensitive) == 0) {
    if (error) *error = QStringLiteral("File name is empty");
    return false;
  }
  if (info.exists() || info.isSymLink()) {
    if (error) *error = QStringLiteral("A file with that name already exists");
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    if (error) *error = QStringLiteral("Cannot create file");
    return false;
  }
  file.close();
  return true;
}

bool renameFile(const QString& path, const QString& newName, QString* error) {
  if (error) error->clear();
  if (!validName(newName.trimmed(), error)) return false;
  const QString fileName = withMarkdownSuffix(newName.trimmed());
  const QString target = QDir(QFileInfo(path).absolutePath()).filePath(fileName);
  if (target == path) return true;
  if (QFileInfo::exists(target)) {
    if (error) *error = QStringLiteral("A file with that name already exists");
    return false;
  }
  if (!QFile::rename(path, target)) {
    if (error) *error = QStringLiteral("Rename failed");
    return false;
  }
  return true;
}

bool moveMarkdownToTrash(const QString& path, QString* error) {
  if (error) error->clear();
  const QFileInfo file(path);
  if (!file.isFile() || file.isSymLink() ||
      file.suffix().compare(QLatin1String("md"), Qt::CaseInsensitive)) {
    if (error) *error = QStringLiteral("File unavailable");
    return false;
  }
  if (!QFile::moveToTrash(file.absoluteFilePath())) {
    if (error) *error = QStringLiteral("Trash failed");
    return false;
  }
  return true;
}

bool createDirectory(const QString& dir, const QString& name, QString* error) {
  if (error) error->clear();
  const auto trimmed = name.trimmed();
  if (!availableDirectory(dir, error) || !validName(trimmed, error)) return false;
  if (QFileInfo::exists(QDir(dir).filePath(trimmed))) {
    if (error) *error = QStringLiteral("A file with that name already exists");
    return false;
  }
  if (!QDir(dir).mkdir(trimmed)) {
    if (error) *error = QStringLiteral("Cannot create folder");
    return false;
  }
  return true;
}

QString duplicateMarkdownFile(const QString& path, QString* error) {
  if (error) error->clear();
  const QFileInfo source(path);
  if (!source.isFile() || source.isSymLink() ||
      source.suffix().compare(QLatin1String("md"), Qt::CaseInsensitive)) {
    if (error) *error = QStringLiteral("File unavailable");
    return {};
  }
  for (int i = 1; i <= 10000; ++i) {
    const auto name = source.completeBaseName() + QStringLiteral("-copy") +
                      (i == 1 ? QString() : QStringLiteral("-%1").arg(i)) + '.' + source.suffix();
    const auto target = source.absoluteDir().filePath(name);
    if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) continue;
    if (QFile::copy(source.absoluteFilePath(), target)) return target;
    // Another process may have claimed this name after the existence check.
    if (QFileInfo::exists(target) || QFileInfo(target).isSymLink()) continue;
    break;
  }
  if (error) *error = QStringLiteral("Copy failed");
  return {};
}

QUrl containingDirectoryUrl(const QString& path) {
  if (path.isEmpty()) return {};
  const QFileInfo info(path);
  if (!info.exists()) return {};
  return QUrl::fromLocalFile(info.isDir() ? info.absoluteFilePath() : info.absolutePath());
}

void revealFile(const QString& path, QObject* context, std::function<void()> onFailure) {
  const QFileInfo info(path);
  if (!info.isFile()) {
    onFailure();
    return;
  }
  const auto openParent = [dir = QUrl::fromLocalFile(info.absolutePath()), onFailure] {
    if (!QDesktopServices::openUrl(dir)) onFailure();
  };
#if defined(Q_OS_WIN)
  Q_UNUSED(context);
  const auto apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  bool selected = false;
  if (SUCCEEDED(apartment) || apartment == RPC_E_CHANGED_MODE) {
    const auto nativePath = QDir::toNativeSeparators(info.absoluteFilePath()).toStdWString();
    PIDLIST_ABSOLUTE item = nullptr;
    if (SUCCEEDED(SHParseDisplayName(nativePath.c_str(), nullptr, &item, 0, nullptr)) && item)
      selected = SUCCEEDED(SHOpenFolderAndSelectItems(item, 0, nullptr, 0));
    CoTaskMemFree(item);
    if (SUCCEEDED(apartment)) CoUninitialize();
  }
  if (!selected) openParent();
#elif defined(Q_OS_MACOS)
  auto* process = new QProcess(context);
  QObject::connect(process, &QProcess::finished, context,
                   [process, openParent](int code, QProcess::ExitStatus status) {
                     process->deleteLater();
                     if (status != QProcess::NormalExit || code != 0) openParent();
                   });
  QObject::connect(process, &QProcess::errorOccurred, context,
                   [process, openParent](QProcess::ProcessError error) {
                     if (error != QProcess::FailedToStart) return;
                     process->deleteLater();
                     openParent();
                   });
  process->start(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), info.absoluteFilePath()});
#elif defined(TYPIST_HAS_FILE_MANAGER_DBUS)
  auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.FileManager1"),
                                                QStringLiteral("/org/freedesktop/FileManager1"),
                                                QStringLiteral("org.freedesktop.FileManager1"),
                                                QStringLiteral("ShowItems"));
  const auto url = QUrl::fromLocalFile(info.absoluteFilePath());
  message << QStringList{url.toString(QUrl::FullyEncoded)} << QString();
  auto* watcher =
      new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 5000), context);
  QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                   [openParent](QDBusPendingCallWatcher* reply) {
                     reply->deleteLater();
                     if (reply->isError()) openParent();
                   });
#else
  Q_UNUSED(context);
  openParent();
#endif
}

bool isMarkdownPath(const QString& path) {
  return path.endsWith(QLatin1String(".md"), Qt::CaseInsensitive);
}

QString firstMarkdownPath(const QList<QUrl>& urls) {
  for (const QUrl& url : urls) {
    const QString path = url.toLocalFile();
    if (isMarkdownPath(path)) return path;
  }
  return QString();
}

QString firstImagePath(const QList<QUrl>& urls) {
  static const QStringList suffixes = {QStringLiteral(".png"),  QStringLiteral(".jpg"),
                                       QStringLiteral(".jpeg"), QStringLiteral(".gif"),
                                       QStringLiteral(".webp"), QStringLiteral(".svg"),
                                       QStringLiteral(".bmp")};
  for (const QUrl& url : urls) {
    const QString path = url.toLocalFile();
    if (path.isEmpty()) continue;
    for (const QString& suffix : suffixes) {
      if (path.endsWith(suffix, Qt::CaseInsensitive)) return path;
    }
  }
  return QString();
}

}  // namespace typist::platform
