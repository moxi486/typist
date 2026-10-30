#pragma once

#include "Conflict.h"
#include "EpubWriter.h"
#include "FolderSearchRunner.h"
#include "PdfWriter.h"
#include "RecoveryStore.h"
#include "Search.h"

#include <QMainWindow>
#include <QPalette>
#include <QString>
#include <QUrl>

class QAction;
class QCloseEvent;
class QDragEnterEvent;
class QDockWidget;
class QDropEvent;
class QListWidget;
class QLabel;
class QToolButton;
class QTabWidget;
class QMouseEvent;
class QMimeData;
class QImage;
class QNetworkAccessManager;
class QNetworkReply;
class QTextDocument;
class QTextEdit;
class QTimer;
namespace typist::editor {
struct HtmlImportResult;
}

namespace typist::ui {
class AutoSave;
class FileTree;
class FindBar;
class ViewSwitch;
class AppTranslator;
class WorkspaceSearchPanel;
class FolderLocationsMenu;
}  // namespace typist::ui

namespace typist {

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  bool openFile(const QString& path);
  bool openFolderPath(const QString& path);
  bool importHtmlFile(const QString& path);

 public slots:
  void followLink(const QString& target);

 public:
  // Typing-to-repaint latency for the performance bench: one edit plus a synchronous repaint.
  qint64 measureTypingRepaint();

  // Lets the bench confirm a file actually loaded before timing keystrokes.
  int documentBlockCount() const;

  // Bench seam: an edit, a switch to source and back must leave undo available.
  bool viewSwitchKeepsUndoForBench();

  // Synthetic keystrokes in bench mode must not be persisted to the file under test.
  void setBenchMode(bool enabled);

  // Bench seam: match count over the document the editor is showing.
  int searchMatchesForBench(const QString& pattern) const;

  // Bench seam: number of highlighted matches for the current case mode.
  int highlightCountForBench(const QString& pattern) const;

  // Bench seam: write the current buffer as DOCX and report bytes written, or -1.
  qint64 exportDocxForBench(const QString& path) const;
  qint64 exportPdfForBench(const QString& path, const platform::PdfPageOptions& options) const;
  qint64 exportEpubForBench(const QString& path, const platform::EpubOptions& options) const;

  // Bench seam: folder scan latency; *hitsOut receives the hit count.
  qint64 searchFolderForBench(const QString& rootDir, const QString& pattern, int* hitsOut);

  // Bench seam: sidebar entry count for a root directory.
  int fileTreeEntriesForBench(const QString& rootDir);

  // Bench seam: sidebar directory count for a root directory.
  int fileTreeDirectoriesForBench(const QString& rootDir);

  // Opens the first Markdown URL from a drop; returns false when nothing applies.
  bool openDroppedFiles(const QList<QUrl>& urls);
  bool saveDocumentAs(const QString& path);

 private slots:
  void openDialog();
  void save();
  void saveAs();
  void undo();
  void redo();
  void toggleSourceView();
  void exportHtml();
  void exportPdf();
  void exportDocx();
  void exportEpub();
  void cycleTheme();
  void autoSaveNow();
  void toggleFindBar();
  void searchNext();
  void searchPrevious();
  void refreshMatches();
  void searchInFolder();
  void rebuildRecentMenu();
  void rebuildOutline();
  void toggleOutline();
  void openOutlineEntry();
  void openRecentEntry(QAction* action);
  void checkCurrentFileConflict();
  void writeRecoveryDraft();
  void recoverLatestDraft();
  void pasteAsMarkdown();
  void pasteHtmlAsMarkdown();
  void importHtmlDialog();
  void toggleTaskAtClick(const QMouseEvent* event);
#ifdef TYPIST_HAS_NETWORK
  void checkForUpdates();
  void handleUpdateReply(class QNetworkReply* reply);
#endif
  void insertImageLink(const QString& path);
  void openSearchHit(const typist::platform::SearchHit& hit);
  void toggleSidebar();
  void openFolder();
  void openTreeEntry(const QString& path);
  void treeFileRenamed(const QString& oldPath, const QString& newPath);
  void treeFileRemoved(const QString& path);

 protected:
  void changeEvent(QEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  void updateTitleAndStatus();
  void markClean();
  QString currentMarkdown() const;
  QString themePreference() const;
  void applyTheme();
  void applyThemePalette(const QPalette& palette);
  void setLanguage(const QString& preference);
  void retranslateUi();
  void updateSearchFocusGuard();
  void createEditingActions(QMenu* file, QMenu* edit, QMenu* view, QMenu* settings);
  bool maybeSaveBeforeNavigate();
  bool adoptHtmlImport(const editor::HtmlImportResult& result);
  bool pasteImages(const QMimeData* source);
  bool insertImageMarkdown(const QStringList& images);
  void newDocument();
  void openQuickly();
  void insertTableDialog();
  void insertLinkDialog();
  void showDocumentStatistics();
  void replaceOne();
  void replaceAll();
  void closeEvent(QCloseEvent* event) override;

  QTextEdit* m_editor = nullptr;
  QTextDocument* m_document = nullptr;
  ui::FindBar* m_findBar = nullptr;
  QDockWidget* m_sidebarDock = nullptr;
  ui::FileTree* m_tree = nullptr;
  ui::SearchController m_search;
  ui::WorkspaceSearchPanel* m_workspaceSearch = nullptr;
  bool m_searchFocusHeld = false;
  bool m_searchFocusPrevious = true;
  QAction* m_undoAction = nullptr;
  QAction* m_redoAction = nullptr;
  QAction* m_sourceAction = nullptr;
  QAction* m_themeAction = nullptr;
  QAction* m_findAction = nullptr;
  QAction* m_folderSearchAction = nullptr;
  QAction* m_sidebarAction = nullptr;
  QAction* m_folderAction = nullptr;
  ui::AutoSave* m_autoSave = nullptr;
  QMenu* m_recentMenu = nullptr;
  QListWidget* m_outlineList = nullptr;
  QTabWidget* m_sidebarTabs = nullptr;
  QLabel* m_sidebarRootLabel = nullptr;
  QToolButton* m_fileOptionsButton = nullptr;
  QToolButton* m_folderLocationsButton = nullptr;
  ui::FolderLocationsMenu* m_folderLocationsMenu = nullptr;
  QAction* m_outlineAction = nullptr;
  QAction* m_pasteMarkdownAction = nullptr;
  QAction* m_updateAction = nullptr;
  QLabel* m_detailsLabel = nullptr;
  ui::AppTranslator* m_translator = nullptr;
  QTimer* m_outlineRefresh = nullptr;
  bool m_autoSaveEnabled = true;
#ifdef TYPIST_HAS_NETWORK
  QNetworkAccessManager* m_network = nullptr;
#endif
  ui::ViewSwitch* m_view = nullptr;
  bool m_benchMode = false;
  QString m_currentPath;
  QString m_savedMarkdown;
  QString m_themePreference;
  bool m_dirty = false;
  bool m_sourceMode = false;
};

}  // namespace typist
