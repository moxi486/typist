#pragma once

#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>

class QRegularExpression;

namespace typist::editor {
struct CodeRange {
  QTextBlock first, last;
  bool valid() const { return first.isValid() && last.isValid(); }
  QString language() const;
  QString text() const;
};
CodeRange codeRange(const QTextCursor& cursor);
const QStringList& codeLanguages();
const QRegularExpression& codeLanguagePattern();
bool setCodeLanguage(QTextCursor& cursor, const QString& identifier);
bool indentCode(QTextCursor& cursor, bool outdent);
bool backspaceCodeIndent(QTextCursor& cursor);
bool insertCodeLine(QTextCursor& cursor, bool literal = false);
bool insertCodeText(QTextCursor& cursor, QString text);
bool exitCode(QTextCursor& cursor);
}  // namespace typist::editor
