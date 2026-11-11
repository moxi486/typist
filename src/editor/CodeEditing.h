#pragma once

#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>

class QRegularExpression;

namespace typist::editor {
constexpr int kDefaultCodeIndentSize = 4;
constexpr int kMaxCodeIndentSize = 8;

struct CodeRange {
  QTextBlock first, last;
  bool valid() const { return first.isValid() && last.isValid(); }
  QString language() const;
  QString text() const;
  QTextCursor selection() const;
};
bool isCodeSelection(const QTextCursor& cursor);
CodeRange codeRange(const QTextCursor& cursor);
const QStringList& codeLanguages();
const QRegularExpression& codeLanguagePattern();
bool setCodeLanguage(QTextCursor& cursor, const QString& identifier);
bool indentCode(QTextCursor& cursor, bool outdent, int indentSize);
bool backspaceCodeIndent(QTextCursor& cursor, int indentSize);
bool insertCodeLine(QTextCursor& cursor, bool literal, int indentSize);
bool insertCodeText(QTextCursor& cursor, QString text);
bool exitCode(QTextCursor& cursor);
}  // namespace typist::editor
