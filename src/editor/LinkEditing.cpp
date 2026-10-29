#include "LinkEditing.h"

#include "editor_document.h"

#include <QTextBlock>
#include <QTextDocument>
#include <QUrl>

namespace typist::editor {
namespace {
QString targetAt(QTextDocument* document, int position) {
  if (position < 0 || position >= document->characterCount() - 1) return {};
  QTextCursor character(document);
  character.setPosition(position);
  character.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
  const auto format = character.charFormat();
  if (!format.isAnchor() || format.hasProperty(kImageProperty) ||
      format.hasProperty(kFootnoteReferenceProperty) || format.hasProperty(kMathSourceProperty))
    return {};
  return format.anchorHref();
}
}  // namespace
QTextCursor linkSelection(const QTextCursor& cursor) {
  if (cursor.isNull()) return {};
  auto* document = cursor.document();
  int start = cursor.selectionStart();
  auto target = targetAt(document, start);
  if (target.isEmpty() && !cursor.hasSelection()) target = targetAt(document, --start);
  if (target.isEmpty()) return {};
  const auto block = document->findBlock(start);
  int end = start + 1;
  while (start > block.position() && targetAt(document, start - 1) == target) --start;
  while (end < block.position() + block.length() - 1 && targetAt(document, end) == target) ++end;
  if (cursor.hasSelection() && cursor.selectionEnd() > end) return {};
  QTextCursor link(document);
  link.setPosition(start);
  link.setPosition(end, QTextCursor::KeepAnchor);
  return link;
}
QString normalizedLink(const QString& destination) {
  auto value = destination.trimmed();
  value.replace('\\', '/');
  value = QUrl(value, QUrl::TolerantMode).toString(QUrl::FullyEncoded);
  value.replace('(', QStringLiteral("%28"));
  value.replace(')', QStringLiteral("%29"));
  return value;
}
bool replaceLink(QTextCursor& cursor, const QString& destination, const QString& label) {
  const auto target = normalizedLink(destination);
  if (target.isEmpty() || label.isEmpty() || label.contains('\n') ||
      label.contains(QChar::ParagraphSeparator))
    return false;
  if (cursor.hasSelection() && cursor.document()->findBlock(cursor.selectionStart()) !=
                                   cursor.document()->findBlock(cursor.selectionEnd() - 1))
    return false;
  cursor.beginEditBlock();
  QTextCharFormat format;
  format.setAnchor(true);
  format.setAnchorHref(target);
  format.setFontUnderline(true);
  if (cursor.hasSelection() && cursor.selectedText() == label)
    cursor.mergeCharFormat(format);
  else {
    auto characters = cursor.charFormat();
    characters.merge(format);
    cursor.insertText(label, characters);
  }
  cursor.endEditBlock();
  return true;
}
bool removeLink(QTextCursor& cursor) {
  auto link = linkSelection(cursor);
  if (link.isNull()) return false;
  QTextCharFormat format;
  format.setAnchor(false);
  format.setAnchorHref(QString());
  format.setFontUnderline(false);
  link.beginEditBlock();
  link.mergeCharFormat(format);
  link.endEditBlock();
  cursor = link;
  return true;
}
}  // namespace typist::editor
