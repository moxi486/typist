#pragma once

#include <QRegularExpression>
#include <QString>
#include <QTextCursor>
#include <QTextDocument>
#include <QPair>
#include <QVector>

namespace typist::ui {

// Plain-text search over a document: counts, forward and backward selection, and
// wrap-around across both ends. Table cell text is reachable through the frame tree.
class SearchController {
 public:
  void setCaseSensitive(bool enabled);
  bool caseSensitive() const;

  // Treats the pattern as a regular expression; an invalid pattern matches nothing.
  void setRegex(bool enabled);
  bool regex() const;

  int countMatches(const QTextDocument* document, const QString& pattern) const;

  // Cursors for every match, capped so highlighting stays cheap on large documents.
  QVector<QTextCursor> allMatches(const QTextDocument* document, const QString& pattern, int maxHighlights = 1000) const;
  bool selectNext(QTextCursor& cursor, const QString& pattern) const;
  bool selectPrevious(QTextCursor& cursor, const QString& pattern) const;

 private:
  QTextDocument::FindFlags flags() const;
  QRegularExpression regexPattern(const QString& pattern) const;
  // Absolute match ranges per block for the regex mode; an invalid pattern yields none.
  QVector<QPair<int, int>> regexRanges(const QTextDocument* document, const QString& pattern) const;

  bool m_caseSensitive = false;
  bool m_regex = false;
};

}  // namespace typist::ui
