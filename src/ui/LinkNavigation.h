#pragma once
#include <QTextDocument>
#include <QUrl>

namespace typist::ui {
struct HeadingLink {
  QString id, title;
  int position = 0;
};
QList<HeadingLink> documentHeadingLinks(QTextDocument* document);
struct LinkDestination {
  enum Kind { Invalid, Heading, Markdown, External, LocalFile } kind = Invalid;
  QUrl url;
  QString path, fragment;
};
LinkDestination resolveLink(const QString& destination, const QUrl& base);
}  // namespace typist::ui
