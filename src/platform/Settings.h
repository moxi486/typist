#pragma once

#include "FileWriter.h"
#include "PdfWriter.h"

#include <QFlags>
#include <QString>
#include <QStringList>

namespace typist::platform {

// User settings live under the Typist organization so each platform picks its own
// native store: registry on Windows, ini under XDG on Linux and macOS.
QString themePreference(const QString& fallback = QStringLiteral("system"));

void setThemePreference(const QString& value);

QString languagePreference();
void setLanguagePreference(const QString& value);
LineEnding defaultLineEnding();
void setDefaultLineEnding(LineEnding ending);

enum class LaunchMode { NewDocument, LastFolder, LastFileAndFolder, CustomFolder };
struct LaunchOptions {
  LaunchMode mode = LaunchMode::NewDocument;
  QString customFolder;
};
struct StartupTarget {
  QString file;
  QString folder;
  bool importHtml = false;
};
LaunchOptions launchOptions();
void setLaunchOptions(const LaunchOptions& options);
StartupTarget lastClosedTarget();
void setLastClosedTarget(const StartupTarget& target);
// Arguments exclude the executable name. Explicit input and benchmark launches
// take precedence over preferences and saved paths.
StartupTarget startupTarget(const QStringList& arguments, bool benchmark = false,
                            const QString& benchmarkFile = {});

enum class ExportFolderMode { DocumentFolder, LastExportFolder, CustomFolder };
enum class ExportFormat {
  Html = 1 << 0,
  PlainHtml = 1 << 1,
  Png = 1 << 2,
  Pdf = 1 << 3,
  Docx = 1 << 4,
  Epub = 1 << 5
};
struct ExportPreferences {
  ExportFolderMode folderMode = ExportFolderMode::DocumentFolder;
  QString customFolder;
  bool openFolderOnSuccess = false;
  QFlags<ExportFormat> openFileOnSuccess{};
};
ExportPreferences exportPreferences();
void setExportPreferences(const ExportPreferences& preferences);
QString exportDirectory(const QString& documentPath);
void rememberExportDirectory(const QString& outputPath);

bool copyImagesToAssets();
void setCopyImagesToAssets(bool enabled);

enum class WritingWidth { Fill = 0, Compact = 640, Standard = 860, Wide = 1120 };
struct WritingAreaWidths {
  WritingWidth rendered = WritingWidth::Standard;
  WritingWidth source = WritingWidth::Standard;
};
WritingAreaWidths writingAreaWidths();
void setWritingAreaWidths(const WritingAreaWidths& widths);

struct CodeFencePreferences {
  QString language;
  bool useLastLanguage = false;
  bool applyToCommands = true;
  bool applyToTypedFences = true;
};
CodeFencePreferences codeFencePreferences();
void setCodeFencePreferences(const CodeFencePreferences& preferences);
QString lastCodeLanguage();
void setLastCodeLanguage(const QString& language);

// PDF page controls; the defaults are A4 with 20mm margins.
QString pdfPaper(const QString& fallback = QStringLiteral("a4"));

void setPdfPaper(const QString& value);

double pdfMarginMm(double fallback = 20.0);

void setPdfMarginMm(double value);
PdfPageOptions pdfOptions(const QString& title);
void setPdfOptions(const PdfPageOptions& options);

// Most recently opened or saved paths, newest first; stale entries are pruned by callers.
QStringList recentFiles(int limit = 8);

void addRecentFile(const QString& path);

void removeRecentFile(const QString& path);

// Closed documents are newest first. Opening a document removes it from the list.
QStringList closedFiles();
void updateClosedFiles(const QString& closedPath, const QString& openedPath = {});
void clearFileHistory();

enum class FileBrowserView { Tree, List };
enum class FileBrowserSort { Name, Natural, Modified, Created };
struct FileBrowserOptions {
  FileBrowserView view = FileBrowserView::Tree;
  FileBrowserSort sort = FileBrowserSort::Name;
  bool descending = false;
  bool foldersFirst = false;
  bool groupByFolder = false;
  bool showHidden = false;
  bool showNonMarkdown = false;
  QStringList includeGlobs;
  QStringList excludeGlobs;
};
FileBrowserOptions fileBrowserOptions();
void setFileBrowserOptions(const FileBrowserOptions& options);
bool outlineCollapsible();
void setOutlineCollapsible(bool enabled);

}  // namespace typist::platform
