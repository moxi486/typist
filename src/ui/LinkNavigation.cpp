#include "LinkNavigation.h"

#include "FileOps.h"
#include "HeadingLinks.h"
#include "LinkEditing.h"
#include "Settings.h"
#include "editor_document.h"
#include "markdown.h"

#include <QDir>
#include <QFileInfo>
#include <QMimeData>
#include <QRegularExpression>
#include <QTextBlock>

namespace typist::ui {
FileDropTarget fileDropTarget(const QMimeData* data) {
  if (data->hasFormat(kSidebarLinkMime)) return {};
  for (const auto& url : data->urls()) {
    const auto path = url.toLocalFile();
    if (!url.isValid() || !url.isLocalFile() || path.contains(QChar::Null) ||
        !QDir::isAbsolutePath(path))
      continue;
    const QFileInfo entry(path);
    const bool folder = entry.isDir();
    if (!folder && (!platform::isMarkdownPath(path) || !entry.isFile())) continue;
    const auto preferences = platform::fileDropPreferences();
    if ((folder ? preferences.folder : preferences.markdown) ==
        platform::FileDropAction::InsertLink)
      return {FileDropTarget::InsertLink, path};
    return {folder ? FileDropTarget::OpenFolder : FileDropTarget::OpenMarkdown, path};
  }
  return {};
}

QString localFileLinkMarkdown(const QString& path, const QUrl& base) {
  if (!QDir::isAbsolutePath(path) || path.contains(QChar::Null)) return {};
  const QFileInfo file(path);
  const bool folder = file.isDir();
  if (!folder && !file.isFile()) return {};
  auto target = editor::normalizedLink(platform::localFileLinkTarget(path, base));
  target.replace('&', QStringLiteral("%26"));
  if (folder && !target.endsWith('/')) target += '/';
  auto label = QFileInfo(QDir::cleanPath(path)).fileName();
  if (label.isEmpty()) label = path;
  label.replace(QRegularExpression(QStringLiteral("[\\r\\n\\t\\x{2028}\\x{2029}]")),
                QStringLiteral(" "));
  core::InlineRun link;
  link.text = label.toStdString();
  link.url = target.toStdString();
  link.link = true;
  return QString::fromStdString(core::serializeInline({link}));
}

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
    for (const auto& suffix : platform::markdownSuffixes()) {
      const auto candidate = path + '.' + suffix;
      if (QFileInfo::exists(candidate)) {
        path = candidate;
        break;
      }
    }
    file.setFile(path);
  }
  if (file.isSymLink() && !file.exists()) return {};
  const bool markdown = platform::isMarkdownPath(file.fileName());
  auto kind = LinkDestination::LocalFile;
  if (file.exists()) {
    if (markdown && file.isFile()) kind = LinkDestination::Markdown;
  } else {
    if ((!markdown && !extensionless) || path.endsWith('/') || path.endsWith('\\') ||
        !QFileInfo(file.absolutePath()).isDir() || !url.host().isEmpty() || url.hasQuery() ||
        pathPart.contains('?'))
      return {};
    if (!markdown) path += '.' + platform::defaultMarkdownSuffix();
    kind = LinkDestination::MissingMarkdown;
  }
  auto fileUrl = QUrl::fromLocalFile(path);
  fileUrl.setFragment(fragment);
  return {kind, fileUrl, path, fragment};
}
}  // namespace typist::ui
