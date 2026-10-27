#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <QVector>

namespace typist::ui {

struct UpdateFeed {
  QString version;
  QVector<QUrl> assets;
};

// Feed URL is overridable so a fork can point at its own releases channel.
QString updateFeedUrl();

// Parse the latest.json release feed; false with a human-readable reason otherwise.
bool parseUpdateFeed(const QByteArray& json, UpdateFeed* feedOut, QString* errorOut);

// Numeric comparison of dotted versions; a prerelease suffix never wins over the base.
bool versionIsNewer(const QString& candidate, const QString& current);

}  // namespace typist::ui
