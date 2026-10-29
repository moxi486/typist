#pragma once
#include <QTextCursor>

namespace typist::editor {
QTextCursor linkSelection(const QTextCursor& cursor);
QString normalizedLink(const QString& destination);
bool replaceLink(QTextCursor& cursor, const QString& destination, const QString& label);
bool removeLink(QTextCursor& cursor);
}  // namespace typist::editor
