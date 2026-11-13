#include "LinkNavigation.h"

#include "HeadingLinks.h"
#include "editor_document.h"
#include "markdown.h"

#include <QDir>
#include <QFileInfo>
#include <QTextBlock>

namespace typist::ui {
QList<HeadingLink> documentHeadingLinks(QTextDocument* document) {
  QList<HeadingLink> links;
  render::HeadingIds ids;
  for (auto block = document->begin(); block.isValid(); block = block.next()) {
    if (block.blockFormat().headingLevel() <= 0) continue;
    auto title = block.text();
    if (title.contains(QChar::ObjectReplacementCharacter)) {
      const auto parsed = core::parseMarkdown(editor::inlineMarkdown(block).toStdString());
      if (!parsed.blocks.empty()) title = QString::fromStdString(parsed.blocks.front().text);
    }
    links.append({ids.next(title), title, block.position()});
  }
  return links;
}
LinkDestination resolveLink(const QString& destination, const QUrl& base) {
  const auto target = destination.trimmed();
  if (target.isEmpty()) return {};
  QUrl url(target, QUrl::TolerantMode);
  const auto fragment = url.fragment(QUrl::FullyDecoded);
  if (target.startsWith('#')) return {LinkDestination::Heading, url, {}, fragment};
  const auto scheme = url.scheme().toLower();
  if (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
      scheme == QLatin1String("mailto")) {
    if ((scheme != QLatin1String("mailto") && url.host().isEmpty()) || !url.isValid()) return {};
    return {LinkDestination::External, url, {}, fragment};
  }
  const auto pathPart = target.section('#', 0, 0);
  if (QFileInfo(pathPart).isAbsolute()) {
    const auto decoded = QUrl::fromPercentEncoding(pathPart.toUtf8());
    if (decoded.contains(QChar::Null)) return {};
    url = QUrl::fromLocalFile(decoded);
    url.setFragment(fragment);
  } else if (url.isRelative() && base.isLocalFile())
    url = base.resolved(url);
  if (!url.isValid() || !url.isLocalFile()) return {};
  QString path = url.toLocalFile();
  if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null)) return {};
  QFileInfo file(path);
  const bool extensionless = file.suffix().isEmpty() && !file.fileName().endsWith('.');
  if (!file.exists() && !file.isSymLink() && extensionless && !path.endsWith('/')) {
    for (const auto* extension : {".md", ".markdown", ".mdown", ".mkd"})
      if (QFileInfo::exists(path + extension)) {
        path += extension;
        break;
      }
    file.setFile(path);
  }
  if (file.isSymLink() && !file.exists()) return {};
  const auto suffix = file.suffix().toLower();
  const bool markdown = suffix == QLatin1String("md") || suffix == QLatin1String("markdown") ||
                        suffix == QLatin1String("mdown") || suffix == QLatin1String("mkd");
  auto kind = LinkDestination::LocalFile;
  if (file.exists()) {
    if (markdown && file.isFile()) kind = LinkDestination::Markdown;
  } else {
    if ((!markdown && !extensionless) || path.endsWith('/') || path.endsWith('\\') ||
        !QFileInfo(file.absolutePath()).isDir() || !url.host().isEmpty() || url.hasQuery() ||
        pathPart.contains('?'))
      return {};
    if (!markdown) path += QStringLiteral(".md");
    kind = LinkDestination::MissingMarkdown;
  }
  auto fileUrl = QUrl::fromLocalFile(path);
  fileUrl.setFragment(fragment);
  return {kind, fileUrl, path, fragment};
}
}  // namespace typist::ui
