#pragma once
#include <QImage>
#include <QUrl>
namespace typist::render {
QString localImagePath(const QString& target, const QUrl& base = {});
QImage imagePreview(const QString& path, QSize maximum = QSize(640, 480));
QString embedLocalImages(QString html, const QUrl& base);
}  // namespace typist::render
