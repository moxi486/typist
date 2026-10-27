#include "Search.h"

namespace typist::ui {

void SearchController::setCaseSensitive(bool enabled) { m_caseSensitive = enabled; }

bool SearchController::caseSensitive() const { return m_caseSensitive; }

QTextDocument::FindFlags SearchController::flags() const {
  return m_caseSensitive ? QTextDocument::FindCaseSensitively : QTextDocument::FindFlags();
}

int SearchController::countMatches(const QTextDocument* document, const QString& pattern) const {
  if (document == nullptr || pattern.isEmpty()) return 0;
  int count = 0;
  int from = 0;
  while (true) {
    const QTextCursor hit = document->find(pattern, from, flags());
    if (hit.isNull()) break;
    ++count;
    // Stepping past the selection end keeps overlapping matches from looping forever.
    from = hit.selectionEnd();
    if (hit.selectionEnd() == hit.selectionStart()) ++from;
  }
  return count;
}

bool SearchController::selectNext(QTextCursor& cursor, const QString& pattern) const {
  if (cursor.isNull() || pattern.isEmpty()) return false;
  QTextDocument* document = cursor.document();
  QTextCursor hit = document->find(pattern, cursor.selectionEnd(), flags());
  if (hit.isNull()) hit = document->find(pattern, 0, flags());
  if (hit.isNull()) return false;
  cursor = hit;
  return true;
}

bool SearchController::selectPrevious(QTextCursor& cursor, const QString& pattern) const {
  if (cursor.isNull() || pattern.isEmpty()) return false;
  QTextDocument* document = cursor.document();
  const QTextDocument::FindFlags backward = flags() | QTextDocument::FindBackward;
  QTextCursor hit = document->find(pattern, cursor.selectionStart(), backward);
  if (hit.isNull()) {
    QTextCursor tail(document);
    tail.movePosition(QTextCursor::End);
    hit = document->find(pattern, tail.position(), backward);
  }
  if (hit.isNull()) return false;
  cursor = hit;
  return true;
}

}  // namespace typist::ui
