#include "ImageResources.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QTextDocumentFragment>

namespace typist::render {
QString localImagePath(const QString& target, const QUrl& base) {
  if (target.isEmpty()) return {};
  // QFileInfo recognizes native drive paths before QUrl interprets the drive as a scheme.
  if (QFileInfo(target).isAbsolute()) return target;
  const QUrl url(target);
  if (url.isLocalFile()) return url.toLocalFile();
  if (!url.isRelative()) return {};
  if (!base.isEmpty() && base.isLocalFile()) return base.resolved(url).toLocalFile();
  return QUrl::fromPercentEncoding(target.toUtf8());
}
QImage imagePreview(const QString& path, QSize maximum) {
  QImageReader reader(path);
  reader.setAutoTransform(true);
  if (!reader.canRead()) return {};
  const auto natural = reader.size();
  if (natural.isValid() &&
      (natural.width() > maximum.width() || natural.height() > maximum.height()))
    reader.setScaledSize(natural.scaled(maximum, Qt::KeepAspectRatio));
  QImage result = reader.read();
  if (result.width() > maximum.width() || result.height() > maximum.height())
    result = result.scaled(maximum, Qt::KeepAspectRatio, Qt::SmoothTransformation);
  return result;
}
QString embedLocalImages(QString html, const QUrl& base) {
  static const QRegularExpression image(QStringLiteral("<img\\b[^>]*\\bsrc=\"([^\"]*)\"[^>]*>"));
  auto matches = image.globalMatch(html);
  struct Replacement {
    int start, length;
    QString value;
  };
  QList<Replacement> replacements;
  while (matches.hasNext()) {
    const auto match = matches.next();
    const QString target = QTextDocumentFragment::fromHtml(match.captured(1)).toPlainText();
    const QString path = localImagePath(target, base);
    if (path.isEmpty() || !QFileInfo(path).isFile()) continue;
    QFile file(path);
    if (file.size() > 64 * 1024 * 1024 || !file.open(QIODevice::ReadOnly)) continue;
    const auto mime = QMimeDatabase().mimeTypeForFile(path).name();
    if (!mime.startsWith(QStringLiteral("image/"))) continue;
    replacements.append({int(match.capturedStart(1)), int(match.capturedLength(1)),
                         QStringLiteral("data:") + mime + QStringLiteral(";base64,") +
                             QString::fromLatin1(file.readAll().toBase64())});
  }
  for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
    html.replace(it->start, it->length, it->value);
  return html;
}
}  // namespace typist::render
