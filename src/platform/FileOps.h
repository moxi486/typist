#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <functional>

class QObject;

namespace typist::platform {

// Sidebar file operations. A name without a .md suffix gets one appended.
bool createMarkdownFile(const QString& dir, const QString& name, QString* error);
// Creates the exact file name, without overwriting a path that already exists.
bool createEmptyFile(const QString& path, QString* error);
bool renameFile(const QString& path, const QString& newName, QString* error);
bool moveMarkdownToTrash(const QString& path, QString* error);
// Moves one entry inside the workspace without replacing any destination entry.
// Returns its new path, or an empty string on failure or if it is already there.
QString moveWorkspaceEntry(const QString& root, const QString& path, const QString& destination,
                           QString* error);
// File identity survives a rename and in-place edits, but changes on replacement.
QByteArray workspaceEntryIdentity(const QString& path);
bool restoreWorkspaceEntry(const QString& root, const QString& path, const QString& originalPath,
                           const QByteArray& identity, QString* error);
// Copies one file or directory into the workspace and publishes it without overwriting.
QString copyIntoWorkspace(const QString& root, const QString& path, const QString& destination,
                          QString* error);
bool createDirectory(const QString& dir, const QString& name, QString* error);
// Copies saved bytes beside the source as name-copy.md, name-copy-2.md, etc.; never overwrites.
QString duplicateMarkdownFile(const QString& path, QString* error);
// Existing directory itself, or the parent of an existing file; empty for unavailable paths.
QUrl containingDirectoryUrl(const QString& path);
// Selects an existing file in the system file manager, or opens its parent as a fallback.
void revealFile(const QString& path, QObject* context, std::function<void()> onFailure);

// Encoded path relative to the document directory when possible, otherwise a file URL.
QString localFileLinkTarget(const QString& path, const QUrl& documentBase);

bool isMarkdownPath(const QString& path);

// First local image path among dropped URLs; empty when none applies.
QString firstImagePath(const QList<QUrl>& urls);

}  // namespace typist::platform
