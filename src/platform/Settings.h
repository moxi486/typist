#pragma once

#include <QString>

namespace typist::platform {

// User settings live under the Typist organization so each platform picks its own
// native store: registry on Windows, ini under XDG on Linux and macOS.
QString themePreference(const QString& fallback = QStringLiteral("system"));

void setThemePreference(const QString& value);

// PDF page controls; the defaults are A4 with 20mm margins.
QString pdfPaper(const QString& fallback = QStringLiteral("a4"));

void setPdfPaper(const QString& value);

double pdfMarginMm(double fallback = 20.0);

void setPdfMarginMm(double value);

}  // namespace typist::platform
