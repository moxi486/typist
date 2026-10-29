#pragma once
#include <QSyntaxHighlighter>
#include <QTextBlockUserData>

namespace typist::render {
enum class SyntaxToken { Keyword, Type, String, Comment, Number, Function, Property, Markup, Link };
struct SyntaxSpan {
  int start;
  int length;
  SyntaxToken token;
};
struct SyntaxState {
  QString delimiter;
  bool comment = false;
};
QList<SyntaxSpan> codeSyntax(const QString& text, const QString& language, SyntaxState& state);
QColor syntaxColor(SyntaxToken token, bool dark);
QString highlightedCodeHtml(const QString& text, const QString& language, bool dark = false);

class SyntaxHighlighter : public QSyntaxHighlighter {
 public:
  explicit SyntaxHighlighter(QTextDocument* document, bool markdownSource = false);
  void setDark(bool dark);
  void setEnabled(bool enabled);
  bool enabled() const { return m_enabled; }

 protected:
  void highlightBlock(const QString& text) override;

 private:
  bool m_markdownSource = false;
  bool m_dark = false;
  bool m_enabled = true;
};
SyntaxHighlighter* installSyntaxHighlighter(QTextDocument* document, bool markdownSource,
                                            bool dark = false, bool enabled = true);
}  // namespace typist::render
