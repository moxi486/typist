#include "SyntaxHighlighter.h"

#include <QRegularExpression>
#include <QSet>
#include <QTextBlock>
#include <QTextDocument>
#include <algorithm>

namespace typist::render {
namespace {
constexpr int codeGroupProperty = 0x100001;
constexpr int codeLanguageProperty = 0x100005;
struct Data : QTextBlockUserData {
  SyntaxState lexical;
  QString language, fence;
  int group = 0;
};
QString normalized(QString language) {
  language = language.toLower().trimmed().section(' ', 0, 0);
  if (language == "c++" || language == "cc" || language == "cxx" || language == "h" ||
      language == "hpp")
    return "cpp";
  if (language == "javascript" || language == "jsx" || language == "typescript" ||
      language == "tsx" || language == "ts")
    return "js";
  if (language == "py" || language == "python3") return "python";
  if (language == "bash" || language == "sh" || language == "zsh" || language == "powershell" ||
      language == "ps1")
    return "shell";
  if (language == "yml") return "yaml";
  if (language == "htm" || language == "xml" || language == "svg") return "html";
  if (language == "md") return "markdown";
  if (language == "tex" || language == "math") return "latex";
  return language;
}
const QSet<QString>& keywords() {
  static const QSet<QString> words = [] {
    QSet<QString> set;
    for (const auto& word :
         QStringLiteral(
             "alignas alignof and as asm async await auto base begin bool boolean break byte case "
             "catch char class concept const constexpr consteval constinit continue crate debugger "
             "def default defer del delete do double dyn elif else end enum except explicit export "
             "extends extern false False final finally float fn for foreach from fun function "
             "global go goto if impl import in inline instanceOf instanceof int interface internal "
             "is lambda let long match mod module mutable mut namespace native new nil None "
             "nonlocal not null nullptr of operator or out override package pass private protected "
             "pub public raise readonly ref register reinterpret_cast repeat require restrict "
             "return sealed self Self short signed sizeof static static_assert static_cast string "
             "String struct super switch synchronized template then this throw throws trait true "
             "True try type typedef typename typeof union unknown unsigned unsafe use using val "
             "var virtual void volatile when where while with yield")
             .split(' ', Qt::SkipEmptyParts))
      set.insert(word);
    return set;
  }();
  return words;
}
bool identifier(QChar ch) { return ch.isLetterOrNumber() || ch == '_' || ch == '$'; }
bool escaped(const QString& text, int position) {
  int slashes = 0;
  while (position > 0 && text.at(--position) == '\\') ++slashes;
  return slashes % 2;
}
int quotedEnd(const QString& text, int start, const QString& delimiter, bool escape) {
  int end = start;
  while ((end = text.indexOf(delimiter, end)) >= 0) {
    if (!escape || !escaped(text, end)) return end + delimiter.size();
    ++end;
  }
  return -1;
}
QList<SyntaxSpan> markdownSpans(const QString& text) {
  QList<SyntaxSpan> out;
  const auto add = [&](const QRegularExpression& expression, SyntaxToken token) {
    auto matches = expression.globalMatch(text);
    while (matches.hasNext()) {
      const auto m = matches.next();
      out.append({int(m.capturedStart()), int(m.capturedLength()), token});
    }
  };
  static const QRegularExpression structure(QStringLiteral(
      "^\\s{0,3}(?:#{1,6}(?=\\s)|>+|[-+*](?=\\s)|\\d+[.)](?=\\s)|[-*_]{3,}\\s*$)|\\[[ xX]\\]"));
  static const QRegularExpression link(
      QStringLiteral("!?\\[[^\\]\\n]*\\]\\([^\\)\\n]*\\)|\\[\\^[^\\]]+\\]|https?://\\S+"));
  static const QRegularExpression emphasis(
      QStringLiteral("\\*{1,3}[^*\\n]+\\*{1,3}|_{1,3}[^_\\n]+_{1,3}|~~[^~\\n]+~~|\\$[^$\\n]+\\$"));
  static const QRegularExpression code(QStringLiteral("`+[^`\\n]+`+"));
  add(structure, SyntaxToken::Markup);
  add(emphasis, SyntaxToken::Keyword);
  add(link, SyntaxToken::Link);
  add(code, SyntaxToken::String);
  return out;
}
QList<SyntaxSpan> disjoint(const QList<SyntaxSpan>& spans, int size) {
  QList<int> roles(size, -1);
  for (const auto& span : spans)
    for (int i = qMax(0, span.start); i < qMin(size, span.start + span.length); ++i)
      roles[i] = int(span.token);
  QList<SyntaxSpan> out;
  for (int i = 0; i < size;) {
    if (roles[i] < 0) {
      ++i;
      continue;
    }
    const int start = i, role = roles[i];
    while (i < size && roles[i] == role) ++i;
    out.append({start, i - start, SyntaxToken(role)});
  }
  return out;
}
}  // namespace
QColor syntaxColor(SyntaxToken token, bool dark) {
  static const char* light[] = {"#8b3d91", "#087c8a", "#257344", "#68758a", "#a66321",
                                "#386db5", "#a44538", "#386db5", "#0969aa"};
  static const char* night[] = {"#d7a0e8", "#72c9cd", "#afd99a", "#8b9ab0", "#efb76f",
                                "#91baff", "#e6aa86", "#91baff", "#83c5ef"};
  return QColor(dark ? night[int(token)] : light[int(token)]);
}
QList<SyntaxSpan> codeSyntax(const QString& text, const QString& info, SyntaxState& state) {
  QList<SyntaxSpan> spans;
  const auto language = normalized(info);
  static const QSet<QString> supported = {
      "c",      "cpp",  "cs",  "csharp", "java",  "js",      "json",  "python",  "shell",
      "yaml",   "toml", "sql", "html",   "css",   "scss",    "rust",  "go",      "swift",
      "kotlin", "ruby", "r",   "lua",    "cmake", "mermaid", "latex", "markdown"};
  if (!supported.contains(language) || text.size() > 32768) {
    state = {};
    return spans;
  }
  if (language == "markdown") return disjoint(markdownSpans(text), text.size());
  const bool hashComment = language == "python" || language == "shell" || language == "yaml" ||
                           language == "toml" || language == "ruby" || language == "r" ||
                           language == "cmake";
  const bool slashComment = language == "c" || language == "cpp" || language == "cs" ||
                            language == "csharp" || language == "java" || language == "js" ||
                            language == "rust" || language == "go" || language == "swift" ||
                            language == "kotlin" || language == "css" || language == "scss";
  const bool sql = language == "sql", html = language == "html";
  const auto add = [&](int start, int end, SyntaxToken token) {
    if (end > start) spans.append({start, end - start, token});
  };
  int i = 0;
  if (!state.delimiter.isEmpty()) {
    const bool escapes = !state.comment && !state.delimiter.startsWith(')') && language != "go";
    const int end = quotedEnd(text, 0, state.delimiter, escapes);
    add(0, end < 0 ? text.size() : end, state.comment ? SyntaxToken::Comment : SyntaxToken::String);
    if (end < 0) return spans;
    i = end;
    state = {};
  }
  while (i < text.size()) {
    const QChar ch = text.at(i);
    const auto rest = QStringView(text).mid(i);
    if ((hashComment && ch == '#') || (slashComment && rest.startsWith(u"//")) ||
        ((sql || language == "lua") && rest.startsWith(u"--")) ||
        (language == "latex" && ch == '%') || (language == "mermaid" && rest.startsWith(u"%%"))) {
      add(i, text.size(), SyntaxToken::Comment);
      break;
    }
    const bool blockComment = slashComment && rest.startsWith(u"/*");
    if (blockComment || (html && rest.startsWith(u"<!--"))) {
      const QString close = blockComment ? QStringLiteral("*/") : QStringLiteral("-->");
      const int end = quotedEnd(text, i + (blockComment ? 2 : 4), close, false);
      add(i, end < 0 ? text.size() : end, SyntaxToken::Comment);
      if (end < 0) {
        state = {close, true};
        break;
      }
      i = end;
      continue;
    }
    // C++ raw strings keep their custom closing delimiter across lines.
    if ((language == "cpp" || language == "c") && rest.startsWith(u"R\"")) {
      const int open = text.indexOf('(', i + 2);
      if (open >= i + 2 && open - i <= 18) {
        const QString close = ")" + text.mid(i + 2, open - i - 2) + "\"";
        const int end = quotedEnd(text, open + 1, close, false);
        add(i, end < 0 ? text.size() : end, SyntaxToken::String);
        if (end < 0) {
          state = {close, false};
          break;
        }
        i = end;
        continue;
      }
    }
    if (ch == '"' || ch == '\'' ||
        (ch == '`' && (language == "js" || language == "go" || language == "shell"))) {
      QString delimiter(ch);
      const bool triple = language == "python" && rest.startsWith(QString(3, ch));
      if (triple) delimiter = QString(3, ch);
      const int end = quotedEnd(text, i + delimiter.size(), delimiter, true);
      add(i, end < 0 ? text.size() : end,
          (language == "json" && end > 0 && text.mid(end).trimmed().startsWith(':'))
              ? SyntaxToken::Property
              : SyntaxToken::String);
      if (end < 0) {
        if (triple || ch == '`' || sql || html || language == "ruby") state = {delimiter, false};
        break;
      }
      i = end;
      continue;
    }
    if (ch.isDigit() && (i == 0 || !identifier(text.at(i - 1)))) {
      const int start = i++;
      while (i < text.size() &&
             (text.at(i).isLetterOrNumber() || text.at(i) == '.' || text.at(i) == '_' ||
              ((text.at(i) == '+' || text.at(i) == '-') &&
               (text.at(i - 1) == 'e' || text.at(i - 1) == 'E'))))
        ++i;
      add(start, i, SyntaxToken::Number);
      continue;
    }
    if (identifier(ch) && !ch.isDigit()) {
      const int start = i++;
      while (i < text.size() && identifier(text.at(i))) ++i;
      const QString word = text.mid(start, i - start), lower = word.toLower();
      int next = i;
      while (next < text.size() && text.at(next).isSpace()) ++next;
      const QChar following = next < text.size() ? text.at(next) : QChar();
      static const QSet<QString> sqlWords = {
          "select", "from",   "where", "join",   "left",   "right",    "inner",  "outer",
          "on",     "group",  "by",    "order",  "asc",    "desc",     "having", "limit",
          "offset", "insert", "into",  "values", "update", "set",      "delete", "create",
          "table",  "drop",   "alter", "index",  "as",     "distinct", "union",  "all",
          "and",    "or",     "not",   "null",   "is",     "like",     "in",     "exists",
          "case",   "when",   "then",  "else",   "end",    "primary",  "key",    "references",
          "true",   "false"};
      if ((sql && sqlWords.contains(lower)) || keywords().contains(word) ||
          (language == "mermaid" &&
           (word == "flowchart" || word == "sequenceDiagram" || word == "participant" ||
            word == "actor" || word == "Note" || word == "as" || word == "graph")))
        add(start, i, SyntaxToken::Keyword);
      else if ((language == "yaml" || language == "toml" || language == "css" ||
                language == "scss") &&
               following == ':')
        add(start, i, SyntaxToken::Property);
      else if (html && start > 0 &&
               (text.at(start - 1) == '<' || (start > 1 && text.mid(start - 2, 2) == "</")))
        add(start, i, SyntaxToken::Markup);
      else if (html && following == '=')
        add(start, i, SyntaxToken::Property);
      else if (following == '(')
        add(start, i, SyntaxToken::Function);
      else if (word.at(0).isUpper() ||
               (language == "latex" && start > 0 && text.at(start - 1) == '\\'))
        add(start, i, SyntaxToken::Type);
      continue;
    }
    ++i;
  }
  return spans;
}
QString highlightedCodeHtml(const QString& text, const QString& language, bool dark) {
  SyntaxState state;
  QStringList output;
  for (const auto& line : text.split('\n')) {
    const auto spans = codeSyntax(line, language, state);
    int previous = 0;
    QString out;
    for (const auto& span : spans) {
      out += line.mid(previous, span.start - previous).toHtmlEscaped();
      out += QStringLiteral("<span style=\"color:%1\">%2</span>")
                 .arg(syntaxColor(span.token, dark).name(),
                      line.mid(span.start, span.length).toHtmlEscaped());
      previous = span.start + span.length;
    }
    out += line.mid(previous).toHtmlEscaped();
    output.append(out);
  }
  return output.join('\n');
}
SyntaxHighlighter::SyntaxHighlighter(QTextDocument* document, bool source)
    : QSyntaxHighlighter(document), m_markdownSource(source) {
  setObjectName(QStringLiteral("typistSyntaxHighlighter"));
}
void SyntaxHighlighter::setDark(bool dark) {
  if (m_dark == dark) return;
  m_dark = dark;
  rehighlight();
}
void SyntaxHighlighter::setEnabled(bool enabled) {
  if (m_enabled == enabled) return;
  m_enabled = enabled;
  rehighlight();
}
void SyntaxHighlighter::highlightBlock(const QString& text) {
  auto* data = new Data;
  const auto* previous = dynamic_cast<Data*>(currentBlock().previous().userData());
  QList<SyntaxSpan> spans;
  if (m_enabled) {
    if (!m_markdownSource) {
      data->group = currentBlock().blockFormat().intProperty(codeGroupProperty);
      if (data->group) {
        data->language = currentBlock().blockFormat().property(codeLanguageProperty).toString();
        if (previous && previous->group == data->group) {
          data->lexical = previous->lexical;
          if (data->language.isEmpty()) data->language = previous->language;
        }
        spans = codeSyntax(text, data->language, data->lexical);
      }
    } else {
      if (previous) {
        data->language = previous->language;
        data->fence = previous->fence;
        data->lexical = previous->lexical;
      }
      static const QRegularExpression fence(QStringLiteral("^ {0,3}(`{3,}|~{3,})([^`~]*)$"));
      const auto match = fence.match(text);
      if (!data->fence.isEmpty()) {
        if (match.hasMatch() && match.captured(1).startsWith(data->fence.at(0)) &&
            match.captured(1).size() >= data->fence.size() &&
            match.captured(2).trimmed().isEmpty()) {
          spans.append({0, int(text.size()), SyntaxToken::Markup});
          data->fence.clear();
          data->language.clear();
          data->lexical = {};
        } else
          spans = codeSyntax(text, data->language, data->lexical);
      } else if (match.hasMatch()) {
        data->fence = match.captured(1);
        data->language = match.captured(2).trimmed();
        spans.append({0, int(text.size()), SyntaxToken::Markup});
      } else if (text.size() <= 32768)
        spans = markdownSpans(text);
    }
  }
  for (const auto& span : spans)
    setFormat(span.start, span.length, syntaxColor(span.token, m_dark));
  const QString state = data->language + QChar(0x1f) + data->fence + QChar(0x1f) +
                        data->lexical.delimiter + QString::number(data->lexical.comment) +
                        QString::number(data->group);
  setCurrentBlockState(int(qHash(state) & 0x7fffffff));
  setCurrentBlockUserData(data);
}
SyntaxHighlighter* installSyntaxHighlighter(QTextDocument* document, bool source, bool dark,
                                            bool enabled) {
  auto* highlighter = dynamic_cast<SyntaxHighlighter*>(document->findChild<QSyntaxHighlighter*>(
      QStringLiteral("typistSyntaxHighlighter"), Qt::FindDirectChildrenOnly));
  if (!highlighter) highlighter = new SyntaxHighlighter(document, source);
  highlighter->setDark(dark);
  highlighter->setEnabled(enabled);
  return highlighter;
}
}  // namespace typist::render
