#include "RecoveryStore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>

#include <algorithm>

namespace typist::ui {

namespace {

QString sanitizedKey(const QString& key) {
  QString out;
  for (const QChar ch : key) {
    if (ch.isLetterOrNumber() || ch == QChar(45)) out += ch;
  }
  return out.isEmpty() ? QStringLiteral("untitled") : out.left(40);
}

}  // namespace

QString recoveryDir() {
  const QString override = qEnvironmentVariable("TYPIST_RECOVERY_DIR");
  if (!override.isEmpty()) return override;
  return QDir::homePath() + QStringLiteral("/.typist/recovery");
}

QString recoveryKeyFor(const QString& path) {
  if (path.isEmpty()) return QStringLiteral("untitled");
  const QByteArray digest = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1);
  return sanitizedKey(QString::fromLatin1(digest.toHex()));
}

QString recoveryPathFor(const QString& path) {
  return recoveryDir() + QStringLiteral("/") + recoveryKeyFor(path) + QStringLiteral(".md");
}

bool writeDraft(const QString& path, const QString& markdown) {
  QDir dir(recoveryDir());
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return false;
  QFile file(recoveryPathFor(path));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
  const QByteArray bytes = markdown.toUtf8();
  const bool written = file.write(bytes) == bytes.size();
  file.close();
  return written;
}

bool readDraft(const QString& path, QString* markdownOut) {
  QFile file(recoveryPathFor(path));
  if (!file.open(QIODevice::ReadOnly)) return false;
  if (markdownOut) *markdownOut = QString::fromUtf8(file.readAll());
  file.close();
  return true;
}

QStringList draftPaths() {
  QDir dir(recoveryDir());
  QFileInfoList entries = dir.entryInfoList(QStringList() << QStringLiteral("*.md"), QDir::Files);
  std::sort(entries.begin(), entries.end(), [](const QFileInfo& left, const QFileInfo& right) {
    return left.lastModified() > right.lastModified();
  });
  QStringList out;
  for (const QFileInfo& entry : entries) out.append(entry.absoluteFilePath());
  return out;
}

bool removeDraft(const QString& path) {
  QFile file(recoveryPathFor(path));
  if (!file.exists()) return false;
  return file.remove();
}

}  // namespace typist::ui
