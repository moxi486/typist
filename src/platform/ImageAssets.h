#pragma once
#include <QImage>
#include <QMap>
namespace typist::platform {
struct StoredImage {
  QString path, target, error;
  bool ok() const { return !path.isEmpty() && error.isEmpty(); }
};
QString imageCacheDirectory();
StoredImage storeImage(const QImage& image, const QString& documentPath);
StoredImage importImage(const QString& path, const QString& documentPath, bool copy);
QString imageMarkdown(const QString& target, QString alt = {});
struct RelocatedImages {
  QString markdown, error;
  QMap<QString, QString> targets;
};
RelocatedImages relocateImages(const QString& markdown, const QString& oldDocument,
                               const QString& newDocument);
}  // namespace typist::platform
