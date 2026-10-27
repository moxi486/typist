#pragma once

#include <QString>
#include <QTextCursor>
#include <QTextDocument>
#include <QVector>

namespace typist::ui {

// Plain-text search over a document: counts, forward and backward selection, and
// wrap-around across both ends. Table cell text is reachable through the frame tree.
class SearchController {
 public:
  void setCaseSensitive(bool enabled);
  bool caseSensitive() const;

  int countMatches(const QTextDocument* document, const QString& pattern) const;

  // Cursors for every match, capped so highlighting stays cheap on large documents.
  QVector<QTextCursor> allMatches(const QTextDocument* document, const QString& pattern, int maxHighlights = 1000) const;
  bool selectNext(QTextCursor& cursor, const QString& pattern) const;
  bool selectPrevious(QTextCursor& cursor, const QString& pattern) const;

 private:
  QTextDocument::FindFlags flags() const;

  bool m_caseSensitive = false;
};

}  // namespace typist::ui
