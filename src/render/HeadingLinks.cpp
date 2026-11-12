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
QString addHeadingLinks(const QString& html, QList<HtmlHeading>* outline) {
  static const QRegularExpression headings(
      QStringLiteral(
          R"rx(<!--[\s\S]*?(?:-->|$)|<(?<raw>script|style|textarea|template)\b[^>]*>[\s\S]*?(?:</\k<raw>\s*>|$)|<blockquote\b[^>]*>|</blockquote\s*>|<h(?<level>[1-6])(?: id="(?<old>heading-[0-9]+)")?>(?<content>[\s\S]*?)</h\k<level>>)rx"),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression images(QStringLiteral("<img\\b[^>]*\\balt=\"([^\"]*)\"[^>]*>"));
  if (outline) outline->clear();
  HeadingIds ids;
  QMap<QString, QString> replacements;
  QString out;
  qsizetype end = 0;
  int quoteDepth = 0;
  auto matches = headings.globalMatch(html);
  while (matches.hasNext()) {
    const auto match = matches.next();
    const auto level = match.capturedView(QStringLiteral("level"));
    if (level.isEmpty()) {
      if (match.capturedView().startsWith(u"<blockquote", Qt::CaseInsensitive))
        ++quoteDepth;
      else if (match.capturedView().startsWith(u"</blockquote", Qt::CaseInsensitive))
        quoteDepth = qMax(0, quoteDepth - 1);
      continue;
    }
    const auto content = match.captured(QStringLiteral("content"));
    auto markup = content;
    markup.replace(images, QStringLiteral("\\1"));
    const auto title = QTextDocumentFragment::fromHtml(markup).toPlainText();
    const auto old = match.captured(QStringLiteral("old"));
    const auto id = quoteDepth ? QStringLiteral("_typist:quote:") + old : ids.next(title);
    if (!old.isEmpty()) replacements[old] = id;
    if (outline && !quoteDepth && !title.trimmed().isEmpty())
      outline->append({level.toInt(), title, id});
    out += html.mid(end, match.capturedStart() - end);
    out += QStringLiteral("<h%1 id=\"%2\">%3</h%1>")
               .arg(level.toString(), id.toHtmlEscaped(), content);
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

namespace {
QString outlineItems(const QList<HtmlHeading>& headings, qsizetype& index, int parentLevel,
                     bool collapsible) {
  QString html = QStringLiteral("<ul>\n");
  while (index < headings.size() && headings.at(index).level > parentLevel) {
    const auto& item = headings.at(index++);
    const auto link = QStringLiteral("<a href=\"#%1\">%2</a>")
                          .arg(item.id.toHtmlEscaped(), item.title.toHtmlEscaped());
    if (collapsible && index < headings.size() && headings.at(index).level > item.level) {
      html += QStringLiteral("<li><details open><summary>") + link + QStringLiteral("</summary>\n");
      html += outlineItems(headings, index, item.level, true);
      html += QStringLiteral("</details></li>\n");
    } else {
      html += QStringLiteral("<li class=\"outline-level-%1\">").arg(item.level) + link +
              QStringLiteral("</li>\n");
    }
  }
  return html + QStringLiteral("</ul>\n");
}
}  // namespace

QString htmlOutline(const QList<HtmlHeading>& headings, const QString& title, bool collapsible) {
  if (headings.isEmpty()) return {};
  qsizetype index = 0;
  return QStringLiteral("<nav class=\"export-outline %1\" aria-label=\"%2\">\n<h2>%2</h2>\n")
             .arg(collapsible ? QStringLiteral("collapsible") : QStringLiteral("flat"),
                  title.toHtmlEscaped()) +
         outlineItems(headings, index, 0, collapsible) + QStringLiteral("</nav>\n");
}
}  // namespace typist::render
