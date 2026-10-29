#pragma once

#include "PdfWriter.h"

#include <QString>
#include <QStringList>

namespace typist::platform {

// User settings live under the Typist organization so each platform picks its own
// native store: registry on Windows, ini under XDG on Linux and macOS.
QString themePreference(const QString& fallback = QStringLiteral("system"));

void setThemePreference(const QString& value);

QString languagePreference();
void setLanguagePreference(const QString& value);
bool copyImagesToAssets();
void setCopyImagesToAssets(bool enabled);

// PDF page controls; the defaults are A4 with 20mm margins.
QString pdfPaper(const QString& fallback = QStringLiteral("a4"));

void setPdfPaper(const QString& value);

double pdfMarginMm(double fallback = 20.0);

void setPdfMarginMm(double value);
PdfPageOptions pdfOptions(const QString& title);
void setPdfOptions(const PdfPageOptions& options);

// Most recently opened or saved paths, newest first; stale entries are pruned by callers.
QStringList recentFiles(int limit = 8);

void addRecentFile(const QString& path);

void removeRecentFile(const QString& path);

}  // namespace typist::platform
