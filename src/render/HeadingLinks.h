#pragma once
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
QString addHeadingLinks(const QString& html);
}  // namespace typist::render
