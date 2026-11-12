#pragma once
#include <QList>
#include <QSet>
#include <QString>

namespace typist::render {
class HeadingIds {
 public:
  QString next(const QString& title);

 private:
  QSet<QString> m_used;
};
QString headingSlug(const QString& title);
struct HtmlHeading {
  int level;
  QString title;
  QString id;
};
QString addHeadingLinks(const QString& html, QList<HtmlHeading>* outline = nullptr);
QString htmlOutline(const QList<HtmlHeading>& headings, const QString& title, bool collapsible);
}  // namespace typist::render
