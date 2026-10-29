#include "HeadingLinks.h"

#include <QMap>
#include <QRegularExpression>
#include <QTextDocumentFragment>

namespace typist::render {
QString headingSlug(const QString& title) {
  QString slug;
  for (const auto ch : title.trimmed().normalized(QString::NormalizationForm_C).toLower()) {
    if (ch.isLetterOrNumber() || ch == '-' || ch == '_')
      slug += ch;
    else if (ch.isSpace())
      slug += '-';
  }
  return slug.isEmpty() ? QStringLiteral("section") : slug;
}
QString HeadingIds::next(const QString& title) {
  const auto base = headingSlug(title);
  auto id = base;
  for (int suffix = 1; m_used.contains(id); ++suffix) id = base + '-' + QString::number(suffix);
  m_used.insert(id);
  return id;
}
QString addHeadingLinks(const QString& html) {
  static const QRegularExpression headings(QStringLiteral(
      "<blockquote>|</blockquote>|<h([1-6])(?: id=\"(heading-[0-9]+)\")?>([\\s\\S]*?)</h\\1>"));
  static const QRegularExpression images(QStringLiteral("<img\\b[^>]*\\balt=\"([^\"]*)\"[^>]*>"));
  HeadingIds ids;
  QMap<QString, QString> replacements;
  QString out;
  qsizetype end = 0;
  int quoteDepth = 0;
  auto matches = headings.globalMatch(html);
  while (matches.hasNext()) {
    const auto match = matches.next();
    if (match.captured() == QLatin1String("<blockquote>")) {
      ++quoteDepth;
      continue;
    }
    if (match.captured() == QLatin1String("</blockquote>")) {
      --quoteDepth;
      continue;
    }
    auto markup = match.captured(3);
    markup.replace(images, QStringLiteral("\\1"));
    const auto title = QTextDocumentFragment::fromHtml(markup).toPlainText();
    const auto id =
        quoteDepth ? QStringLiteral("_typist:quote:") + match.captured(2) : ids.next(title);
    if (!match.captured(2).isEmpty()) replacements[match.captured(2)] = id;
    out += html.mid(end, match.capturedStart() - end);
    out += QStringLiteral("<h%1 id=\"%2\">%3</h%1>")
               .arg(match.captured(1), id.toHtmlEscaped(), match.captured(3));
    end = match.capturedEnd();
  }
  out += html.mid(end);
  static const QRegularExpression nav(
      QStringLiteral("<nav class=\"table-of-contents\">[\\s\\S]*?</nav>"));
  static const QRegularExpression target(QStringLiteral("href=\"#(heading-[0-9]+)\""));
  QString result;
  end = 0;
  auto menus = nav.globalMatch(out);
  while (menus.hasNext()) {
    const auto menu = menus.next();
    result += out.mid(end, menu.capturedStart() - end);
    const auto original = menu.captured();
    auto links = target.globalMatch(original);
    qsizetype last = 0;
    while (links.hasNext()) {
      const auto link = links.next();
      result += original.mid(last, link.capturedStart() - last);
      result += QStringLiteral("href=\"#%1\"")
                    .arg(replacements.value(link.captured(1), link.captured(1)).toHtmlEscaped());
      last = link.capturedEnd();
    }
    result += original.mid(last);
    end = menu.capturedEnd();
  }
  return result + out.mid(end);
}
}  // namespace typist::render
