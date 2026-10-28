#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace typist::ui {

QString updateFeedUrl() {
  const QByteArray override = qgetenv("TYPIST_UPDATE_URL");
  if (!override.isEmpty()) return QString::fromUtf8(override);
  return QStringLiteral(
      "https://github.com/typist-editor/typist/releases/latest/download/latest.json");
}

bool parseUpdateFeed(const QByteArray& json, UpdateFeed* feedOut, QString* errorOut) {
  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (errorOut != nullptr) *errorOut = QStringLiteral("Release feed is not a JSON object");
    return false;
  }
  const QJsonObject root = document.object();
  const QString version = root.value(QStringLiteral("version")).toString();
  if (version.isEmpty()) {
    if (errorOut != nullptr) *errorOut = QStringLiteral("Release feed has no version");
    return false;
  }
  const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
  if (assets.isEmpty()) {
    if (errorOut != nullptr) *errorOut = QStringLiteral("Release feed has no assets");
    return false;
  }
  if (feedOut != nullptr) {
    feedOut->version = version;
    feedOut->assets.clear();
    for (const QJsonValue& entry : assets) {
      const QString url = entry.toObject().value(QStringLiteral("url")).toString();
      if (!url.isEmpty()) feedOut->assets.append(QUrl(url));
    }
  }
  return true;
}

bool versionIsNewer(const QString& candidate, const QString& current) {
  const QString candidateBase = candidate.section(QLatin1Char('-'), 0, 0);
  const QString currentBase = current.section(QLatin1Char('-'), 0, 0);
  const QStringList left = candidateBase.split(QLatin1Char('.'));
  const QStringList right = currentBase.split(QLatin1Char('.'));
  const int count = qMax(left.size(), right.size());
  for (int i = 0; i < count; ++i) {
    const int a = i < left.size() ? left.at(i).toInt() : 0;
    const int b = i < right.size() ? right.at(i).toInt() : 0;
    if (a != b) return a > b;
  }
  return false;
}

}  // namespace typist::ui
