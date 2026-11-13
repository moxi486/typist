#pragma once
#include <QImage>
#include <QMap>
#include <QUrl>
namespace typist::platform {
struct StoredImage {
  QString path, target, error;
  bool ok() const { return !path.isEmpty() && error.isEmpty(); }
};
QString imageCacheDirectory();
StoredImage storeImage(const QImage& image, const QString& documentPath);
StoredImage importImage(const QString& path, const QString& documentPath, bool copy);
QString imageTarget(const QString& path, const QUrl& documentBase);
bool copyImageFile(const QString& source, const QString& destination, QString* error);
bool saveImagePng(const QImage& image, const QString& destination, QString* error);
QString imageMarkdown(const QString& target, QString alt = {});
struct RelocatedImages {
  QString markdown, error;
  QMap<QString, QString> targets;
};
RelocatedImages relocateImages(const QString& markdown, const QUrl& base,
                               const QString& newDocument);
}  // namespace typist::platform
