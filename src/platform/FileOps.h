#pragma once

#include <QString>
#include <QUrl>

namespace typist::platform {

// Sidebar file operations. A name without a .md suffix gets one appended.
bool createMarkdownFile(const QString& dir, const QString& name, QString* error);
bool renameFile(const QString& path, const QString& newName, QString* error);
bool deleteFile(const QString& path, QString* error);

// First local .md path among dropped URLs; empty when none applies.
QString firstMarkdownPath(const QList<QUrl>& urls);

}  // namespace typist::platform
