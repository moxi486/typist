#pragma once

#include <QString>

namespace typist::platform {

// Writes contents to path by writing a sibling temporary file first and then
// renaming it over the target. The temporary file is removed on any failure.
bool writeAtomic(const QString& path, const QString& contents, QString* error = nullptr);

}  // namespace typist::platform
