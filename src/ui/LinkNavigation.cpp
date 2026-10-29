#include "LinkNavigation.h"

#include "HeadingLinks.h"
#include "editor_document.h"
#include "markdown.h"

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
    url = QUrl::fromLocalFile(QUrl::fromPercentEncoding(pathPart.toUtf8()));
    url.setFragment(fragment);
  } else if (url.isRelative() && base.isLocalFile())
    url = base.resolved(url);
  if (!url.isLocalFile()) return {};
  QString path = url.toLocalFile();
  if (!QFileInfo::exists(path) && QFileInfo(path).suffix().isEmpty())
    for (const auto* extension : {".md", ".markdown", ".mdown", ".mkd"})
      if (QFileInfo::exists(path + extension)) {
        path += extension;
        break;
      }
  if (!QFileInfo::exists(path)) return {};
  const auto suffix = QFileInfo(path).suffix().toLower();
  const bool markdown = suffix == QLatin1String("md") || suffix == QLatin1String("markdown") ||
                        suffix == QLatin1String("mdown") || suffix == QLatin1String("mkd");
  auto fileUrl = QUrl::fromLocalFile(path);
  fileUrl.setFragment(fragment);
  return {markdown ? LinkDestination::Markdown : LinkDestination::LocalFile, fileUrl, path,
          fragment};
}
}  // namespace typist::ui
