#pragma once

#include <QString>
#include <QStringList>

namespace typist::ui {

// Draft store for crash recovery: TYPIST_RECOVERY_DIR overrides ~/.typist/recovery.
QString recoveryDir();

// Stable key for a path; an unnamed buffer shares one untitled slot.
QString recoveryKeyFor(const QString& path);

QString recoveryPathFor(const QString& path);

bool writeDraft(const QString& path, const QString& markdown);

bool readDraft(const QString& path, QString* markdownOut);

// Existing draft files, newest first by modification time.
QStringList draftPaths();

bool removeDraft(const QString& path);

}  // namespace typist::ui
