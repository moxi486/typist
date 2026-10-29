#pragma once
#include <QString>
#include <QStringList>

namespace typist::platform {
struct EpubOptions {
  QString title;
  QString author;
  QString language = QStringLiteral("en");
};
bool exportEpub(const QString& path, const QString& markdown, const EpubOptions& options,
                QString* error, const QString& baseDirectory = {}, QStringList* notices = nullptr);
}  // namespace typist::platform
