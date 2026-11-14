#pragma once
#include <QTextDocument>
#include <QUrl>

class QMimeData;

namespace typist::ui {
inline const auto kSidebarLinkMime = QStringLiteral("application/x-typist-sidebar-link");

struct FileDropTarget {
  enum Action { OpenMarkdown, OpenFolder, InsertLink } action = OpenMarkdown;
  QString path;
};
FileDropTarget fileDropTarget(const QMimeData* data);
QString localFileLinkMarkdown(const QString& path, const QUrl& base);

struct HeadingLink {
  QString id, title;
  int position = 0;
};
QList<HeadingLink> documentHeadingLinks(QTextDocument* document);
struct LinkDestination {
  enum Kind { Invalid, Heading, Markdown, MissingMarkdown, External, LocalFile } kind = Invalid;
  QUrl url;
  QString path, fragment;
};
LinkDestination resolveLink(const QString& destination, const QUrl& base);
}  // namespace typist::ui
