#pragma once

#include <QString>

namespace typist::platform {

// Sidebar file operations. A name without a .md suffix gets one appended.
bool createMarkdownFile(const QString& dir, const QString& name, QString* error);
bool renameFile(const QString& path, const QString& newName, QString* error);
bool deleteFile(const QString& path, QString* error);

}  // namespace typist::platform
