#include "FileOps.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <filesystem>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <shlobj.h>
#elif defined(Q_OS_MACOS)
#include <QProcess>
#include <stdio.h>
#elif defined(Q_OS_LINUX)
#include <fcntl.h>
#include <linux/fs.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
#if defined(TYPIST_HAS_FILE_MANAGER_DBUS)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#endif
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace typist::platform {

namespace {

QString withMarkdownSuffix(const QString& name, const QString& suffix) {
  return isMarkdownPath(name) ? name : name + '.' + suffix;
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

bool isWithin(const QString& parent, const QString& path) {
  const auto relative = QDir(parent).relativeFilePath(path);
  return relative != QLatin1String("..") && !relative.startsWith(QLatin1String("../")) &&
         !QDir::isAbsolutePath(relative);
}

bool renameWithoutReplacing(const QString& source, const QString& target) {
#if defined(Q_OS_WIN)
  return MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()),
                     reinterpret_cast<LPCWSTR>(target.utf16()), 0) != 0;
#elif defined(Q_OS_MACOS)
  return renamex_np(QFile::encodeName(source).constData(), QFile::encodeName(target).constData(),
                    RENAME_EXCL) == 0;
#elif defined(Q_OS_LINUX) && defined(SYS_renameat2)
  return syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(source).constData(), AT_FDCWD,
                 QFile::encodeName(target).constData(), RENAME_NOREPLACE) == 0;
#else
  return QFile::rename(source, target);
#endif
}

bool entryExists(const QFileInfo& entry) {
  return entry.exists() || entry.isSymLink() || entry.isJunction();
}

bool copyEntry(const QFileInfo& source, const QString& target) {
  if (source.isSymLink() || source.isJunction()) return false;
  if (source.isFile()) return QFile::copy(source.absoluteFilePath(), target);
  if (!source.isDir() || !QDir().mkdir(target)) return false;
  std::error_code error;
  std::filesystem::directory_iterator entries(source.filesystemAbsoluteFilePath(), error), end;
  for (; !error && entries != end; entries.increment(error)) {
    const QFileInfo child(entries->path());
    if (!copyEntry(child, QDir(target).filePath(child.fileName()))) return false;
  }
  return !error;
}

QString relocateWorkspaceEntry(const QString& root, const QString& path, const QString& targetPath,
                               const QByteArray& expectedIdentity, QString* error) {
  if (error) error->clear();
  const auto fail = [error](const char* reason) {
    if (error) *error = QString::fromLatin1(reason);
    return QString();
  };
  const QFileInfo source(path), target(targetPath);
  const QFileInfo folder(target.absolutePath());
  const auto workspace = QDir(root).canonicalPath();
  const auto from = source.canonicalFilePath();
  const auto to = folder.canonicalFilePath();
  if (root.isEmpty() || workspace.isEmpty() || !folder.isDir() || folder.isSymLink() ||
      folder.isJunction() || to.isEmpty() || !isWithin(workspace, to))
    return fail("Folder unavailable");
  if ((!source.isFile() && !source.isDir()) || source.isSymLink() || source.isJunction() ||
      from.isEmpty() || from == workspace || !isWithin(workspace, from))
    return fail("Entry unavailable");
  if (source.isDir() && isWithin(from, to)) return fail("Cannot move into itself");
  const QFileInfo destination(QDir(to).filePath(target.fileName()));
  if (from == destination.absoluteFilePath()) return {};
  if (entryExists(destination)) return fail("A file with that name already exists");
  if (!expectedIdentity.isEmpty() && workspaceEntryIdentity(from) != expectedIdentity)
    return fail("Entry replaced");
  if (!renameWithoutReplacing(from, destination.absoluteFilePath())) {
    const QFileInfo occupied(destination.absoluteFilePath());
    return fail(entryExists(occupied) ? "A file with that name already exists" : "Move failed");
  }
  return target.absoluteFilePath();
}

}  // namespace

QString createMarkdownFile(const QString& dir, const QString& name, const QString& suffix,
                           QString* error) {
  if (error) error->clear();
  const auto trimmed = name.trimmed();
  if (!availableDirectory(dir, error) || !validName(trimmed, error)) return {};
  const auto path = QDir(dir).absoluteFilePath(withMarkdownSuffix(trimmed, suffix));
  return createEmptyFile(path, error) ? path : QString();
}

bool createEmptyFile(const QString& path, QString* error) {
  if (error) error->clear();
  const QFileInfo info(path);
  if (!availableDirectory(info.absolutePath(), error) || !validName(info.fileName(), error))
    return false;
  if (isMarkdownPath(info.fileName()) && info.completeBaseName().isEmpty()) {
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

QString renameFile(const QString& path, const QString& newName, QString* error) {
  if (error) error->clear();
  const auto name = newName.trimmed();
  if (!validName(name, error)) return {};
  const QFileInfo source(path);
  const auto suffix =
      isMarkdownPath(source.fileName()) ? source.suffix() : markdownSuffixes().first();
  const auto fileName = withMarkdownSuffix(name, suffix);
  if (QFileInfo(fileName).completeBaseName().isEmpty()) {
    if (error) *error = QStringLiteral("File name is empty");
    return {};
  }
  const auto target = QDir(source.absolutePath()).filePath(fileName);
  if (target == source.absoluteFilePath()) return target;
  if (QFileInfo::exists(target)) {
    if (error) *error = QStringLiteral("A file with that name already exists");
    return {};
  }
  if (!QFile::rename(path, target)) {
    if (error) *error = QStringLiteral("Rename failed");
    return {};
  }
  return target;
}

bool moveMarkdownToTrash(const QString& path, QString* error) {
  if (error) error->clear();
  const QFileInfo file(path);
  if (!file.isFile() || file.isSymLink() || !isMarkdownPath(file.fileName())) {
    if (error) *error = QStringLiteral("File unavailable");
    return false;
  }
  if (!QFile::moveToTrash(file.absoluteFilePath())) {
    if (error) *error = QStringLiteral("Trash failed");
    return false;
  }
  return true;
}

QString moveWorkspaceEntry(const QString& root, const QString& path, const QString& destination,
                           QString* error) {
  if (destination.isEmpty()) {
    if (error) *error = QStringLiteral("Folder unavailable");
    return {};
  }
  return relocateWorkspaceEntry(root, path, QDir(destination).filePath(QFileInfo(path).fileName()),
                                {}, error);
}

QByteArray workspaceEntryIdentity(const QString& path) {
#ifdef Q_OS_WIN
  const auto native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
  const auto handle =
      CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                  FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  if (handle == INVALID_HANDLE_VALUE) return {};
  BY_HANDLE_FILE_INFORMATION info{};
  const bool ok = GetFileInformationByHandle(handle, &info);
  CloseHandle(handle);
  if (!ok || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
  const auto index = (qulonglong(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
  const auto created =
      (qulonglong(info.ftCreationTime.dwHighDateTime) << 32) | info.ftCreationTime.dwLowDateTime;
  return QByteArray::number(qulonglong(info.dwVolumeSerialNumber)) + ':' +
         QByteArray::number(index) + ':' + QByteArray::number(created);
#elif defined(Q_OS_UNIX)
  struct stat info {};
  if (lstat(QFile::encodeName(path).constData(), &info) || S_ISLNK(info.st_mode)) return {};
  const auto birth = QFileInfo(path).birthTime();
  return QByteArray::number(qulonglong(info.st_dev)) + ':' +
         QByteArray::number(qulonglong(info.st_ino)) + ':' +
         (birth.isValid() ? QByteArray::number(birth.toMSecsSinceEpoch()) : QByteArray());
#else
  return {};
#endif
}

bool restoreWorkspaceEntry(const QString& root, const QString& path, const QString& originalPath,
                           const QByteArray& identity, QString* error) {
  if (identity.isEmpty()) {
    if (error) *error = QStringLiteral("Entry replaced");
    return false;
  }
  return !relocateWorkspaceEntry(root, path, originalPath, identity, error).isEmpty();
}

QString copyIntoWorkspace(const QString& root, const QString& path, const QString& destination,
                          QString* error) {
  if (error) error->clear();
  const auto fail = [error](const char* reason) {
    if (error) *error = QString::fromLatin1(reason);
    return QString();
  };
  auto sourcePath = QDir::fromNativeSeparators(path);
  while (sourcePath.endsWith('/') && !QDir(sourcePath).isRoot()) sourcePath.chop(1);
  const QFileInfo source(sourcePath), folder(destination);
  const auto workspace = QDir(root).canonicalPath();
  const auto into = folder.canonicalFilePath();
  if (root.isEmpty() || destination.isEmpty() || workspace.isEmpty() || into.isEmpty() ||
      !folder.isDir() || folder.isSymLink() || folder.isJunction() || !isWithin(workspace, into))
    return fail("Folder unavailable");
  const auto from = source.canonicalFilePath();
  if ((!source.isFile() && !source.isDir()) || source.isSymLink() || source.isJunction() ||
      from.isEmpty() || source.fileName().isEmpty())
    return fail("Entry unavailable");
  if (source.isDir() && isWithin(from, into)) return fail("Cannot copy into itself");
  const QFileInfo target(QDir(into).filePath(source.fileName()));
  if (entryExists(target)) return fail("A file with that name already exists");

  // Stage in the target directory so a failed recursive copy leaves no partial entry
  // and a same-filesystem rename can publish the complete copy in one operation.
  QTemporaryDir staging(QDir(into).filePath(QStringLiteral(".typist-import-XXXXXX")));
  const auto staged = staging.filePath(QStringLiteral("entry"));
  if (!staging.isValid() || !copyEntry(source, staged)) return fail("Import failed");
  if (!renameWithoutReplacing(staged, target.absoluteFilePath()))
    return fail(entryExists(QFileInfo(target.absoluteFilePath()))
                    ? "A file with that name already exists"
                    : "Import failed");
  return QDir(folder.absoluteFilePath()).filePath(source.fileName());
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
  if (!source.isFile() || source.isSymLink() || !isMarkdownPath(source.fileName())) {
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

QString localFileLinkTarget(const QString& path, const QUrl& documentBase) {
  const auto absolute = QFileInfo(path).absoluteFilePath();
  if (documentBase.isLocalFile()) {
    const QString relative = QDir(documentBase.toLocalFile()).relativeFilePath(absolute);
    if (!QFileInfo(relative).isAbsolute())
      return QString::fromLatin1(QUrl::toPercentEncoding(relative, "/"));
  }
  return QUrl::fromLocalFile(absolute).toString(QUrl::FullyEncoded);
}

bool isMarkdownPath(const QString& path) {
  const auto dot = path.lastIndexOf('.');
  return dot >= 0 &&
         markdownSuffixes().contains(QStringView(path).sliced(dot + 1), Qt::CaseInsensitive);
}

const QStringList& markdownSuffixes() {
  static const QStringList suffixes{QStringLiteral("md"), QStringLiteral("markdown"),
                                    QStringLiteral("mdown"), QStringLiteral("mkd")};
  return suffixes;
}

QStringList markdownNameFilters(const QString& preferred) {
  auto suffixes = markdownSuffixes();
  if (suffixes.removeOne(preferred)) suffixes.prepend(preferred);
  for (auto& suffix : suffixes) suffix.prepend(QStringLiteral("*."));
  return suffixes;
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
