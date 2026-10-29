#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

namespace typist::editor {
struct HtmlImportResult {
  QString markdown;
  QString title;
  QStringList notices;
  QString error;
  bool ok() const { return error.isEmpty(); }
};

// Converts the native Qt rich-text subset without loading images, stylesheets or URLs.
// A file base resolves local references; an empty base leaves unresolved references as text.
HtmlImportResult importHtml(const QString& html, const QUrl& base = {});
HtmlImportResult importHtmlFile(const QString& path);
}  // namespace typist::editor
