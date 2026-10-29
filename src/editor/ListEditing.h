#pragma once

#include "Editing.h"

#include <QTextBlock>

namespace typist::editor {
enum class TaskStatus { Toggle, Done, Todo };

bool setListStyle(QTextCursor& cursor, ParagraphStyle style);
bool setTaskStatus(QTextCursor& cursor, TaskStatus status);
bool insertListItem(QTextCursor& cursor);
int listContentStart(const QTextBlock& block);
bool selectionHasList(const QTextCursor& cursor);
}  // namespace typist::editor
