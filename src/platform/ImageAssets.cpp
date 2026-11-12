#include "ImageAssets.h"

#include "ImageResources.h"
#include "markdown.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QStandardPaths>

namespace typist::platform {
namespace {
constexpr qint64 kMaxImageFileBytes = 64 * 1024 * 1024;

QString message(const char* source) { return QCoreApplication::translate("ImageAssets", source); }
bool fail(QString* error, const char* reason) {
  if (error) *error = message(reason);
  return false;
}

QString assetDirectory(const QString& documentPath) {
  if (documentPath.isEmpty()) return imageCacheDirectory();
  const QFileInfo document(documentPath);
  return document.dir().filePath(document.completeBaseName() + QStringLiteral(".assets"));
}
QString targetFor(const QString& path, const QString& documentPath) {
  const auto base = documentPath.isEmpty()
                        ? QUrl()
                        : QUrl::fromLocalFile(QFileInfo(documentPath).absolutePath() + '/');
  return imageTarget(path, base);
}
StoredImage store(const QByteArray& bytes, const QString& suffix, const QString& documentPath) {
  const QString directory = assetDirectory(documentPath);
  if (!QDir().mkpath(directory)) return {{}, {}, message("Cannot create the image assets folder")};
  const QString name =
      QStringLiteral("image-") +
      QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) +
      '.' + suffix;
  const QString path = QDir(directory).filePath(name);
  if (QFileInfo::exists(path)) {
    QFile existing(path);
    if (!existing.open(QIODevice::ReadOnly) || existing.readAll() != bytes)
      return {{}, {}, message("An existing image has different contents; nothing was overwritten")};
  } else {
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
        !output.commit())
      return {{}, {}, message("Cannot save the image")};
  }
  return {path, targetFor(path, documentPath), {}};
}
}  // namespace
QString imageTarget(const QString& path, const QUrl& documentBase) {
  const auto absolute = QFileInfo(path).absoluteFilePath();
  if (documentBase.isLocalFile()) {
    const QString relative = QDir(documentBase.toLocalFile()).relativeFilePath(absolute);
    if (!QFileInfo(relative).isAbsolute())
      return QString::fromLatin1(QUrl::toPercentEncoding(relative, "/"));
  }
  return QUrl::fromLocalFile(absolute).toString(QUrl::FullyEncoded);
}

bool copyImageFile(const QString& source, const QString& destination, QString* error) {
  const QFileInfo original(source), target(destination);
  QFile input(source);
  if (!original.isFile() || !input.open(QIODevice::ReadOnly))
    return fail(error, "Cannot read the image file");
  if (input.size() > kMaxImageFileBytes) return fail(error, "The image is too large");
  if (!QImageReader(&input).canRead() || !input.seek(0))
    return fail(error, "This image format cannot be read");
  if (original == target)
    return fail(error, "Choose a different destination from the source image");
  const auto bytes = input.read(kMaxImageFileBytes + 1);
  if (input.error() != QFileDevice::NoError || bytes.isEmpty())
    return fail(error, "Cannot read the image file");
  if (bytes.size() > kMaxImageFileBytes) return fail(error, "The image is too large");
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit())
    return fail(error, "Cannot save the image");
  return true;
}

bool saveImagePng(const QImage& image, const QString& destination, QString* error) {
  QSaveFile output(destination);
  if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG") || !output.commit())
    return fail(error, "Cannot save the image");
  return true;
}

QString imageCacheDirectory() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
      .filePath(QStringLiteral("images"));
}
StoredImage storeImage(const QImage& image, const QString& documentPath) {
  if (image.isNull()) return {{}, {}, message("The clipboard does not contain a valid image")};
  if (qint64(image.width()) * image.height() > 32000000)
    return {{}, {}, message("The image is too large")};
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG"))
    return {{}, {}, message("Cannot encode the image")};
  return store(bytes, QStringLiteral("png"), documentPath);
}
StoredImage importImage(const QString& path, const QString& documentPath, bool copy) {
  QFile input(path);
  if (input.size() > kMaxImageFileBytes) return {{}, {}, message("The image is too large")};
  if (!input.open(QIODevice::ReadOnly)) return {{}, {}, message("Cannot read the image file")};
  QImageReader reader(path);
  const auto format = QString::fromLatin1(reader.format()).toLower();
  if (!reader.canRead() || render::imagePreview(path).isNull())
    return {{}, {}, message("This image format cannot be read")};
  if (!copy)
    return {QFileInfo(path).absoluteFilePath(),
            targetFor(QFileInfo(path).absoluteFilePath(), documentPath),
            {}};
  return store(input.readAll(), format, documentPath);
}
QString imageMarkdown(const QString& target, QString alt) {
  alt.replace('\\', QStringLiteral("\\\\"));
  alt.replace('[', QStringLiteral("\\["));
  alt.replace(']', QStringLiteral("\\]"));
  alt.replace('\n', ' ');
  alt.replace('\r', ' ');
  return QStringLiteral("![") + alt + QStringLiteral("](") + target + ')';
}
RelocatedImages relocateImages(const QString& markdown, const QString& oldDocument,
                               const QString& newDocument) {
  RelocatedImages result{markdown, {}, {}};
  if (oldDocument == newDocument) return result;
  const QUrl base = oldDocument.isEmpty()
                        ? QUrl()
                        : QUrl::fromLocalFile(QFileInfo(oldDocument).absolutePath() + '/');
  const auto source = markdown.toUtf8();
  const auto images =
      core::imageSources(std::string_view(source.constData(), std::size_t(source.size())));
  struct Replacement {
    int start, length;
    QByteArray target;
  };
  QList<Replacement> replacements;
  for (const auto& image : images) {
    const QString target = QString::fromStdString(image.target);
    const QString path = render::localImagePath(target, base);
    if (path.isEmpty() || !QFileInfo::exists(path)) continue;
    const bool cached =
        QFileInfo(path).absolutePath() == QFileInfo(imageCacheDirectory()).absoluteFilePath();
    const bool relative = QUrl(target).isRelative() && !QFileInfo(target).isAbsolute();
    if (!cached && !relative) continue;
    // An inline destination or a reference definition is rewritten only at the
    // parser-confirmed location; identical text in code is never touched.
    if (image.start == std::string::npos) {
      result.error =
          message("Save As cannot relocate this image reference; use an inline image link");
      return result;
    }
    const auto imported = importImage(path, newDocument, true);
    if (!imported.ok()) {
      result.error = imported.error;
      return result;
    }
    result.targets.insert(target, imported.target);
    replacements.append({int(image.start), int(image.length), imported.target.toUtf8()});
  }
  QByteArray updated = source;
  for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
    updated.replace(it->start, it->length, it->target);
  result.markdown = QString::fromUtf8(updated);
  return result;
}
}  // namespace typist::platform
