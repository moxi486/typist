#include "CodeEditing.h"

#include "SyntaxHighlighter.h"
#include "editor_document.h"

#include <QTextDocument>

namespace typist::editor {
namespace {
constexpr int tabWidth = 4;
int leadingSize(const QString& text) {
  int size = 0;
  while (size < text.size() && (text.at(size) == ' ' || text.at(size) == '\t')) ++size;
  return size;
}
int columns(const QString& text) {
  int column = 0;
  for (const auto ch : text) column += ch == '\t' ? tabWidth - column % tabWidth : 1;
  return column;
}
int outdentSize(const QString& text) {
  if (text.startsWith('\t')) return 1;
  return qMin(tabWidth, leadingSize(text.left(tabWidth)));
}
bool codeSelection(const QTextCursor& cursor) {
  const int group = cursor.document()
                        ->findBlock(cursor.selectionStart())
                        .blockFormat()
                        .intProperty(kCodeBlockProperty);
  if (!group || cursor.currentTable()) return false;
  const int end = cursor.hasSelection() ? cursor.selectionEnd() - 1 : cursor.position();
  for (auto block = cursor.document()->findBlock(cursor.selectionStart());
       block.isValid() && block.position() <= end; block = block.next())
    if (block.blockFormat().intProperty(kCodeBlockProperty) != group) return false;
  return true;
}
bool syntaxAllowsIndent(const QTextBlock& current, const QString& prefix) {
  // Only candidate openers need lexical work. Bound the scan for very large fences;
  // ordinary newline insertion always retains indentation without scanning the fence.
  QList<QTextBlock> previous;
  const int group = current.blockFormat().intProperty(kCodeBlockProperty);
  for (auto block = current.previous();
       block.isValid() && block.blockFormat().intProperty(kCodeBlockProperty) == group;
       block = block.previous()) {
    if (previous.size() >= 512 || block.text().size() > 32768) return false;
    previous.prepend(block);
  }
  render::SyntaxState state;
  QString language = current.blockFormat().property(kCodeInfoProperty).toString();
  if (language.isEmpty() && !previous.isEmpty())
    language = previous.first().blockFormat().property(kCodeInfoProperty).toString();
  for (const auto& block : previous) render::codeSyntax(block.text(), language, state);
  const auto spans = render::codeSyntax(prefix, language, state);
  const int last = prefix.trimmed().size() + leadingSize(prefix) - 1;
  if (!state.delimiter.isEmpty() || state.comment) return false;
  for (const auto& span : spans)
    if (last >= span.start && last < span.start + span.length &&
        (span.token == render::SyntaxToken::String || span.token == render::SyntaxToken::Comment))
      return false;
  return true;
}
void splitLine(QTextCursor& cursor, const QString& indent) {
  auto format = cursor.blockFormat();
  const int group = format.intProperty(kCodeBlockProperty);
  const bool last = cursor.block().next().blockFormat().intProperty(kCodeBlockProperty) != group;
  format.setBottomMargin(0);
  format.clearProperty(kEmptyCodeFenceProperty);
  cursor.setBlockFormat(format);
  format.setTopMargin(0);
  format.setBottomMargin(last ? 10 : 0);
  // Block char format keeps the monospace font even on an empty fence.
  const auto character = cursor.block().charFormat();
  cursor.insertBlock(format, character);
  if (!indent.isEmpty()) cursor.insertText(indent, character);
}
}  // namespace

QString CodeRange::language() const {
  return first.blockFormat().property(kCodeInfoProperty).toString();
}
QString CodeRange::text() const {
  if (!valid()) return {};
  if (first == last && first.text().isEmpty() &&
      first.blockFormat().boolProperty(kEmptyCodeFenceProperty))
    return {};
  QString result;
  for (auto block = first; block.isValid(); block = block.next()) {
    result += block.text() + '\n';
    if (block == last) break;
  }
  return result;
}
CodeRange codeRange(const QTextCursor& cursor) {
  if (!codeSelection(cursor)) return {};
  auto first = cursor.document()->findBlock(cursor.selectionStart()), last = first;
  const int group = first.blockFormat().intProperty(kCodeBlockProperty);
  while (first.previous().isValid() &&
         first.previous().blockFormat().intProperty(kCodeBlockProperty) == group)
    first = first.previous();
  while (last.next().isValid() &&
         last.next().blockFormat().intProperty(kCodeBlockProperty) == group)
    last = last.next();
  return {first, last};
}
bool indentCode(QTextCursor& cursor, bool outdent) {
  if (!codeSelection(cursor)) return false;
  if (!outdent && !cursor.hasSelection()) {
    const int width =
        tabWidth - columns(cursor.block().text().left(cursor.positionInBlock())) % tabWidth;
    cursor.insertText(QString(width, ' '));
    return true;
  }
  const bool selected = cursor.hasSelection(), forward = cursor.position() >= cursor.anchor();
  int start = cursor.selectionStart(), end = cursor.selectionEnd();
  const int lastPosition = selected ? end - 1 : end;
  QList<QTextBlock> blocks;
  for (auto block = cursor.document()->findBlock(start);
       block.isValid() && block.position() <= lastPosition; block = block.next())
    blocks.append(block);
  cursor.beginEditBlock();
  for (auto it = blocks.crbegin(); it != blocks.crend(); ++it) {
    const int position = it->position(), remove = outdent ? outdentSize(it->text()) : 0;
    QTextCursor line(*it);
    if (outdent) {
      line.setPosition(position + remove, QTextCursor::KeepAnchor);
      line.removeSelectedText();
      const auto adjust = [position, remove](int value) {
        return value > position ? qMax(position, value - remove) : value;
      };
      start = adjust(start);
      end = adjust(end);
    } else {
      line.insertText(QString(tabWidth, ' '));
      if (position < start) start += tabWidth;
      if (position <= end) end += tabWidth;
    }
  }
  cursor.endEditBlock();
  cursor.setPosition(selected && !forward ? end : start);
  cursor.setPosition(selected && !forward ? start : end,
                     selected ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
  return true;
}
bool backspaceCodeIndent(QTextCursor& cursor) {
  if (cursor.hasSelection() || !codeSelection(cursor) || cursor.atBlockStart()) return false;
  const QString prefix = cursor.block().text().left(cursor.positionInBlock());
  if (leadingSize(prefix) != prefix.size()) return false;
  const int target = ((columns(prefix) - 1) / tabWidth) * tabWidth;
  int size = int(prefix.size());
  while (size > 0 && columns(prefix.left(size)) > target) --size;
  const int end = cursor.position();
  cursor.setPosition(cursor.block().position() + size);
  cursor.setPosition(end, QTextCursor::KeepAnchor);
  cursor.removeSelectedText();
  return true;
}
bool insertCodeLine(QTextCursor& cursor, bool literal) {
  if (!codeSelection(cursor)) return false;
  cursor.beginEditBlock();
  cursor.removeSelectedText();
  const QString text = cursor.block().text();
  const QString prefix = text.left(cursor.positionInBlock()), trimmed = prefix.trimmed();
  const QString indent = prefix.left(leadingSize(prefix));
  const QString language = cursor.blockFormat().property(kCodeInfoProperty).toString().toLower();
  const QChar opener = trimmed.isEmpty() ? QChar() : trimmed.back();
  const bool brace = opener == '{' || opener == '[' || opener == '(';
  const bool colon =
      opener == ':' && (language == "python" || language == "py" || language == "python3");
  const bool extra = !literal && (brace || colon) && syntaxAllowsIndent(cursor.block(), prefix);
  const QString unit = indent.contains('\t') ? QStringLiteral("\t") : QString(tabWidth, ' ');
  const QChar close = opener == '{' ? QChar('}') : opener == '[' ? QChar(']') : QChar(')');
  const bool pair = extra && brace && text.mid(cursor.positionInBlock()).trimmed() == close;
  if (pair) {
    const int spaces = leadingSize(text.mid(cursor.positionInBlock()));
    for (int i = 0; i < spaces; ++i) cursor.deleteChar();
  }
  splitLine(cursor, indent + (extra ? unit : QString()));
  if (pair) {
    const int caret = cursor.position();
    splitLine(cursor, indent);
    cursor.setPosition(caret);
  }
  cursor.endEditBlock();
  return true;
}
bool insertCodeText(QTextCursor& cursor, QString text) {
  if (!codeSelection(cursor)) return false;
  text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
  text.replace('\r', '\n');
  text.replace(QChar::ParagraphSeparator, QChar('\n'));
  cursor.beginEditBlock();
  cursor.removeSelectedText();
  const auto lines = text.split('\n');
  for (int i = 0; i < lines.size(); ++i) {
    if (i) splitLine(cursor, {});
    cursor.insertText(lines.at(i), cursor.block().charFormat());
  }
  cursor.endEditBlock();
  return true;
}
bool exitCode(QTextCursor& cursor) {
  const auto range = codeRange(cursor);
  if (!range.valid()) return false;
  const auto next = range.last.next();
  if (next.isValid() && !next.textList() && next.blockFormat().headingLevel() == 0 &&
      !next.blockFormat().hasProperty(kCodeBlockProperty) &&
      !next.blockFormat().hasProperty(kQuoteProperty) &&
      !next.blockFormat().hasProperty(kSpecialBlockProperty) &&
      !next.blockFormat().hasProperty(kRuleProperty) &&
      !next.blockFormat().hasProperty(kFootnoteDefinitionProperty) &&
      !QTextCursor(next).currentTable()) {
    cursor = QTextCursor(next);
  } else {
    cursor = QTextCursor(range.last);
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.beginEditBlock();
    auto spacing = cursor.blockFormat();
    spacing.setBottomMargin(10);
    cursor.setBlockFormat(spacing);
    QTextBlockFormat body;
    body.setLineHeight(160, QTextBlockFormat::ProportionalHeight);
    body.setBottomMargin(12);
    cursor.insertBlock(body, QTextCharFormat());
    cursor.endEditBlock();
  }
  cursor.setCharFormat(QTextCharFormat());
  return true;
}
}  // namespace typist::editor
