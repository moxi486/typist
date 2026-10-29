#include "Search.h"

#include <QRegularExpression>
#include <QTextBlock>

namespace typist::ui {

void SearchController::setCaseSensitive(bool enabled) { m_caseSensitive = enabled; }

bool SearchController::caseSensitive() const { return m_caseSensitive; }

void SearchController::setRegex(bool enabled) { m_regex = enabled; }

bool SearchController::regex() const { return m_regex; }

QTextDocument::FindFlags SearchController::flags() const {
  return m_caseSensitive ? QTextDocument::FindCaseSensitively : QTextDocument::FindFlags();
}

QRegularExpression SearchController::regexPattern(const QString& pattern) const {
  const QRegularExpression::PatternOptions option =
      QRegularExpression::UseUnicodePropertiesOption |
      (m_caseSensitive ? QRegularExpression::NoPatternOption
                       : QRegularExpression::CaseInsensitiveOption);
  return QRegularExpression(pattern, option);
}

QVector<QPair<int, int>> SearchController::regexRanges(const QTextDocument* document,
                                                       const QString& pattern) const {
  QVector<QPair<int, int>> ranges;
  const QRegularExpression expression = regexPattern(pattern);
  if (!expression.isValid())
    return ranges;  // an invalid pattern matches nothing rather than throwing
  for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
    QRegularExpressionMatchIterator it = expression.globalMatch(block.text());
    while (it.hasNext()) {
      const QRegularExpressionMatch match = it.next();
      if (match.capturedLength() == 0) continue;  // empty matches would stack at the same position
      ranges.append(qMakePair(block.position() + match.capturedStart(),
                              block.position() + match.capturedEnd()));
    }
  }
  return ranges;
}

int SearchController::countMatches(const QTextDocument* document, const QString& pattern) const {
  if (document == nullptr || pattern.isEmpty()) return 0;
  if (m_regex) return regexRanges(document, pattern).size();
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

QVector<QTextCursor> SearchController::allMatches(const QTextDocument* document,
                                                  const QString& pattern, int maxHighlights) const {
  QVector<QTextCursor> hits;
  if (document == nullptr || pattern.isEmpty()) return hits;
  if (m_regex) {
    for (const QPair<int, int>& range : regexRanges(document, pattern)) {
      // Selection building needs a non-const cursor; matching itself changes nothing.
      QTextCursor cursor(const_cast<QTextDocument*>(document));
      cursor.setPosition(range.first, QTextCursor::MoveAnchor);
      cursor.setPosition(range.second, QTextCursor::KeepAnchor);
      hits.append(cursor);
      if (maxHighlights > 0 && hits.size() >= maxHighlights) break;
    }
    return hits;
  }
  int from = 0;
  while (from < document->characterCount()) {
    const QTextCursor hit = document->find(pattern, from, flags());
    if (hit.isNull()) break;
    hits.append(hit);
    if (maxHighlights > 0 && hits.size() >= maxHighlights) break;
    from = hit.selectionEnd();
    if (hit.selectionEnd() == hit.selectionStart()) ++from;
  }
  return hits;
}

bool SearchController::selectNext(QTextCursor& cursor, const QString& pattern) const {
  if (cursor.isNull() || pattern.isEmpty()) return false;
  QTextDocument* document = cursor.document();
  if (m_regex) {
    const QVector<QPair<int, int>> ranges = regexRanges(document, pattern);
    if (ranges.isEmpty()) return false;
    const int from = cursor.selectionEnd();
    int chosen = 0;
    for (int i = 0; i < ranges.size(); ++i) {
      if (ranges.at(i).first >= from) {
        chosen = i;
        break;
      }
    }
    cursor = QTextCursor(const_cast<QTextDocument*>(document));
    cursor.setPosition(ranges.at(chosen).first, QTextCursor::MoveAnchor);
    cursor.setPosition(ranges.at(chosen).second, QTextCursor::KeepAnchor);
    return true;
  }
  QTextCursor hit = document->find(pattern, cursor.selectionEnd(), flags());
  if (hit.isNull()) hit = document->find(pattern, 0, flags());
  if (hit.isNull()) return false;
  cursor = hit;
  return true;
}

bool SearchController::selectPrevious(QTextCursor& cursor, const QString& pattern) const {
  if (cursor.isNull() || pattern.isEmpty()) return false;
  QTextDocument* document = cursor.document();
  if (m_regex) {
    const QVector<QPair<int, int>> ranges = regexRanges(document, pattern);
    if (ranges.isEmpty()) return false;
    const int from = cursor.selectionStart();
    int chosen = ranges.size() - 1;
    for (int i = ranges.size() - 1; i >= 0; --i) {
      if (ranges.at(i).first < from) {
        chosen = i;
        break;
      }
    }
    cursor = QTextCursor(document);
    cursor.setPosition(ranges.at(chosen).first);
    cursor.setPosition(ranges.at(chosen).second, QTextCursor::KeepAnchor);
    return true;
  }
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

QString SearchController::replacementFor(const QTextCursor& cursor, const QString& pattern,
                                         const QString& replacement) const {
  if (!m_regex) return replacement;
  const QTextBlock block = cursor.document()->findBlock(cursor.selectionStart());
  const auto match =
      regexPattern(pattern).match(block.text(), cursor.selectionStart() - block.position());
  QString result;
  for (int index = 0; index < replacement.size(); ++index) {
    const QChar ch = replacement.at(index);
    if ((ch == QLatin1Char('$') || ch == QLatin1Char('\\')) && index + 1 < replacement.size() &&
        replacement.at(index + 1).isDigit()) {
      QString number;
      while (index + 1 < replacement.size() && replacement.at(index + 1).isDigit())
        number += replacement.at(++index);
      result += match.captured(number.toInt());
    } else if (ch == QLatin1Char('$') && index + 1 < replacement.size() &&
               replacement.at(index + 1) == QLatin1Char('$')) {
      result += QLatin1Char('$');
      ++index;
    } else
      result += ch;
  }
  return result;
}

bool SearchController::replaceNext(QTextCursor& cursor, const QString& pattern,
                                   const QString& replacement) const {
  if (pattern.isEmpty() || cursor.isNull()) return false;
  bool selectedMatch = false;
  if (cursor.hasSelection()) {
    if (m_regex) {
      const QTextBlock block = cursor.document()->findBlock(cursor.selectionStart());
      const auto match =
          regexPattern(pattern).match(block.text(), cursor.selectionStart() - block.position());
      selectedMatch = match.hasMatch() &&
                      block.position() + match.capturedStart() == cursor.selectionStart() &&
                      match.capturedLength() == cursor.selectedText().size();
    } else
      selectedMatch = cursor.selectedText().compare(
                          pattern, m_caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive) == 0;
  }
  if (!selectedMatch && !selectNext(cursor, pattern)) return false;
  const QString text = replacementFor(cursor, pattern, replacement);
  cursor.beginEditBlock();
  cursor.insertText(text);
  cursor.endEditBlock();
  return true;
}

int SearchController::replaceAll(QTextDocument* document, const QString& pattern,
                                 const QString& replacement) const {
  const auto matches = allMatches(document, pattern, 0);
  if (matches.isEmpty()) return 0;
  QVector<QString> replacements;
  for (const auto& cursor : matches)
    replacements.append(replacementFor(cursor, pattern, replacement));
  QTextCursor group(document);
  group.beginEditBlock();
  // Stable original matches and reverse edits prevent inserted text matching again.
  for (int index = matches.size() - 1; index >= 0; --index) {
    QTextCursor cursor = matches.at(index);
    cursor.insertText(replacements.at(index));
  }
  group.endEditBlock();
  return matches.size();
}

}  // namespace typist::ui
