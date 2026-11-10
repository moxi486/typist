#pragma once

#include <QString>
#include <QUrl>

namespace typist::platform {

// Sidebar file operations. A name without a .md suffix gets one appended.
bool createMarkdownFile(const QString& dir, const QString& name, QString* error);
bool renameFile(const QString& path, const QString& newName, QString* error);
bool deleteFile(const QString& path, QString* error);
bool createDirectory(const QString& dir, const QString& name, QString* error);
// Copies saved bytes beside the source as name-copy.md, name-copy-2.md, etc.; never overwrites.
QString duplicateMarkdownFile(const QString& path, QString* error);
// Existing directory itself, or the parent of an existing file; empty for unavailable paths.
QUrl containingDirectoryUrl(const QString& path);

// First local .md path among dropped URLs; empty when none applies.
QString firstMarkdownPath(const QList<QUrl>& urls);

// First local image path among dropped URLs; empty when none applies.
QString firstImagePath(const QList<QUrl>& urls);

}  // namespace typist::platform
