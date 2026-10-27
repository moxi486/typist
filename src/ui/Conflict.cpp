#include "Conflict.h"

namespace typist::ui {

ConflictChoice decideConflict(bool dirty, bool diskDiffers, bool canPrompt) {
  if (!diskDiffers) return ConflictChoice::KeepEditor;
  if (!dirty) return ConflictChoice::ReloadDisk;
  if (!canPrompt) return ConflictChoice::KeepEditor;
  return ConflictChoice::ReloadDisk;
}

}  // namespace typist::ui
