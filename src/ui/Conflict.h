#pragma once

namespace typist::ui {

enum class ConflictChoice { KeepEditor, ReloadDisk };

// A clean buffer follows the disk; a dirty buffer never silently loses edits.
ConflictChoice decideConflict(bool dirty, bool diskDiffers, bool canPrompt);

}  // namespace typist::ui
